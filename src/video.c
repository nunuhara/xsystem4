/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 * Copyright (C) 2000- Fumihiko Murata <fmurata@p1.tcnet.ne.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <SDL.h>
#include "gfx/gl.h"
#if defined(USE_GLES) && defined(__linux__)
#include <GLES2/gl2ext.h>
#endif
#include <cglm/cglm.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#ifdef __SWITCH__
#include "hub/controller_runtime.h"
#endif
#ifdef __ANDROID__
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/system_properties.h>
#include <android/log.h>
#endif

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/private.h"
#include "icon.h"
#include "modifier.h"
#include "xsystem4.h"
#ifdef __SWITCH__
#include "hub/runtime_menu_engines.h"
static Texture *switch_menu_surface;
#endif
#ifdef ALICEHUB_IOS
#include "EnginePlatform.h"
#include "gfx/font.h"
#include "mixer.h"
#endif

struct sdl_private sdl;

#ifdef __SWITCH__
static void draw_switch_rect(void *context, const SDL_Rect *rect, SDL_Color color)
{
	int height = *(int *)context;
	glScissor(rect->x, height - rect->y - rect->h, rect->w, rect->h);
	glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static void draw_switch_controls(void)
{
	if (!switch_controls_touch_enabled())
		return;
	int width, height;
	SDL_GL_GetDrawableSize(sdl.window, &width, &height);
	GLint scissor[4];
	GLfloat color[4];
	GLboolean enabled = glIsEnabled(GL_SCISSOR_TEST);
	glGetIntegerv(GL_SCISSOR_BOX, scissor);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, color);
	glEnable(GL_SCISSOR_TEST);
	switch_controls_draw(width, height, draw_switch_rect, &height);
	glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
	glClearColor(color[0], color[1], color[2], color[3]);
	if (!enabled)
		glDisable(GL_SCISSOR_TEST);
}
#endif

static const GLchar glsl_preamble[] =
#ifdef USE_GLES
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp sampler2D;\n"
	"precision highp usampler2D;\n";
#else
	"#version 140\n";
#endif

/*
 * Transform from the window coordinate system -> clip-space.
 *
 * Window CS:  x= 0..w, y= 0..h (+y down)
 * clip-space: x=-1..1, y=-1..1 (+y up)
 *
 * +-                 -+
 * | 2/w   0    0   -1 |
 * |  0   -2/h  0    1 |
 * |  0    0    2   -1 |
 * |  0    0    0    1 |
 * +-                 -+
 */
static mat4 world_view_transform = MAT4(
	0, 0, 0, -1,
	0, 0, 0, -1,
	0, 0, 1, -1,
	0, 0, 0,  1);

static struct shader default_shader;

static GLuint main_surface_fb;
static GLuint offscreen_draw_fbo;
static GLuint offscreen_read_fbo;
static GLuint offscreen_draw_tex;
static GLuint offscreen_read_tex;
static GLuint pinned_draw_tex;
static bool pinned_draw_bound;
static struct texture main_surface;
static struct texture *view = &main_surface;
static GLint max_texture_size;

struct tracked_tex_entry {
	GLuint handle;
	int w, h;
	GLenum internal_format;
	bool skip_backup;
	uint8_t state; /* 0 = empty, 1 = occupied, 2 = tombstone */
};

struct backup_tex_entry {
	GLuint old_handle;
	int w, h;
	GLenum internal_format;
	GLint min_filter;
	GLint mag_filter;
	GLint wrap_s;
	GLint wrap_t;
	uint8_t *pixels;
};

struct remap_tex_entry {
	uint32_t orig_gen;
	GLuint orig_handle;
	uint32_t cur_gen;
	GLuint cur_handle;
	uint8_t state; /* 0 = empty, 1 = occupied */
};

struct reset_cb_entry {
	gfx_reset_callback cb;
	void *user;
};

static uint32_t gfx_ctx_gen = 1;
static struct tracked_tex_entry *live_tex_table = NULL;
static size_t live_tex_cap = 0;
static size_t live_tex_count = 0;
static size_t live_tex_used = 0;
static struct backup_tex_entry *backup_tex_list = NULL;
static size_t backup_tex_count = 0;
static size_t backup_tex_cap = 0;
static struct backup_tex_entry backup_main_surface = {0};
static bool has_backup_main_surface = false;
static struct remap_tex_entry *remap_table = NULL;
static size_t remap_cap = 0;
static size_t remap_count = 0;
static struct reset_cb_entry *reset_callbacks = NULL;
static size_t nr_reset_callbacks = 0;
static bool gfx_initialized = false;
static bool gfx_recovering = false;

static uint32_t hash_u32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static void live_tex_rehash(size_t new_cap)
{
	struct tracked_tex_entry *nt = xcalloc(new_cap, sizeof(*nt));
	size_t mask = new_cap - 1;
	for (size_t i = 0; i < live_tex_cap; i++) {
		if (live_tex_table[i].state != 1)
			continue;
		size_t idx = hash_u32(live_tex_table[i].handle) & mask;
		while (nt[idx].state == 1)
			idx = (idx + 1) & mask;
		nt[idx] = live_tex_table[i];
	}
	free(live_tex_table);
	live_tex_table = nt;
	live_tex_cap = new_cap;
	live_tex_used = live_tex_count;
}

static void track_live_texture(GLuint handle, int w, int h, GLenum internal_format, bool skip_backup)
{
	if (!handle)
		return;
	if (!live_tex_cap)
		live_tex_rehash(128);
	else if ((live_tex_used + 1) * 10 >= live_tex_cap * 7)
		live_tex_rehash(live_tex_count * 2 >= live_tex_cap ? live_tex_cap * 2 : live_tex_cap);
	size_t mask = live_tex_cap - 1;
	size_t idx = hash_u32(handle) & mask;
	size_t first_tomb = (size_t)-1;
	while (live_tex_table[idx].state != 0) {
		if (live_tex_table[idx].state == 1 && live_tex_table[idx].handle == handle) {
			live_tex_table[idx].w = w;
			live_tex_table[idx].h = h;
			live_tex_table[idx].internal_format = internal_format;
			live_tex_table[idx].skip_backup = skip_backup;
			return;
		}
		if (live_tex_table[idx].state == 2 && first_tomb == (size_t)-1)
			first_tomb = idx;
		idx = (idx + 1) & mask;
	}
	size_t dst = first_tomb != (size_t)-1 ? first_tomb : idx;
	if (live_tex_table[dst].state == 0)
		live_tex_used++;
	live_tex_table[dst] = (struct tracked_tex_entry){
		.handle = handle,
		.w = w,
		.h = h,
		.internal_format = internal_format,
		.skip_backup = skip_backup,
		.state = 1,
	};
	live_tex_count++;
}

static void untrack_live_texture(GLuint handle)
{
	if (!handle || !live_tex_cap)
		return;
	size_t mask = live_tex_cap - 1;
	size_t idx = hash_u32(handle) & mask;
	while (live_tex_table[idx].state != 0) {
		if (live_tex_table[idx].state == 1 && live_tex_table[idx].handle == handle) {
			live_tex_table[idx].state = 2;
			live_tex_table[idx].handle = 0;
			live_tex_count--;
			return;
		}
		idx = (idx + 1) & mask;
	}
}

static void clear_live_textures(void)
{
	if (live_tex_table && live_tex_cap)
		memset(live_tex_table, 0, live_tex_cap * sizeof(*live_tex_table));
	live_tex_count = 0;
	live_tex_used = 0;
}

static uint32_t hash_gen_handle(uint32_t gen, GLuint handle)
{
	return hash_u32(gen * 0x9e3779b1u ^ handle);
}

static void remap_rehash(size_t new_cap)
{
	struct remap_tex_entry *nt = xcalloc(new_cap, sizeof(*nt));
	size_t mask = new_cap - 1;
	for (size_t i = 0; i < remap_cap; i++) {
		if (remap_table[i].state != 1)
			continue;
		size_t idx = hash_gen_handle(remap_table[i].orig_gen, remap_table[i].orig_handle) & mask;
		while (nt[idx].state == 1)
			idx = (idx + 1) & mask;
		nt[idx] = remap_table[i];
	}
	free(remap_table);
	remap_table = nt;
	remap_cap = new_cap;
}

static void remap_insert(uint32_t orig_gen, GLuint orig_handle, uint32_t cur_gen, GLuint cur_handle)
{
	if (!orig_gen || !orig_handle)
		return;
	if (!remap_cap)
		remap_rehash(128);
	else if ((remap_count + 1) * 10 >= remap_cap * 7)
		remap_rehash(remap_cap * 2);
	size_t mask = remap_cap - 1;
	size_t idx = hash_gen_handle(orig_gen, orig_handle) & mask;
	while (remap_table[idx].state == 1) {
		if (remap_table[idx].orig_gen == orig_gen && remap_table[idx].orig_handle == orig_handle) {
			remap_table[idx].cur_gen = cur_gen;
			remap_table[idx].cur_handle = cur_handle;
			return;
		}
		idx = (idx + 1) & mask;
	}
	remap_table[idx] = (struct remap_tex_entry){
		.orig_gen = orig_gen,
		.orig_handle = orig_handle,
		.cur_gen = cur_gen,
		.cur_handle = cur_handle,
		.state = 1,
	};
	remap_count++;
}

static struct remap_tex_entry *remap_find(uint32_t orig_gen, GLuint orig_handle)
{
	if (!remap_cap || !orig_gen || !orig_handle)
		return NULL;
	size_t mask = remap_cap - 1;
	size_t idx = hash_gen_handle(orig_gen, orig_handle) & mask;
	while (remap_table[idx].state == 1) {
		if (remap_table[idx].orig_gen == orig_gen && remap_table[idx].orig_handle == orig_handle)
			return &remap_table[idx];
		idx = (idx + 1) & mask;
	}
	return NULL;
}

void gfx_register_reset_callback(gfx_reset_callback cb, void *user)
{
	if (!cb)
		return;
	for (size_t i = 0; i < nr_reset_callbacks; i++) {
		if (reset_callbacks[i].cb == cb && reset_callbacks[i].user == user)
			return;
	}
	reset_callbacks = xrealloc(reset_callbacks, (nr_reset_callbacks + 1) * sizeof(*reset_callbacks));
	reset_callbacks[nr_reset_callbacks++] = (struct reset_cb_entry){ .cb = cb, .user = user };
}

static void invalidate_remap_target(GLuint handle)
{
	if (!handle || remap_count == 0)
		return;
	for (size_t i = 0; i < remap_cap; i++) {
		if (remap_table[i].state == 1 &&
		    remap_table[i].cur_gen == gfx_ctx_gen &&
		    remap_table[i].cur_handle == handle) {
			remap_table[i].cur_handle = 0;
		}
	}
}

void gfx_unregister_reset_callback(gfx_reset_callback cb, void *user)
{
	if (!cb || !reset_callbacks)
		return;
	for (size_t i = 0; i < nr_reset_callbacks; i++) {
		if (reset_callbacks[i].cb == cb && reset_callbacks[i].user == user) {
			if (i + 1 < nr_reset_callbacks) {
				memmove(&reset_callbacks[i], &reset_callbacks[i + 1],
				        (nr_reset_callbacks - i - 1) * sizeof(*reset_callbacks));
			}
			nr_reset_callbacks--;
			return;
		}
	}
}

GLuint gfx_remap_texture_handle(GLuint old_handle)
{
	if (!old_handle || gfx_ctx_gen <= 1)
		return 0;
	struct remap_tex_entry *e = remap_find(gfx_ctx_gen - 1, old_handle);
	if (e && e->cur_gen == gfx_ctx_gen)
		return e->cur_handle;
	return 0;
}

bool gfx_remap_texture(struct texture *t)
{
	if (!t || !t->handle)
		return false;
	if (t->ctx_gen == gfx_ctx_gen)
		return true;
	if (t->ctx_gen == 0)
		return glIsTexture(t->handle) == GL_TRUE;
	struct remap_tex_entry *e = remap_find(t->ctx_gen, t->handle);
	if (e && e->cur_gen == gfx_ctx_gen && e->cur_handle) {
		t->handle = e->cur_handle;
		t->ctx_gen = gfx_ctx_gen;
		return true;
	}
	t->handle = 0;
	t->opaque = false;
	t->ctx_gen = 0;
	return false;
}

void gfx_release_context_backup(void)
{
	for (size_t i = 0; i < backup_tex_count; i++)
		free(backup_tex_list[i].pixels);
	free(backup_tex_list);
	backup_tex_list = NULL;
	backup_tex_count = 0;
	backup_tex_cap = 0;
	free(backup_main_surface.pixels);
	memset(&backup_main_surface, 0, sizeof(backup_main_surface));
	has_backup_main_surface = false;
}

static bool snapshot_texture_pixels(GLuint handle, int w, int h, GLenum internal_format, struct backup_tex_entry *out)
{
	if (!handle || w <= 0 || h <= 0 || !glIsTexture(handle))
		return false;
	if ((size_t)w > (size_t)SIZE_MAX / (size_t)h / 4)
		return false;

	GLint min_filter = GL_LINEAR;
	GLint mag_filter = GL_LINEAR;
	GLint wrap_s = GL_CLAMP_TO_EDGE;
	GLint wrap_t = GL_CLAMP_TO_EDGE;
	glBindTexture(GL_TEXTURE_2D, handle);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag_filter);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap_s);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrap_t);

	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, handle, 0);
	if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		while (glGetError() != GL_NO_ERROR) {}
		return false;
	}

	size_t n = (size_t)w * (size_t)h;
	uint8_t *rgba = xmalloc(n * 4);
	while (glGetError() != GL_NO_ERROR) {}
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	if (glGetError() != GL_NO_ERROR) {
		while (glGetError() != GL_NO_ERROR) {}
		free(rgba);
		return false;
	}

	uint8_t *pixels = rgba;
	if (internal_format == GL_RGB) {
		for (size_t i = 0; i < n; i++) {
			pixels[i * 3 + 0] = rgba[i * 4 + 0];
			pixels[i * 3 + 1] = rgba[i * 4 + 1];
			pixels[i * 3 + 2] = rgba[i * 4 + 2];
		}
	}

	*out = (struct backup_tex_entry){
		.old_handle = handle,
		.w = w,
		.h = h,
		.internal_format = internal_format,
		.min_filter = min_filter,
		.mag_filter = mag_filter,
		.wrap_s = wrap_s,
		.wrap_t = wrap_t,
		.pixels = pixels,
	};
	return true;
}

void gfx_end_draw_target(void);

static SDL_GLContext ensure_current_gl_context(void)
{
	SDL_GLContext current = SDL_GL_GetCurrentContext();
	if (!current && sdl.window && sdl.gl.context) {
		if (SDL_GL_MakeCurrent(sdl.window, sdl.gl.context) == 0)
			current = SDL_GL_GetCurrentContext();
	}
	return current;
}

void gfx_prepare_context_backup(void)
{
	if (!gfx_initialized || !sdl.window)
		return;
	SDL_GLContext current = ensure_current_gl_context();
	if (!current || current != sdl.gl.context || !offscreen_read_fbo || !glIsFramebuffer(offscreen_read_fbo))
		return;

	gfx_release_context_backup();
	gfx_end_draw_target();
	glFlush();

	GLint prev_read_fbo = 0, prev_active_tex = GL_TEXTURE0, prev_tex2d = 0, prev_pack_align = 4;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d);
	glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack_align);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, offscreen_read_fbo);

	if (main_surface.handle) {
		has_backup_main_surface = snapshot_texture_pixels(
			main_surface.handle, main_surface.w, main_surface.h, GL_RGB, &backup_main_surface);
	}

	for (size_t i = 0; i < live_tex_cap; i++) {
		struct tracked_tex_entry *e = &live_tex_table[i];
		if (e->state != 1 || e->skip_backup || !e->handle || e->handle == main_surface.handle)
			continue;
		struct backup_tex_entry snap;
		if (!snapshot_texture_pixels(e->handle, e->w, e->h, e->internal_format, &snap))
			continue;
		if (backup_tex_count == backup_tex_cap) {
			size_t ncap = backup_tex_cap ? backup_tex_cap * 2 : 64;
			backup_tex_list = xrealloc(backup_tex_list, ncap * sizeof(*backup_tex_list));
			backup_tex_cap = ncap;
		}
		backup_tex_list[backup_tex_count++] = snap;
	}

	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	offscreen_read_tex = 0;
	glPixelStorei(GL_PACK_ALIGNMENT, prev_pack_align);
	glBindTexture(GL_TEXTURE_2D, prev_tex2d);
	glActiveTexture(prev_active_tex);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
}
static SDL_Color clear_color = { 0, 0, 0, 255 };
static float frame_rate;
static bool in_gfx_swap = false;
static bool last_swap_succeeded = false;
static bool frame_presented_for_update_view = false;
#ifdef __ANDROID__
static bool wait_vsync = true;

#define EGL_DRAW_SURFACE_ENUM 0x3059
#define ANDROID_ADPF_TARGET_NS 12500000LL
#define ANDROID_FRAME_PERIOD_NS 16666667LL
typedef void *(*pfn_eglGetCurrentDisplay_t)(void);
typedef void *(*pfn_eglGetCurrentSurface_t)(int readdraw);
typedef void *(*pfn_eglGetProcAddress_t)(const char *procname);
typedef unsigned int (*pfn_eglPresentationTimeANDROID_t)(void *dpy, void *surf, int64_t time_ns);

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
typedef APerformanceHintManager *(*pfn_adpf_get_manager_t)(void);
typedef APerformanceHintSession *(*pfn_adpf_create_session_t)(
	APerformanceHintManager *mgr, const int32_t *tids, size_t size, int64_t target_ns);
typedef int64_t (*pfn_adpf_get_pref_rate_t)(APerformanceHintManager *mgr);
typedef int (*pfn_adpf_report_work_t)(APerformanceHintSession *sess, int64_t actual_ns);
typedef void (*pfn_adpf_close_session_t)(APerformanceHintSession *sess);

static void *s_lib_egl = NULL;
static void *s_lib_android = NULL;
static pfn_eglGetCurrentDisplay_t s_pfn_egl_cur_dpy = NULL;
static pfn_eglGetCurrentSurface_t s_pfn_egl_cur_surf = NULL;
static pfn_eglPresentationTimeANDROID_t s_pfn_egl_pres_time = NULL;

static APerformanceHintSession *s_adpf_session = NULL;
static pfn_adpf_get_manager_t s_pfn_adpf_get_mgr = NULL;
static pfn_adpf_create_session_t s_pfn_adpf_create = NULL;
static pfn_adpf_get_pref_rate_t s_pfn_adpf_get_rate = NULL;
static pfn_adpf_report_work_t s_pfn_adpf_report = NULL;
static pfn_adpf_close_session_t s_pfn_adpf_close = NULL;
static int64_t s_adpf_min_interval_ns = 16000000LL;
static int64_t s_last_adpf_report_ns = 0;

static int64_t s_last_frame_end_ns = 0;
static int64_t s_work_start_ns = 0;
static int64_t s_frame_sleep_ns = 0;
static int64_t s_work_cputime_start_ns = 0;
static bool s_android_in_background = false;
static bool s_adpf_needs_focus_refresh = false;

static inline int64_t android_monotonic_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static inline int64_t android_thread_cputime_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
		return 0;
	return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static void android_apply_render_thread_sched(void)
{
	/* THREAD_PRIORITY_URGENT_DISPLAY (-8) + 50us timer slack for accurate pacing */
	setpriority(PRIO_PROCESS, 0, -8);
	prctl(PR_SET_TIMERSLACK, 50000UL, 0, 0, 0);
}

static void android_reset_pacer_timestamps(void)
{
	s_last_frame_end_ns = 0;
	s_work_start_ns = 0;
	s_frame_sleep_ns = 0;
	s_work_cputime_start_ns = 0;
}

static void android_adpf_close_session(void)
{
	if (s_adpf_session && s_pfn_adpf_close) {
		s_pfn_adpf_close(s_adpf_session);
		s_adpf_session = NULL;
	}
	s_last_adpf_report_ns = 0;
}

static void android_adpf_open_session(void)
{
	if (!s_pfn_adpf_get_mgr || !s_pfn_adpf_create || !s_pfn_adpf_report)
		return;
	android_adpf_close_session();
	APerformanceHintManager *mgr = s_pfn_adpf_get_mgr();
	if (!mgr)
		return;
	if (s_pfn_adpf_get_rate) {
		int64_t rate = s_pfn_adpf_get_rate(mgr);
		if (rate > s_adpf_min_interval_ns && rate <= 250000000LL)
			s_adpf_min_interval_ns = rate;
	}
	int32_t tid = (int32_t)gettid();
	s_adpf_session = s_pfn_adpf_create(mgr, &tid, 1, ANDROID_ADPF_TARGET_NS);
}

static void android_perf_pacing_init(void)
{
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "1");
	android_apply_render_thread_sched();

	if (!s_lib_egl)
		s_lib_egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
	if (s_lib_egl) {
		s_pfn_egl_cur_dpy = (pfn_eglGetCurrentDisplay_t)dlsym(s_lib_egl, "eglGetCurrentDisplay");
		s_pfn_egl_cur_surf = (pfn_eglGetCurrentSurface_t)dlsym(s_lib_egl, "eglGetCurrentSurface");
		pfn_eglGetProcAddress_t get_proc =
			(pfn_eglGetProcAddress_t)dlsym(s_lib_egl, "eglGetProcAddress");
		if (get_proc) {
			s_pfn_egl_pres_time = (pfn_eglPresentationTimeANDROID_t)
				get_proc("eglPresentationTimeANDROID");
		}
		if (!s_pfn_egl_pres_time) {
			s_pfn_egl_pres_time = (pfn_eglPresentationTimeANDROID_t)
				dlsym(s_lib_egl, "eglPresentationTimeANDROID");
		}
	}

	if (!s_lib_android)
		s_lib_android = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
	if (s_lib_android) {
		s_pfn_adpf_get_mgr =
			(pfn_adpf_get_manager_t)dlsym(s_lib_android, "APerformanceHint_getManager");
		s_pfn_adpf_create =
			(pfn_adpf_create_session_t)dlsym(s_lib_android, "APerformanceHint_createSession");
		s_pfn_adpf_get_rate =
			(pfn_adpf_get_pref_rate_t)dlsym(s_lib_android, "APerformanceHint_getPreferredUpdateRateNanos");
		s_pfn_adpf_report =
			(pfn_adpf_report_work_t)dlsym(s_lib_android, "APerformanceHint_reportActualWorkDuration");
		s_pfn_adpf_close =
			(pfn_adpf_close_session_t)dlsym(s_lib_android, "APerformanceHint_closeSession");
		android_adpf_open_session();
	}
}

static void android_perf_pacing_fini(void)
{
	android_adpf_close_session();
	android_reset_pacer_timestamps();
}
#elif defined(ALICEHUB_IOS) || defined(__SWITCH__)
static bool wait_vsync = true;
#else
static bool wait_vsync = false;
#endif

static GLchar *read_shader_file(const char *path)
{
	GLchar *source = SDL_LoadFile(path, NULL);
	if (!source) {
		char full_path[PATH_MAX];
		snprintf(full_path, PATH_MAX, XSYS4_DATA_DIR "/%s", path);
		source = SDL_LoadFile(full_path, NULL);
		if (!source)
			ERROR("Failed to load shader file %s", full_path, strerror(errno));
	}
	return source;
}

GLuint gfx_load_shader_file(const char *path, GLenum type, const char *defines)
{
	GLint shader_compiled;
	GLuint shader;
	const GLchar *source[3] = {
		glsl_preamble,
		defines ? defines : "",
		read_shader_file(path)
	};
	shader = glCreateShader(type);
	glShaderSource(shader, 3, source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_compiled);
	if (!shader_compiled) {
		GLint len;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		char *infolog = xmalloc(len + 1);
		glGetShaderInfoLog(shader, len, NULL, infolog);
		ERROR("Failed to compile shader %s: %s", path, infolog);
	}
	SDL_free((char*)source[2]);
	return shader;
}

void gfx_load_shader(struct shader *dst, const char *vertex_shader_path, const char *fragment_shader_path)
{
	GLuint program = glCreateProgram();
	GLuint vertex_shader = gfx_load_shader_file(vertex_shader_path, GL_VERTEX_SHADER, NULL);
	GLuint fragment_shader = gfx_load_shader_file(fragment_shader_path, GL_FRAGMENT_SHADER, NULL);

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	glDetachShader(program, vertex_shader);
	glDetachShader(program, fragment_shader);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	GLint link_success;
	glGetProgramiv(program, GL_LINK_STATUS, &link_success);
	if (!link_success) {
		GLint len;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
		char *infolog = xcalloc(len + 1, 1);
		glGetProgramInfoLog(program, len, NULL, infolog);
		ERROR("Failed to link shader %s, %s: %s", vertex_shader_path, fragment_shader_path, infolog);
	}

	dst->program = program;
	dst->world_transform = glGetUniformLocation(program, "world_transform");
	dst->view_transform = glGetUniformLocation(program, "view_transform");
	dst->texture = glGetUniformLocation(program, "tex");
	dst->vertex_pos = glGetAttribLocation(program, "vertex_pos");
	dst->vertex_uv = glGetAttribLocation(program, "vertex_uv");
	dst->prepare = NULL;
}

#if defined(USE_GLES) && defined(__linux__)
static void GL_APIENTRY gl_diagnostic_message(GLenum source, GLenum type, GLuint id,
		GLenum severity, GLsizei length, const GLchar *message, const void *user)
{
	(void)user;
	if (type == GL_DEBUG_TYPE_ERROR_KHR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR)
		WARNING("GLES debug message (source=0x%x type=0x%x id=%u severity=0x%x): %.*s",
			source, type, id, severity, length, message);
}

static bool gl_diagnostics_requested(void)
{
#ifdef __ANDROID__
	char value[PROP_VALUE_MAX] = {0};
	__system_property_get("log.tag.AliceHubGLES", value);
	return !strcmp(value, "DEBUG") || !strcmp(value, "VERBOSE");
#else
	const char *value = SDL_getenv("ALICEHUB_GLES_DEBUG");
	return value && !strcmp(value, "1");
#endif
}

static void gl_enable_diagnostics(void)
{
	if (!gl_diagnostics_requested())
		return;

	if (!SDL_GL_ExtensionSupported("GL_KHR_debug")) {
		WARNING("GLES diagnostics: GL_KHR_debug is unavailable");
		return;
	}
	PFNGLDEBUGMESSAGECALLBACKKHRPROC callback = (PFNGLDEBUGMESSAGECALLBACKKHRPROC)
		SDL_GL_GetProcAddress("glDebugMessageCallbackKHR");
	if (!callback) {
		WARNING("GLES diagnostics: glDebugMessageCallbackKHR is unavailable");
		return;
	}
	callback(gl_diagnostic_message, NULL);
	glEnable(GL_DEBUG_OUTPUT_KHR);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
}
#endif

static int gl_initialize(void)
{
#ifdef USE_GLES
#ifdef __ANDROID__
	__android_log_print(ANDROID_LOG_INFO, "libsys4", "GLES driver: vendor=%s renderer=%s version=%s",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
#else
	NOTICE("GLES driver: vendor=%s renderer=%s version=%s", glGetString(GL_VENDOR),
		glGetString(GL_RENDERER), glGetString(GL_VERSION));
#endif
#endif
#if defined(USE_GLES) && defined(__linux__)
	gl_enable_diagnostics();
#endif
	gfx_load_shader(&default_shader, "shaders/render.v.glsl", "shaders/render.f.glsl");

	const struct gfx_vertex vertex_data[] = {
		//  x,   y,   z,   w,   u,   v
		{ 0.f, 0.f, 0.f, 1.f, 0.f, 0.f },
		{ 1.f, 0.f, 0.f, 1.f, 1.f, 0.f },
		{ 0.f, 1.f, 0.f, 1.f, 0.f, 1.f },
		{ 1.f, 1.f, 0.f, 1.f, 1.f, 1.f }
	};

	const GLuint rect_index_data[] = { 0, 1, 3, 2 };
	const GLuint line_index_data[] = { 0, 3 };

	glGenVertexArrays(1, &sdl.gl.vao);
	glBindVertexArray(sdl.gl.vao);

	glGenBuffers(1, &sdl.gl.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
	glGenBuffers(1, &sdl.gl.quad_vbo);

	glGenBuffers(1, &sdl.gl.rect_ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.rect_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * sizeof(GLuint), rect_index_data, GL_STATIC_DRAW);

	glGenBuffers(1, &sdl.gl.line_ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.line_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 2 * sizeof(GLuint), line_index_data, GL_STATIC_DRAW);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glBindVertexArray(0);

	glGenFramebuffers(1, &offscreen_draw_fbo);
	glGenFramebuffers(1, &offscreen_read_fbo);

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

	return 0;
}

static void set_window_title(void)
{
	char title[1024] = { [1023] = 0 };
	char *game_name = sjis_filename2utf(config.game_name, 0);
	snprintf(title, 1023, "%s - XSystem4", game_name);
	free(game_name);

	SDL_SetWindowTitle(sdl.window, title);
}

#ifdef __ANDROID__
static unsigned resume_trace_swaps;
static unsigned resume_trace_seq;

static bool resume_trace_enabled(void)
{
	char value[PROP_VALUE_MAX];
	__system_property_get("log.tag.AliceHubResume", value);
	return !strcmp(value, "DEBUG") || !strcmp(value, "VERBOSE");
}

static void trace_resume_gl_state(const char *phase)
{
	// Render-thread only. Do not read pixels, wait for the GPU, clear GL/SDL
	// errors, make a context current, or attempt any recovery from this hook.
	SDL_GLContext current = SDL_GL_GetCurrentContext();
	GLint framebuffer = -1;
	bool ready = gfx_initialized && current;
	if (ready)
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
	const GLubyte *renderer = ready ? glGetString(GL_RENDERER) : NULL;
	__android_log_print(ANDROID_LOG_DEBUG, "AliceHubResume",
		"render.%s seq=%u ticks=%u context=%d retained=%d texture=%d program=%d fbo=%d bound=%d viewport=%dx%d renderer=%s sdl_error_may_be_stale=%s",
		phase, resume_trace_seq, SDL_GetTicks(), current != NULL,
		current && current == sdl.gl.context,
		ready && glIsTexture(view->handle), ready && glIsProgram(default_shader.program),
		ready && glIsFramebuffer(main_surface_fb), framebuffer,
		sdl.viewport.w, sdl.viewport.h,
		renderer ? (const char *)renderer : "none", SDL_GetError());
}
#endif

void gfx_trace_resume(const char *phase)
{
#ifdef __ANDROID__
	if (!resume_trace_enabled()) {
		resume_trace_swaps = 0;
		return;
	}
	resume_trace_seq++;
	resume_trace_swaps = 2; // Bounded: no per-frame logging after this window.
	trace_resume_gl_state(phase);
#else
	(void)phase;
#endif
}


/*
 * Reduce window size if the display resolution is smaller than the game
 * resolution.
 */
static void init_window_size(void)
{
#if !defined(__ANDROID__) && !defined(ALICEHUB_IOS)
	int display_id = SDL_GetWindowDisplayIndex(sdl.window);
	if (display_id < 0)
		return;

	SDL_Rect bounds;
	if (SDL_GetDisplayUsableBounds(display_id, &bounds) < 0)
		return;

	int w, h;
	SDL_GetWindowSize(sdl.window, &w, &h);
	if (bounds.w > w && bounds.h > h)
		return;

	int border_top, border_bot;
	if (SDL_GetWindowBordersSize(sdl.window, &border_top, NULL, &border_bot, NULL) < 0)
		return;

	int scaled_h = bounds.h - (border_top + border_bot);
	int scaled_w = ((float)scaled_h / h) * w;
	int scaled_x = bounds.w / 2 - scaled_w / 2;

	SDL_SetWindowSize(sdl.window, scaled_w, scaled_h);
	SDL_SetWindowPosition(sdl.window, bounds.x + scaled_x, bounds.y + border_top);
#endif
}

static void gfx_create_window_and_context(uint32_t window_flags)
{
#if defined(USE_GLES) && defined(__linux__)
	int context_flags = 0;
	bool debug_context = false;
	if (gl_diagnostics_requested()) {
		if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &context_flags) == 0 &&
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, context_flags | SDL_GL_CONTEXT_DEBUG_FLAG) == 0) {
			debug_context = true;
		} else {
			WARNING("GLES diagnostics: could not request a debug context: %s", SDL_GetError());
			SDL_ClearError();
		}
	}
#endif

	for (;;) {
		sdl.window = SDL_CreateWindow("XSystem4", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
				config.view_width, config.view_height, window_flags);
		if (sdl.window) {
			set_window_title();
			sdl.gl.context = SDL_GL_CreateContext(sdl.window);
			if (sdl.gl.context)
				return;
		}
#if defined(USE_GLES) && defined(__linux__)
		if (debug_context) {
			WARNING("GLES diagnostics: debug context creation failed; retrying without the debug flag: %s",
				SDL_GetError());
			if (sdl.window)
				SDL_DestroyWindow(sdl.window);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, context_flags & ~SDL_GL_CONTEXT_DEBUG_FLAG);
			SDL_ClearError();
			debug_context = false;
			continue;
		}
#endif
		if (!sdl.window)
			ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
		ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
	}
}

int gfx_init(void)
{
	if (gfx_initialized)
		return true;

#ifdef ALICEHUB_IOS
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
#endif

	uint32_t flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
	if (config.joypad)
		flags |= SDL_INIT_GAMECONTROLLER;
	if (SDL_Init(flags) < 0)
		ERROR("SDL_Init failed: %s", SDL_GetError());

#ifdef USE_GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

	sdl.format = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);
	uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#ifdef ALICEHUB_IOS
	window_flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
	gfx_create_window_and_context(window_flags);
#ifdef ALICEHUB_IOS
	alicehub_ios_prepare_window(sdl.window);
#endif

#if !defined(USE_GLES) && !defined(__APPLE__)
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY)
		ERROR("glewInit failed");
#endif

	SDL_GL_SetSwapInterval(wait_vsync ? 1 : 0);
#ifdef __ANDROID__
	android_perf_pacing_init();
#endif
	gl_initialize();
	gfx_draw_init();
	gfx_set_window_logical_size(config.view_width, config.view_height);
	init_window_size();
#if !defined(__ANDROID__) && !defined(ALICEHUB_IOS)
	atexit(gfx_fini);
#endif
	gfx_clear();
	icon_init();
	gfx_initialized = true;
	return 0;
}

void gfx_android_fini(void)
{
#ifdef __ANDROID__
	android_perf_pacing_fini();
#endif
}

#if defined(ALICEHUB_IOS)
void sact_end_session(void);
void sprite_end_session(void);
void scene_end_session(void);
void parts_end_session(void);
void effect_end_session(void);
void sengoku_rance_font_end_session(void);
void movie_end_session(void);
void system_service_end_session(void);
#endif

void gfx_fini(void)
{
#ifdef ALICEHUB_IOS
	bool quit_sdl = mixer_fini();
	if (!sdl.gl.context) {
		movie_end_session();
		sengoku_rance_font_end_session();
		system_service_end_session();
		gfx_font_fini();
		goto close_sdl;
	}
	effect_end_session();
	parts_end_session();
	sact_end_session();
	scene_end_session();
	sprite_end_session();
	movie_end_session();
	sengoku_rance_font_end_session();
	system_service_end_session();
	gfx_font_fini();
#endif
#ifdef __ANDROID__
	android_perf_pacing_fini();
#endif
	if (offscreen_draw_fbo)
		glDeleteFramebuffers(1, &offscreen_draw_fbo);
	if (offscreen_read_fbo)
		glDeleteFramebuffers(1, &offscreen_read_fbo);
	if (main_surface_fb)
		glDeleteFramebuffers(1, &main_surface_fb);
	offscreen_draw_fbo = 0;
	offscreen_read_fbo = 0;
	main_surface_fb = 0;
	offscreen_draw_tex = 0;
	offscreen_read_tex = 0;
	pinned_draw_tex = 0;
	pinned_draw_bound = false;
	gfx_delete_texture(&main_surface);
	glDeleteProgram(default_shader.program);
	gfx_release_context_backup();
	clear_live_textures();
	free(live_tex_table);
	live_tex_table = NULL;
	live_tex_cap = 0;
	free(remap_table);
	remap_table = NULL;
	remap_cap = 0;
	remap_count = 0;
	free(reset_callbacks);
	reset_callbacks = NULL;
	nr_reset_callbacks = 0;
#ifdef ALICEHUB_IOS
	SDL_GL_DeleteContext(sdl.gl.context);
close_sdl:
#endif
	icon_fini();
	SDL_DestroyWindow(sdl.window);
	SDL_FreeFormat(sdl.format);
#ifdef ALICEHUB_IOS
	memset(&sdl, 0, sizeof(sdl));
	memset(&default_shader, 0, sizeof(default_shader));
	memset(&main_surface, 0, sizeof(main_surface));
	view = &main_surface;
	gfx_initialized = false;
	gfx_ctx_gen++;
	frame_presented_for_update_view = false;
	last_swap_succeeded = false;
	in_gfx_swap = false;
	wait_vsync = true;
	if (!quit_sdl)
		return;
#endif
	SDL_Quit();
	sdl = (struct sdl_private){0};
	main_surface = (struct texture){0};
	main_surface_fb = 0;
	view = &main_surface;
	default_shader = (struct shader){0};
	clear_color = (SDL_Color){0, 0, 0, 255};
	gfx_set_view_offset(0, 0);
	gfx_initialized = false;
}

Texture *gfx_main_surface(void)
{
	return &main_surface;
}

static void main_surface_init(int w, int h)
{
	gfx_delete_texture(&main_surface);

	glGenTextures(1, &main_surface.handle);
	glBindTexture(GL_TEXTURE_2D, main_surface.handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	main_surface.w = w;
	main_surface.h = h;
	main_surface.opaque = true;
	main_surface.ctx_gen = gfx_ctx_gen;
	track_live_texture(main_surface.handle, w, h, GL_RGB, false);

	if (main_surface_fb)
		glDeleteFramebuffers(1, &main_surface_fb);
	glGenFramebuffers(1, &main_surface_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_surface.handle, 0);
	pinned_draw_bound = false;
}

void gfx_set_window_logical_size(int w, int h)
{
	sdl.w = w;
	sdl.h = h;
	world_view_transform[0][0] = 2.0 / w;
	world_view_transform[1][1] = 2.0 / h;
	gfx_update_screen_scale();
	main_surface_init(w, h);
}

bool gfx_set_fullscreen(bool enable)
{
	uint32_t flags = enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
	if (SDL_SetWindowFullscreen(sdl.window, flags) < 0) {
		WARNING("Failed to set fullscreen mode: %s", SDL_GetError());
		return false;
	}
	return true;
}

bool gfx_is_fullscreen(void)
{
	return SDL_GetWindowFlags(sdl.window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void gfx_toggle_fullscreen(void)
{
	gfx_set_fullscreen(!gfx_is_fullscreen());
}

void gfx_update_screen_scale(void)
{
	int display_w = 0, display_h = 0;
#ifdef ALICEHUB_IOS
	SDL_GetWindowSize(sdl.window, &display_w, &display_h);
#else
	SDL_GL_GetDrawableSize(sdl.window, &display_w, &display_h);
#endif
	if (display_w <= 0 || display_h <= 0 || sdl.w <= 0 || sdl.h <= 0)
		return;

	if (display_w * sdl.h == display_h * sdl.w) {
		// The aspect ratios are the same, just scale appropriately.
		sdl.viewport.x = 0;
		sdl.viewport.y = 0;
		sdl.viewport.w = display_w;
		sdl.viewport.h = display_h;
		return;
	}

	double logical_aspect = (double)sdl.w / sdl.h;
	double display_aspect = (double)display_w / display_h;
	if (logical_aspect > display_aspect) {
		// letterbox
		sdl.viewport.w = display_w;
		sdl.viewport.h = sdl.h * display_w / sdl.w;
		sdl.viewport.x = 0;
		sdl.viewport.y = (display_h - sdl.viewport.h) / 2;
	} else {
		// pillarbox (side bars)
		sdl.viewport.w = sdl.w * display_h / sdl.h;
		sdl.viewport.h = display_h;
		sdl.viewport.x = (display_w - sdl.viewport.w) / 2;
		sdl.viewport.y = 0;
	}
}

void gfx_set_wait_vsync(bool wait)
{
	wait_vsync = wait;
	if (gfx_initialized)
		SDL_GL_SetSwapInterval(wait ? 1 : 0);
	gfx_reset_frame_pacer();
}

void gfx_reset_frame_pacer(void)
{
#ifdef __ANDROID__
	android_reset_pacer_timestamps();
	if (gfx_initialized)
		SDL_GL_SetSwapInterval(wait_vsync ? 1 : 0);
	android_apply_render_thread_sched();
	if (!s_android_in_background && !s_adpf_session)
		android_adpf_open_session();
#endif
}

void gfx_android_on_background(void)
{
#ifdef __ANDROID__
	gfx_prepare_context_backup();
	s_android_in_background = true;
	s_adpf_needs_focus_refresh = true;
	android_adpf_close_session();
	android_reset_pacer_timestamps();
#endif
}

void gfx_android_on_foreground(void)
{
#ifdef __ANDROID__
	bool should_reopen = s_android_in_background || !s_adpf_session || s_adpf_needs_focus_refresh;
	s_android_in_background = false;
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	if (gfx_initialized)
		SDL_GL_SetSwapInterval(wait_vsync ? 1 : 0);
	android_apply_render_thread_sched();
	if (should_reopen) {
		android_adpf_open_session();
		s_adpf_needs_focus_refresh = false;
	}
	android_reset_pacer_timestamps();
	file_note_fs_mutation();
#endif
}

#ifdef __ANDROID__
static int s_delay_will_bg_handled = 0;
static int s_delay_did_fg_handled = 0;
#endif

bool gfx_android_consume_lifecycle_event(Uint32 event_type)
{
#ifdef __ANDROID__
	if (event_type == SDL_APP_WILLENTERBACKGROUND && s_delay_will_bg_handled > 0) {
		s_delay_will_bg_handled--;
		return true;
	}
	if (event_type == SDL_APP_DIDENTERFOREGROUND && s_delay_did_fg_handled > 0) {
		s_delay_did_fg_handled--;
		return true;
	}
#else
	(void)event_type;
#endif
	return false;
}

void gfx_android_delay(Uint32 ms)
{
#ifdef __ANDROID__
	int64_t t0 = android_monotonic_ns();
	if (ms > 16 && sys_is_sdl_main_thread()) {
		Uint32 start = SDL_GetTicks();
		for (;;) {
			Uint32 elapsed = SDL_GetTicks() - start;
			if (elapsed >= ms)
				break;
			Uint32 remaining = ms - elapsed;
			SDL_Delay(remaining > 16 ? 16 : remaining);
			SDL_PumpEvents();
			SDL_Event evs[64];
			int n = SDL_PeepEvents(evs, 64, SDL_PEEKEVENT,
			                       SDL_APP_WILLENTERBACKGROUND,
			                       SDL_APP_DIDENTERFOREGROUND);
			int will_idx = 0;
			int did_idx = 0;
			for (int i = 0; i < n; i++) {
				if (evs[i].type == SDL_APP_WILLENTERBACKGROUND) {
					if (will_idx >= s_delay_will_bg_handled) {
						gfx_android_on_background();
						s_delay_will_bg_handled = will_idx + 1;
					}
					will_idx++;
				} else if (evs[i].type == SDL_APP_DIDENTERFOREGROUND) {
					if (did_idx >= s_delay_did_fg_handled) {
						gfx_android_on_foreground();
						gfx_restore_window();
						s_delay_did_fg_handled = did_idx + 1;
					}
					did_idx++;
				}
			}
		}
	} else {
		SDL_Delay(ms);
	}
	int64_t t1 = android_monotonic_ns();
	if (t0 > 0 && t1 > t0)
		s_frame_sleep_ns += (t1 - t0);
#else
	SDL_Delay(ms);
#endif
}

void gfx_set_clear_color(int r, int g, int b, int a)
{
	clear_color.r = max(0, min(255, r));
	clear_color.g = max(0, min(255, g));
	clear_color.b = max(0, min(255, b));
	clear_color.a = max(0, min(255, a));
}

static inline bool texture_targets_view(const Texture *t)
{
	if (!t)
		return false;
	if (t == view || t == &main_surface)
		return true;
	if (t->handle && ((view && t->handle == view->handle) || t->handle == main_surface.handle))
		return true;
	return false;
}

void gfx_mark_frame_presented(void)
{
	frame_presented_for_update_view = last_swap_succeeded;
}

void gfx_clear_frame_presented(void)
{
	frame_presented_for_update_view = false;
}

bool gfx_consume_frame_presented(void)
{
	bool was = frame_presented_for_update_view;
	frame_presented_for_update_view = false;
	return was;
}

void gfx_clear(void)
{
	if (!in_gfx_swap)
		frame_presented_for_update_view = false;
	glClearColor(clear_color.r / 255.f, clear_color.g / 255.f, clear_color.b / 255.f, clear_color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;

void gfx_set_view_offset(int x, int y)
{
	frame_presented_for_update_view = false;
	glm_mat4_identity(mw_transform);
	if (!x && !y)
		return;
	vec3 off = { (float)x / config.view_width, (float)y / config.view_height, 0 };
	glm_translate(mw_transform, off);
}

float gfx_get_frame_rate(void)
{
	return frame_rate;
}

static void gfx_update_frame_rate_counter(void)
{
	static uint64_t timestamp;
	static int frame_count;

	frame_count++;
	uint64_t current_time = SDL_GetTicks64();
	if (current_time > timestamp + 1000) {
		frame_rate = frame_count * 1000.f / (current_time - timestamp);
		timestamp = current_time;
		frame_count = 0;
	}
}

static bool is_app_in_background(void)
{
#ifdef __ANDROID__
	return s_android_in_background;
#else
	return false;
#endif
}

static void gfx_refresh_viewport_if_needed(void)
{
	static int last_display_w = 0, last_display_h = 0;
	static int last_logical_w = 0, last_logical_h = 0;
	if (!sdl.window)
		return;
	int display_w = 0, display_h = 0;
#ifdef ALICEHUB_IOS
	SDL_GetWindowSize(sdl.window, &display_w, &display_h);
#else
	SDL_GL_GetDrawableSize(sdl.window, &display_w, &display_h);
#endif
	if (display_w <= 0 || display_h <= 0 || sdl.w <= 0 || sdl.h <= 0)
		return;
	if (sdl.viewport.w <= 0 || sdl.viewport.h <= 0 ||
			display_w != last_display_w || display_h != last_display_h ||
			sdl.w != last_logical_w || sdl.h != last_logical_h) {
		gfx_update_screen_scale();
		if (sdl.viewport.w > 0 && sdl.viewport.h > 0) {
			last_display_w = display_w;
			last_display_h = display_h;
			last_logical_w = sdl.w;
			last_logical_h = sdl.h;
		}
	}
}

void gfx_swap(void)
{
	last_swap_succeeded = false;
#ifdef __ANDROID__
	bool trace_swap = resume_trace_swaps > 0;
	if (trace_swap) {
		resume_trace_swaps--;
		trace_resume_gl_state("swap.begin");
	}
#endif
	if (!gfx_initialized)
		return;
	gfx_recover_gl_if_needed();
	if (!glIsProgram(default_shader.program) ||
	    !glIsFramebuffer(main_surface_fb) || !glIsTexture(main_surface.handle))
		return;
	if ((backup_tex_list || has_backup_main_surface) && !is_app_in_background())
		gfx_release_context_backup();
	if (!view)
		view = &main_surface;
	else if (view != &main_surface && !gfx_remap_texture(view))
		view = &main_surface;
	gfx_refresh_viewport_if_needed();
	if (sdl.viewport.w <= 0 || sdl.viewport.h <= 0)
		return;
	pinned_draw_tex = 0;
	pinned_draw_bound = false;
#if   defined(ALICEHUB_IOS)
	alicehub_ios_bind_drawable(sdl.window, &sdl.viewport);
#else
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
	glViewport(sdl.viewport.x, sdl.viewport.y, sdl.viewport.w, sdl.viewport.h);
	in_gfx_swap = true;
	gfx_clear();

	static mat4 wv_transform = MAT4(
		2,  0, 0, -1,
		0, -2, 0,  1,
		0,  0, 1,  0,
		0,  0, 0,  1);
	struct gfx_render_job job = {
		.shader = &default_shader,
		.shape = GFX_RECTANGLE,
		.texture = view->handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = view
	};
#ifdef __SWITCH__
	if (switch_menu_surface) {
		int width, height;
		SDL_GL_GetDrawableSize(sdl.window, &width, &height);
		int w = width, h = (int)((int64_t)width * switch_menu_surface->h / switch_menu_surface->w);
		if (h > height) {
			h = height;
			w = (int)((int64_t)height * switch_menu_surface->w / switch_menu_surface->h);
		}
		glViewport((width - w) / 2, (height - h) / 2, w, h);
		static mat4 identity = GLM_MAT4_IDENTITY_INIT;
		job.texture = switch_menu_surface->handle;
		job.world_transform = identity[0];
		job.data = switch_menu_surface;
	}
#endif
	gfx_render(&job);
	in_gfx_swap = false;
	frame_presented_for_update_view = false;
	/* Kick off mobile tile-based GPU rasterization immediately in parallel */
	glFlush();

#ifdef __ANDROID__
	int64_t work_end_ns = android_monotonic_ns();
	if (!s_android_in_background && s_adpf_session && s_pfn_adpf_report &&
	    s_work_start_ns > 0 && work_end_ns > s_work_start_ns) {
		int64_t wall_elapsed_ns = work_end_ns - s_work_start_ns;
		int64_t actual_ns = (wall_elapsed_ns > s_frame_sleep_ns)
			? (wall_elapsed_ns - s_frame_sleep_ns) : 0;
		int64_t cputime_now_ns = android_thread_cputime_ns();
		if (s_work_cputime_start_ns > 0 && cputime_now_ns >= s_work_cputime_start_ns) {
			int64_t cpu_active_ns = cputime_now_ns - s_work_cputime_start_ns;
			int64_t cap_ns = cpu_active_ns + 4500000LL;
			if (actual_ns > cap_ns)
				actual_ns = cap_ns;
		}
		if (actual_ns < 500000LL)
			actual_ns = 500000LL;
		else if (actual_ns > 20000000LL)
			actual_ns = 20000000LL;
		if (actual_ns >= 10500000LL ||
		    (work_end_ns - s_last_adpf_report_ns) >= s_adpf_min_interval_ns) {
			s_pfn_adpf_report(s_adpf_session, actual_ns);
			s_last_adpf_report_ns = work_end_ns;
		}
	}
	s_frame_sleep_ns = 0;

	if (wait_vsync && s_last_frame_end_ns > 0 &&
	    s_pfn_egl_pres_time && s_pfn_egl_cur_dpy && s_pfn_egl_cur_surf) {
		/*
		 * Only hint presentation time on >=105Hz panels (e.g. 120Hz/144Hz that
		 * did not switch to 60Hz). Use +12.0ms so it skips the +8.33ms intermediate
		 * 120Hz VSync while keeping ~4.7ms headroom before SurfaceFlinger's 16.67ms
		 * latch window. On 60Hz/90Hz panels, a future timestamp misses VSync and
		 * halves output to 30fps/45fps.
		 */
		SDL_DisplayMode dm;
		int disp_idx = sdl.window ? SDL_GetWindowDisplayIndex(sdl.window) : -1;
		if (disp_idx >= 0 && SDL_GetCurrentDisplayMode(disp_idx, &dm) == 0 && dm.refresh_rate >= 105) {
			int64_t present_ns = s_last_frame_end_ns + 12000000LL;
			if (present_ns > work_end_ns && (present_ns - work_end_ns) < 25000000LL) {
				void *dpy = s_pfn_egl_cur_dpy();
				void *surf = dpy ? s_pfn_egl_cur_surf(EGL_DRAW_SURFACE_ENUM) : NULL;
				if (dpy && surf)
					s_pfn_egl_pres_time(dpy, surf, present_ns);
			}
		}
	}
#endif

#ifdef __SWITCH__
	if (!switch_menu_surface)
		draw_switch_controls();
#endif
	SDL_GL_SwapWindow(sdl.window);
	last_swap_succeeded = true;
#ifdef __ANDROID__
	if (trace_swap)
		trace_resume_gl_state("swap.end");
#endif

#ifdef __ANDROID__
	int64_t after_swap_ns = android_monotonic_ns();
	if (wait_vsync && after_swap_ns > 0) {
		int64_t target_end_ns = (s_last_frame_end_ns > 0)
			? (s_last_frame_end_ns + ANDROID_FRAME_PERIOD_NS) : 0;
		if (target_end_ns > after_swap_ns + 300000LL &&
		    (target_end_ns - after_swap_ns) < 17000000LL) {
			struct timespec ts = {
				.tv_sec  = (time_t)(target_end_ns / 1000000000LL),
				.tv_nsec = (long)(target_end_ns % 1000000000LL),
			};
			while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
			}
			after_swap_ns = android_monotonic_ns();
		}
		if (target_end_ns > 0 && after_swap_ns >= target_end_ns &&
		    (after_swap_ns - target_end_ns) < 1500000LL)
			s_last_frame_end_ns = target_end_ns;
		else
			s_last_frame_end_ns = after_swap_ns;
	} else {
		s_last_frame_end_ns = 0;
	}
	s_work_start_ns = after_swap_ns;
	s_work_cputime_start_ns = android_thread_cputime_ns();
	s_frame_sleep_ns = 0;
#endif

	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);

	gfx_update_frame_rate_counter();
#ifdef __SWITCH__
	if (switch_menu_surface)
		return;
#endif
	modifier_on_frame();
}

#ifdef __SWITCH__
bool gfx_switch_menu_present(void *context, SDL_Surface *panel)
{
	(void)context;
	if (!gfx_initialized || !ensure_current_gl_context())
		return false;
	Texture texture = {0};
	gfx_init_texture_with_pixels(&texture, panel->w, panel->h, panel->pixels);
	if (!texture.handle)
		return false;
	texture.opaque = true;
	switch_menu_surface = &texture;
	gfx_swap();
	switch_menu_surface = NULL;
	gfx_delete_texture(&texture);
	return true;
}

bool gfx_switch_menu_capture(void *context, const char *path)
{
	(void)context;
	if (!gfx_initialized || !ensure_current_gl_context())
		return false;
	Texture *texture = view ? view : &main_surface;
	return gfx_save_texture(texture, path, ALCG_PNG) != 0;
}
#endif

// SDL can recreate Android's EGL window surface while retaining the GL context
// and our last composited frame. RESTORED does not imply EXPOSED in SDL2, so a
// script waiting for input may otherwise leave the new surface blank forever.
// Called on the SDL/render thread after its pause loop restores the context;
// never recreate GL resources or present from Activity/JNI callbacks.
static bool gfx_needs_gl_reset(void)
{
	SDL_GLContext current = SDL_GL_GetCurrentContext();
	if (!current)
		return false;
	if (current != sdl.gl.context)
		return true;
	if (!default_shader.program || !glIsProgram(default_shader.program))
		return true;
	if (!sdl.gl.vao || !glIsVertexArray(sdl.gl.vao))
		return true;
	if (!main_surface_fb || !glIsFramebuffer(main_surface_fb))
		return true;
	if (!main_surface.handle || !glIsTexture(main_surface.handle))
		return true;
	return false;
}

static void upload_backup_entry(const struct backup_tex_entry *b, GLuint handle)
{
	glBindTexture(GL_TEXTURE_2D, handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, b->min_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, b->mag_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, b->wrap_s);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, b->wrap_t);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	GLenum fmt = b->internal_format == GL_RGB ? GL_RGB : GL_RGBA;
	glTexImage2D(GL_TEXTURE_2D, 0, fmt, b->w, b->h, 0, fmt, GL_UNSIGNED_BYTE, b->pixels);
	if (b->min_filter == GL_LINEAR_MIPMAP_LINEAR || b->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
	    b->min_filter == GL_NEAREST_MIPMAP_LINEAR || b->min_filter == GL_NEAREST_MIPMAP_NEAREST) {
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
}

bool gfx_release_context_backup_if_ready(void)
{
	if (!backup_tex_list && !has_backup_main_surface)
		return true;
	if (!gfx_initialized || !sdl.window || is_app_in_background())
		return false;
	SDL_GLContext current = ensure_current_gl_context();
	if (!current)
		return false;
	if (gfx_needs_gl_reset()) {
		gfx_recover_gl_if_needed();
		if (gfx_needs_gl_reset())
			return false;
	}
	gfx_release_context_backup();
	return true;
}

bool gfx_recover_gl_if_needed(void)
{
	if (!gfx_initialized || !sdl.window || gfx_recovering)
		return false;
	SDL_GLContext current = ensure_current_gl_context();
	if (!current || !gfx_needs_gl_reset())
		return false;

	gfx_recovering = true;
	sdl.gl.context = current;
	for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {}

	uint32_t prev_gen = gfx_ctx_gen;
	gfx_ctx_gen = (gfx_ctx_gen + 1) ? (gfx_ctx_gen + 1) : 1;
	clear_live_textures();

	memset(&default_shader, 0, sizeof(default_shader));
	sdl.gl.vao = 0;
	sdl.gl.vbo = 0;
	sdl.gl.quad_vbo = 0;
	sdl.gl.rect_ibo = 0;
	sdl.gl.line_ibo = 0;
	offscreen_draw_fbo = 0;
	offscreen_read_fbo = 0;
	offscreen_draw_tex = 0;
	offscreen_read_tex = 0;
	pinned_draw_tex = 0;
	pinned_draw_bound = false;
	main_surface_fb = 0;

	GLuint old_main_handle = main_surface.handle;
	int mw = main_surface.w > 0 ? main_surface.w : (sdl.w > 0 ? sdl.w : config.view_width);
	int mh = main_surface.h > 0 ? main_surface.h : (sdl.h > 0 ? sdl.h : config.view_height);
	main_surface.handle = 0;
	main_surface.ctx_gen = 0;

	gl_initialize();
	if (mw > 0 && mh > 0) {
		main_surface_init(mw, mh);
		if (has_backup_main_surface && backup_main_surface.pixels &&
		    backup_main_surface.w == mw && backup_main_surface.h == mh) {
			upload_backup_entry(&backup_main_surface, main_surface.handle);
		} else {
			glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
			glClearColor(0.f, 0.f, 0.f, 1.f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
	}

	if (old_main_handle && main_surface.handle)
		remap_insert(prev_gen, old_main_handle, gfx_ctx_gen, main_surface.handle);
	for (size_t i = 0; i < backup_tex_count; i++) {
		const struct backup_tex_entry *b = &backup_tex_list[i];
		GLuint new_handle = 0;
		glGenTextures(1, &new_handle);
		upload_backup_entry(b, new_handle);
		track_live_texture(new_handle, b->w, b->h, b->internal_format, false);
		remap_insert(prev_gen, b->old_handle, gfx_ctx_gen, new_handle);
	}
	// Advance pre-existing multi-reset remap chains using the new mappings.
	for (size_t i = 0; i < remap_cap; i++) {
		if (remap_table[i].state != 1 || remap_table[i].cur_gen != prev_gen)
			continue;
		struct remap_tex_entry *step = remap_find(prev_gen, remap_table[i].cur_handle);
		remap_table[i].cur_gen = gfx_ctx_gen;
		remap_table[i].cur_handle = (step && step->cur_gen == gfx_ctx_gen) ? step->cur_handle : 0;
	}

	if (!view)
		view = &main_surface;
	else if (view != &main_surface && !gfx_remap_texture(view))
		view = &main_surface;

	gfx_release_context_backup();
	gfx_update_screen_scale();
	for (size_t i = 0; i < nr_reset_callbacks; i++)
		reset_callbacks[i].cb(reset_callbacks[i].user);

	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	if (sdl.w > 0 && sdl.h > 0)
		glViewport(0, 0, sdl.w, sdl.h);
#ifdef __ANDROID__
	SDL_GL_SetSwapInterval(wait_vsync ? 1 : 0);
#endif
	frame_presented_for_update_view = false;
	last_swap_succeeded = false;
	gfx_recovering = false;
	return true;
}

void gfx_restore_window(void)
{
	if (!gfx_initialized)
		return;
	gfx_recover_gl_if_needed();
	gfx_update_screen_scale();
	if (!SDL_GL_GetCurrentContext() || sdl.viewport.w <= 0 || sdl.viewport.h <= 0)
		return;
	gfx_swap();
}

void gfx_set_view(struct texture *t)
{
	if (t)
		gfx_remap_texture(t);
	view = (t && t->handle) ? t : &main_surface;
	frame_presented_for_update_view = false;
}

void gfx_reset_view(void)
{
	view = &main_surface;
	frame_presented_for_update_view = false;
}

/*
 * Set up mandatory shader arguments.
 */
void gfx_prepare_job(struct gfx_render_job *job)
{
	glUseProgram(job->shader->program);

	glUniformMatrix4fv(job->shader->world_transform, 1, GL_FALSE, job->world_transform);
	glUniformMatrix4fv(job->shader->view_transform, 1, GL_FALSE, job->view_transform);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, job->texture);
	glUniform1i(job->shader->texture, 0);

	if (job->shader->prepare)
		job->shader->prepare(job, job->data);
}

/*
 * Execute a render job set up previously with gfx_prepare_job.
 */
void gfx_run_job(struct gfx_render_job *job)
{
	GLint pos = job->shader->vertex_pos;
	GLint uv = job->shader->vertex_uv;
	glBindVertexArray(sdl.gl.vao);
	glBindBuffer(GL_ARRAY_BUFFER, job->shape == GFX_QUADRILATERAL ? sdl.gl.quad_vbo : sdl.gl.vbo);
	if (pos >= 0) {
		glEnableVertexAttribArray(pos);
		glVertexAttribPointer(pos, 4, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), NULL);
	}
	if (uv >= 0) {
		glEnableVertexAttribArray(uv);
		glVertexAttribPointer(uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), (void*)offsetof(struct gfx_vertex, u));
	}

	switch (job->shape) {
	case GFX_RECTANGLE:
	case GFX_QUADRILATERAL:
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.rect_ibo);
		glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);
		break;
	case GFX_LINE:
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.line_ibo);
		glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, NULL);
		break;
	}

	if (pos >= 0)
		glDisableVertexAttribArray(pos);
	if (uv >= 0)
		glDisableVertexAttribArray(uv);
	glBindVertexArray(0);
	glUseProgram(0);
}

void gfx_render(struct gfx_render_job *job)
{
	if (!in_gfx_swap)
		frame_presented_for_update_view = false;
	if (!job->shader) {
		job->shader = &default_shader;
		gfx_prepare_job(job);
		gfx_run_job(job);
		job->shader = NULL;
	} else {
		gfx_prepare_job(job);
		gfx_run_job(job);
	}
}

void _gfx_render_texture(struct shader *s, struct texture *t, Rectangle *r, void *data)
{
	gfx_remap_texture(t);
	if (!t || !t->handle) {
		WARNING("Attempted to render uninitialized texture");
		return;
	}

	mat4 world_transform = WORLD_TRANSFORM(t->w, t->h, r ? r->x : 0, r ? r->y : 0);

	struct gfx_render_job job = {
		.shader = s,
		.shape = GFX_RECTANGLE,
		.texture = t->handle,
		.world_transform = world_transform[0],
		.view_transform = world_view_transform[0],
		.data = data
	};
	gfx_render(&job);
}

void gfx_render_texture(struct texture *t, Rectangle *r)
{
	_gfx_render_texture(&default_shader, t, r, t);
}

void _gfx_render_quadrilateral(struct shader *s, struct texture *t, struct gfx_vertex vertices[4], void *data)
{
	gfx_remap_texture(t);
	if (!t || !t->handle || t->w <= 0 || t->h <= 0) {
		WARNING("Attempted to render uninitialized texture");
		return;
	}

	// Normalize the texture coordinates to [0, 1]
	struct gfx_vertex buf[4];
	memcpy(buf, vertices, sizeof(struct gfx_vertex) * 4);
	for (int i = 0; i < 4; i++) {
		buf[i].u /= t->w;
		buf[i].v /= t->h;
	}

	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_DYNAMIC_DRAW);

	mat4 world_transform = GLM_MAT4_IDENTITY_INIT;
	struct gfx_render_job job = {
		.shader = s,
		.shape = GFX_QUADRILATERAL,
		.texture = t->handle,
		.world_transform = world_transform[0],
		.view_transform = world_view_transform[0],
		.data = data
	};
	gfx_render(&job);
}

void gfx_render_quadrilateral(struct texture *t, struct gfx_vertex vertices[4])
{
	_gfx_render_quadrilateral(&default_shader, t, vertices, t);
}

static void init_texture(struct texture *t, int w, int h)
{
	glGenTextures(1, &t->handle);
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	t->w = w;
	t->h = h;
	t->opaque = false;
	t->ctx_gen = gfx_ctx_gen;
}

void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels)
{
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	track_live_texture(t->handle, w, h, GL_RGBA, false);
}

void gfx_update_texture_with_pixels(struct texture *t, void *pixels)
{
	if (texture_targets_view(t))
		frame_presented_for_update_view = false;
	if (!gfx_remap_texture(t)) {
		if (t && t->w > 0 && t->h > 0)
			gfx_init_texture_with_pixels(t, t->w, t->h, pixels);
		return;
	}
	t->opaque = false;
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void gfx_init_texture_with_cg(struct texture *t, struct cg *cg)
{
	gfx_init_texture_with_pixels(t, cg->metrics.w, cg->metrics.h, cg->pixels);
	if (!cg->metrics.has_alpha) {
		t->opaque = true;
	} else if (cg->pixels && cg->metrics.w >= config.view_width && cg->metrics.h >= config.view_height) {
		const uint8_t *p = cg->pixels;
		size_t n = (size_t)cg->metrics.w * cg->metrics.h;
		bool all_opaque = true;
		for (size_t i = 0; i < n; i++) {
			if (p[i * 4 + 3] != 255) {
				all_opaque = false;
				break;
			}
		}
		t->opaque = all_opaque;
	}
}

void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color)
{
	if (w > max_texture_size) {
		WARNING("Texture width %d exceeds maximum texture size %d", w, max_texture_size);
		w = max_texture_size;
	}
	if (h > max_texture_size) {
		WARNING("Texture height %d exceeds maximum texture size %d", h, max_texture_size);
		h = max_texture_size;
	}
	if (w > 0 && h > 0 && (int64_t)w * h <= 65536) {
		size_t n = (size_t)w * (size_t)h;
		uint8_t stack_buf[4096];
		uint8_t *pixels = (n * 4 <= sizeof(stack_buf)) ? stack_buf : xmalloc(n * 4);
		if ((color.r | color.g | color.b | color.a) == 0) {
			memset(pixels, 0, n * 4);
		} else if (color.r == color.g && color.r == color.b && color.r == color.a) {
			memset(pixels, color.r, n * 4);
		} else {
			uint8_t rgba[4] = { color.r, color.g, color.b, color.a };
			uint32_t packed;
			memcpy(&packed, rgba, 4);
			uint32_t *p32 = (uint32_t *)pixels;
			for (size_t i = 0; i < n; i++)
				p32[i] = packed;
		}
		gfx_init_texture_with_pixels(t, w, h, pixels);
		if (pixels != stack_buf)
			free(pixels);
		return;
	}
	gfx_init_texture_blank(t, w, h);
	if (w <= 0 || h <= 0)
		return;
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
	glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

void gfx_init_texture_rgb(struct texture *t, int w, int h, SDL_Color color)
{
	if (w > max_texture_size) {
		WARNING("Texture width %d exceeds maximum texture size %d", w, max_texture_size);
		w = max_texture_size;
	}
	if (h > max_texture_size) {
		WARNING("Texture height %d exceeds maximum texture size %d", h, max_texture_size);
		h = max_texture_size;
	}
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	track_live_texture(t->handle, w, h, GL_RGB, false);
	if (w <= 0 || h <= 0)
		return;
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
	glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	t->opaque = true;
}

void gfx_init_texture_amap(struct texture *t, int w, int h, uint8_t *amap, SDL_Color color)
{
	uint32_t *pixels = xmalloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w*h; i++) {
		uint32_t c = SDL_MapRGBA(sdl.format, color.r, color.g, color.b, amap[i]);
		pixels[i] = c;
	}

	gfx_init_texture_with_pixels(t, w, h, pixels);
	free(pixels);
}

void gfx_init_texture_rmap(struct texture *t, int w, int h, uint8_t *rmap)
{
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, rmap);
	track_live_texture(t->handle, w, h, GL_R8, true);
}

void gfx_init_texture_blank(struct texture *t, int w, int h)
{
	gfx_init_texture_with_pixels(t, w, h, NULL);
}

void gfx_copy_main_surface(struct texture *dst)
{
	init_texture(dst, main_surface.w, main_surface.h);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, main_surface_fb);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, main_surface.w, main_surface.h, 0);
	dst->opaque = true;
	track_live_texture(dst->handle, main_surface.w, main_surface.h, GL_RGB, false);
}

void gfx_delete_texture(struct texture *t)
{
	if (!t)
		return;
	if (t->handle) {
		if (t->ctx_gen != 0 && t->ctx_gen != gfx_ctx_gen && !gfx_remap_texture(t))
			return;
		if (pinned_draw_tex == t->handle)
			gfx_end_draw_target();
		if (offscreen_draw_tex == t->handle)
			offscreen_draw_tex = 0;
		if (offscreen_read_tex == t->handle)
			offscreen_read_tex = 0;
		invalidate_remap_target(t->handle);
		untrack_live_texture(t->handle);
		glDeleteTextures(1, &t->handle);
	}
	t->handle = 0;
	t->opaque = false;
	t->ctx_gen = 0;
}

void gfx_begin_draw_target(Texture *t)
{
	gfx_remap_texture(t);
	if (!t || !t->handle)
		return;
	t->opaque = false;
	pinned_draw_tex = t->handle;
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, offscreen_draw_fbo);
	if (offscreen_draw_tex != t->handle) {
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->handle, 0);
		offscreen_draw_tex = t->handle;
	}
	pinned_draw_bound = true;
}

void gfx_end_draw_target(void)
{
	if (!pinned_draw_tex && !pinned_draw_bound)
		return;
	pinned_draw_tex = 0;
	pinned_draw_bound = false;
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);
}

void gfx_flush_draw_target(Texture *t)
{
	gfx_remap_texture(t);
	if (!t || !t->handle || offscreen_draw_tex != t->handle)
		return;
	pinned_draw_bound = false;
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, main_surface_fb);
}

GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h)
{
	gfx_remap_texture(t);
	if (target == GL_READ_FRAMEBUFFER) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, offscreen_read_fbo);
		if (offscreen_read_tex != t->handle) {
			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->handle, 0);
			offscreen_read_tex = t->handle;
		}
		return offscreen_read_fbo;
	}

	if (texture_targets_view(t))
		frame_presented_for_update_view = false;
	t->opaque = false;
	if (target == GL_DRAW_FRAMEBUFFER && pinned_draw_bound && pinned_draw_tex == t->handle) {
		glViewport(x, y, w, h);
		return offscreen_draw_fbo;
	}

	glBindFramebuffer(target, offscreen_draw_fbo);
	if (offscreen_draw_tex != t->handle) {
		glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->handle, 0);
		offscreen_draw_tex = t->handle;
	}
	if (target == GL_FRAMEBUFFER)
		offscreen_read_tex = 0;
	pinned_draw_bound = (target == GL_DRAW_FRAMEBUFFER && pinned_draw_tex != 0 && pinned_draw_tex == t->handle);
	glViewport(x, y, w, h);
	return offscreen_draw_fbo;
}

void gfx_reset_framebuffer(GLenum target, GLuint fbo)
{
	(void)fbo;
	if (target == GL_READ_FRAMEBUFFER) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, main_surface_fb);
		return;
	}
	if (target == GL_DRAW_FRAMEBUFFER && pinned_draw_tex) {
		if (!pinned_draw_bound || offscreen_draw_tex != pinned_draw_tex) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, offscreen_draw_fbo);
			if (offscreen_draw_tex != pinned_draw_tex) {
				glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pinned_draw_tex, 0);
				offscreen_draw_tex = pinned_draw_tex;
			}
			pinned_draw_bound = true;
		}
		return;
	}
	pinned_draw_bound = false;
	glBindFramebuffer(target, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);
}

SDL_Color gfx_get_pixel(Texture *t, int x, int y)
{
	if (!gfx_remap_texture(t) && (!t || !t->handle))
		return (SDL_Color){0};
	GLuint fbo = gfx_set_framebuffer(GL_READ_FRAMEBUFFER, t, 0, 0, t->w, t->h);

	uint8_t pixel[4];
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

	gfx_reset_framebuffer(GL_READ_FRAMEBUFFER, fbo);

	return (SDL_Color) {
		.r = pixel[0],
		.g = pixel[1],
		.b = pixel[2],
		.a = pixel[3],
	};
}

void *gfx_get_pixels(Texture *t)
{
	if (!gfx_remap_texture(t) && (!t || !t->handle))
		return xcalloc(1, (t && t->w > 0 && t->h > 0) ? (size_t)t->w * (size_t)t->h * 4 : 4);
	gfx_flush_draw_target(t);
	GLuint fbo = gfx_set_framebuffer(GL_READ_FRAMEBUFFER, t, 0, 0, t->w, t->h);
	void *pixels = xmalloc((size_t)t->w * t->h * 4);
	glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	gfx_reset_framebuffer(GL_READ_FRAMEBUFFER, fbo);
	return pixels;
}

int gfx_save_texture(Texture *t, const char *path, enum cg_type format)
{
	void *pixels = gfx_get_pixels(t);
	struct cg cg = {
		.type = ALCG_UNKNOWN,
		.metrics = {
			.w = t->w,
			.h = t->h,
			.bpp = 24,
			.has_pixel = true,
			.has_alpha = true,
			.pixel_pitch = t->w * 3,
			.alpha_pitch = 1
		},
		.pixels = pixels
	};
	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("Failed to open %s: %s", display_utf0(path), strerror(errno));
		free(pixels);
		return 0;
	}
	int r = cg_write(&cg, format, fp);
	fclose(fp);
	free(pixels);
	return r;
}
