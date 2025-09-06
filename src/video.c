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
#include <cglm/cglm.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/private.h"
#include "icon.h"
#include "xsystem4.h"

struct sdl_private sdl;

static const GLchar glsl_preamble[] =
#ifdef USE_GLES
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n";
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
static struct texture main_surface;
static struct texture *view = &main_surface;
static GLint max_texture_size;
static SDL_Color clear_color = { 0, 0, 0, 255 };
static float frame_rate;
static bool wait_vsync = false;

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

	GLint link_success;
	glGetProgramiv(program, GL_LINK_STATUS, &link_success);
	if (!link_success)
		ERROR("Failed to link shader: %s, %s", vertex_shader_path, fragment_shader_path);

	dst->program = program;
	dst->world_transform = glGetUniformLocation(program, "world_transform");
	dst->view_transform = glGetUniformLocation(program, "view_transform");
	dst->texture = glGetUniformLocation(program, "tex");
	dst->vertex_pos = glGetAttribLocation(program, "vertex_pos");
	dst->vertex_uv = glGetAttribLocation(program, "vertex_uv");
	dst->prepare = NULL;
}

static int gl_initialize(void)
{
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

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

	return 0;
}

static void set_window_title(void)
{
	char title[1024] = { [1023] = 0 };
	char *game_name = sjis2utf(config.game_name, 0);
	snprintf(title, 1023, "%s - XSystem4", game_name);
	free(game_name);

	SDL_SetWindowTitle(sdl.window, title);
}

static bool gfx_initialized = false;

/*
 * Reduce window size if the display resolution is smaller than the game
 * resolution.
 */
static void init_window_size(void)
{
#ifndef __ANDROID__
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

int gfx_init(void)
{
	if (gfx_initialized)
		return true;

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
	uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
#ifdef __ANDROID__
	window_flags |= SDL_WINDOW_FULLSCREEN;
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#else
	window_flags |= SDL_WINDOW_RESIZABLE;
#endif
	sdl.window =  SDL_CreateWindow("XSystem4",
				       SDL_WINDOWPOS_UNDEFINED,
				       SDL_WINDOWPOS_UNDEFINED,
				       config.view_width,
				       config.view_height,
				       window_flags);
	if (!sdl.window)
		ERROR("SDL_CreateWindow failed: %s", SDL_GetError());

	set_window_title();

	sdl.gl.context = SDL_GL_CreateContext(sdl.window);
	if (!sdl.gl.context)
		ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());

#ifndef USE_GLES
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY)
		ERROR("glewInit failed");
#endif

	SDL_GL_SetSwapInterval(wait_vsync ? 1 : 0);
	gl_initialize();
	gfx_draw_init();
	gfx_set_window_logical_size(config.view_width, config.view_height);
	init_window_size();
	atexit(gfx_fini);
	gfx_clear();
	icon_init();
	gfx_initialized = true;
	return 0;
}

void gfx_fini(void)
{
	glDeleteProgram(default_shader.program);
	SDL_DestroyWindow(sdl.window);
	SDL_FreeFormat(sdl.format);
	SDL_Quit();
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
	glBindTexture(GL_TEXTURE_2D, 0);

	main_surface.w = w;
	main_surface.h = h;

	glGenFramebuffers(1, &main_surface_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_surface.handle, 0);
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

void gfx_update_screen_scale(void)
{
	int display_w, display_h;
	SDL_GL_GetDrawableSize(sdl.window, &display_w, &display_h);

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
}

void gfx_set_clear_color(int r, int g, int b, int a)
{
	clear_color.r = max(0, min(255, r));
	clear_color.g = max(0, min(255, g));
	clear_color.b = max(0, min(255, b));
	clear_color.a = max(0, min(255, a));
}

void gfx_clear(void)
{
	glClearColor(clear_color.r / 255.f, clear_color.g / 255.f, clear_color.b / 255.f, clear_color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;

void gfx_set_view_offset(int x, int y)
{
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

void gfx_swap(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(sdl.viewport.x, sdl.viewport.y, sdl.viewport.w, sdl.viewport.h);
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
	gfx_render(&job);

	SDL_GL_SwapWindow(sdl.window);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);

	gfx_update_frame_rate_counter();
}

void gfx_set_view(struct texture *t)
{
	view = t;
}

void gfx_reset_view(void)
{
	view = &main_surface;
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
	glBindVertexArray(sdl.gl.vao);
	glEnableVertexAttribArray(job->shader->vertex_pos);
	glEnableVertexAttribArray(job->shader->vertex_uv);

	glBindBuffer(GL_ARRAY_BUFFER, job->shape == GFX_QUADRILATERAL ? sdl.gl.quad_vbo : sdl.gl.vbo);
	glVertexAttribPointer(job->shader->vertex_pos, 4, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), NULL);
	glVertexAttribPointer(job->shader->vertex_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), (void*)offsetof(struct gfx_vertex, u));

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

	glDisableVertexAttribArray(job->shader->vertex_pos);
	glDisableVertexAttribArray(job->shader->vertex_uv);
	glBindVertexArray(0);
	glUseProgram(0);
}

void gfx_render(struct gfx_render_job *job)
{
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
	if (!t->handle) {
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

void gfx_render_quadrilateral(struct texture *t, struct gfx_vertex vertices[4])
{
	if (!t->handle) {
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
		.shader = &default_shader,
		.shape = GFX_QUADRILATERAL,
		.texture = t->handle,
		.world_transform = world_transform[0],
		.view_transform = world_view_transform[0],
		.data = t
	};
	gfx_render(&job);
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
}

void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels)
{
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void gfx_update_texture_with_pixels(struct texture *t, void *pixels)
{
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void gfx_init_texture_with_cg(struct texture *t, struct cg *cg)
{
	gfx_init_texture_with_pixels(t, cg->metrics.w, cg->metrics.h, cg->pixels);
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
	if (w <= 0 || h <= 0)
		return;
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
	glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
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
}

void gfx_init_texture_blank(struct texture *t, int w, int h)
{
	gfx_init_texture_with_pixels(t, w, h, NULL);
}

void gfx_copy_main_surface(struct texture *dst)
{
	init_texture(dst, main_surface.w, main_surface.h);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, main_surface.w, main_surface.h, 0);
}

void gfx_delete_texture(struct texture *t)
{
	if (t->handle)
		glDeleteTextures(1, &t->handle);
	t->handle = 0;
}

GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h)
{
	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(target, fbo);
	glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->handle, 0);
	glViewport(x, y, w, h);

	if (glCheckFramebufferStatus(target) != GL_FRAMEBUFFER_COMPLETE)
		ERROR("Incomplete framebuffer");
	return fbo;
}

void gfx_reset_framebuffer(GLenum target, GLuint fbo)
{
	glBindFramebuffer(target, main_surface_fb);
	glDeleteFramebuffers(1, &fbo);
	glViewport(0, 0, sdl.w, sdl.h);
}

SDL_Color gfx_get_pixel(Texture *t, int x, int y)
{
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
	GLuint fbo = gfx_set_framebuffer(GL_READ_FRAMEBUFFER, t, 0, 0, t->w, t->h);
	void *pixels = xmalloc(t->w * t->h * 4);
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
