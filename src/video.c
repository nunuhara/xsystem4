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
#include "xsystem4.h"

struct sdl_private sdl;

static const GLchar glsl_preamble[] =
#ifdef USE_GLES
	"#version 300 es\n"
	"precision highp float;\n";
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

struct default_shader {
	struct shader s;
	GLuint alpha_mod;
} default_shader;

GLuint main_surface_fb;
struct texture main_surface;

void prepare_default_shader(struct gfx_render_job *job, void *data)
{
	struct default_shader *s = (struct default_shader*)job->shader;
	struct texture *t = (struct texture*)data;

	glUniform1f(s->alpha_mod, t->alpha_mod / 255.0);
}

static GLchar *read_shader_file(const char *path)
{
	GLchar *source = file_read(path, NULL);
	if (!source) {
		char full_path[PATH_MAX];
		snprintf(full_path, PATH_MAX, XSYS4_DATA_DIR "/%s", path);
		source = file_read(full_path, NULL);
		if (!source)
			ERROR("Failed to load shader file %s", full_path, strerror(errno));
	}
	return source;
}

static GLuint load_shader_file(const char *path, GLenum type)
{
	GLint shader_compiled;
	GLuint shader;
	const GLchar *source[2] = {
		glsl_preamble,
		read_shader_file(path)
	};
	shader = glCreateShader(type);
	glShaderSource(shader, 2, source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_compiled);
	if (!shader_compiled) {
		GLint len;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		char *infolog = xmalloc(len + 1);
		glGetShaderInfoLog(shader, len, NULL, infolog);
		ERROR("Failed to compile shader %s: %s", path, infolog);
	}
	free((char*)source[1]);
	return shader;
}

void gfx_load_shader(struct shader *dst, const char *vertex_shader_path, const char *fragment_shader_path)
{
	GLuint program = glCreateProgram();
	GLuint vertex_shader = load_shader_file(vertex_shader_path, GL_VERTEX_SHADER);
	GLuint fragment_shader = load_shader_file(fragment_shader_path, GL_FRAGMENT_SHADER);

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
	dst->vertex = glGetAttribLocation(program, "vertex_pos");
	dst->alpha_mod = glGetAttribLocation(program, "alpha_mod");
	dst->prepare = NULL;
}

static int gl_initialize(void)
{
	gfx_load_shader(&default_shader.s, "shaders/render.v.glsl", "shaders/render.f.glsl");
	default_shader.alpha_mod = glGetUniformLocation(default_shader.s.program, "alpha_mod");
	default_shader.s.prepare = prepare_default_shader;

	glClearColor(0.f, 0.f, 0.f, 1.f);

	GLfloat vertex_data[] = {
		0.f, 0.f, 0.f, 1.f,
		1.f, 0.f, 0.f, 1.f,
		1.f, 1.f, 0.f, 1.f,
		0.f, 1.f, 0.f, 1.f
	};

	GLuint index_data[] = { 0, 1, 2, 3 };

	glGenVertexArrays(1, &sdl.gl.vao);
	glBindVertexArray(sdl.gl.vao);

	glGenBuffers(1, &sdl.gl.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, 4 * 4 * sizeof(GLfloat), vertex_data, GL_STATIC_DRAW);

	glGenBuffers(1, &sdl.gl.ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * sizeof(GLuint), index_data, GL_STATIC_DRAW);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glBindVertexArray(0);

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
	sdl.window =  SDL_CreateWindow("XSystem4",
				       SDL_WINDOWPOS_UNDEFINED,
				       SDL_WINDOWPOS_UNDEFINED,
				       config.view_width,
				       config.view_height,
				       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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

	SDL_GL_SetSwapInterval(0);
	gl_initialize();
	gfx_draw_init();
	gfx_set_window_logical_size(config.view_width, config.view_height);
	atexit(gfx_fini);
	gfx_clear();
	gfx_initialized = true;
	return 0;
}

void gfx_fini(void)
{
	glDeleteProgram(default_shader.s.program);
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
	main_surface.has_alpha = true;
	main_surface.alpha_mod = 255;
	main_surface.draw_method = DRAW_METHOD_NORMAL;

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
	SDL_GL_SetSwapInterval(wait);
}

void gfx_set_clear_color(int r, int g, int b, int a)
{
	float rf = max(0, min(255, r)) / 255.0;
	float gf = max(0, min(255, g)) / 255.0;
	float bf = max(0, min(255, b)) / 255.0;
	float af = max(0, min(255, a)) / 255.0;
	glClearColor(rf, gf, bf, af);
}

void gfx_clear(void)
{
	glClear(GL_COLOR_BUFFER_BIT);
}

void gfx_swap(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(sdl.viewport.x, sdl.viewport.y, sdl.viewport.w, sdl.viewport.h);
	glClear(GL_COLOR_BUFFER_BIT);

	static mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	static mat4 wv_transform = MAT4(
		2,  0, 0, -1,
		0, -2, 0,  1,
		0,  0, 1,  0,
		0,  0, 0,  1);
	struct gfx_render_job job = {
		.shader = &default_shader.s,
		.texture = main_surface.handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = &main_surface
	};
	gfx_render(&job);

	SDL_GL_SwapWindow(sdl.window);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);
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
	glEnableVertexAttribArray(job->shader->vertex);

	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.vbo);
	glVertexAttribPointer(job->shader->vertex, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.ibo);
	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);

	glDisableVertexAttribArray(job->shader->vertex);
	glBindVertexArray(0);
	glUseProgram(0);
}

void gfx_render(struct gfx_render_job *job)
{
	if (!job->shader) {
		job->shader = &default_shader.s;
		gfx_prepare_job(job);
		gfx_run_job(job);
		job->shader = NULL;
	} else {
		gfx_prepare_job(job);
		gfx_run_job(job);
	}
}

void gfx_render_texture(struct texture *t, Rectangle *r)
{
	if (!t->handle) {
		WARNING("Attempted to render uninitialized texture");
		return;
	}
	// set blend mode
	switch (t->draw_method) {
	case DRAW_METHOD_SCREEN:
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_MULTIPLY:
		glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_ADDITIVE:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	default:
		// FIXME: why doesn't this work?
		//if (t->alpha_mod != 255) {
		//	glBlendColor(0, 0, 0, t->alpha_mod / 255.0);
		//	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_CONSTANT_ALPHA, GL_ZERO);
		//}
		break;
	}

	t->world_transform[3][0] = r ? r->x : 0;
	t->world_transform[3][1] = r ? r->y : 0;

	struct gfx_render_job job = {
		.shader = &default_shader.s,
		.texture = t->handle,
		.world_transform = t->world_transform[0],
		.view_transform = world_view_transform[0],
		.data = t
	};
	gfx_render(&job);

	if (t->draw_method != DRAW_METHOD_NORMAL || t->alpha_mod != 255)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

static void init_texture(struct texture *t, int w, int h)
{
	glGenTextures(1, &t->handle);
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	memset(t->world_transform, 0, sizeof(GLfloat)*16);
	t->world_transform[0][0] = w;
	t->world_transform[1][1] = h;
	t->world_transform[2][2] = 1;
	t->world_transform[3][3] = 1;

	t->w = w;
	t->h = h;

	t->has_alpha = true;
	t->alpha_mod = 255;
	t->draw_method = DRAW_METHOD_NORMAL;

}

void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels)
{
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void gfx_init_texture_with_cg(struct texture *t, struct cg *cg)
{
	gfx_init_texture_with_pixels(t, cg->metrics.w, cg->metrics.h, cg->pixels);
	t->has_alpha = cg->metrics.has_alpha;
}

void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color)
{
	// create pixel data
	int c = SDL_MapRGBA(sdl.format, color.r, color.g, color.b, color.a);
	uint32_t *pixels = xmalloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w*h; i++) {
		pixels[i] = c;
	}

	gfx_init_texture_with_pixels(t, w, h, pixels);
	free(pixels);
}

void gfx_init_texture_rgb(struct texture *t, int w, int h, SDL_Color color)
{
	uint8_t *pixels = xmalloc(w*h*3);
	for (int i = 0; i < w*h; i++) {
		pixels[i*3+0] = color.r;
		pixels[i*3+1] = color.g;
		pixels[i*3+2] = color.b;
	}

	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
	free(pixels);
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
			.has_alpha = t->has_alpha,
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
