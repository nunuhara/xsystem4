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
#include <GL/glew.h>
#include <string.h>
#include <errno.h>

#include "system4.h"
#include "gfx_core.h"
#include "gfx_private.h"
#include "cg.h"
#include "file.h"

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
static GLfloat world_view_transform[16] = {
	[10] =  2,
	[12] = -1,
	[13] = -1,
	[14] = -1,
	[15] =  1
};

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

static GLuint load_shader_file(const char *path, GLenum type)
{
	GLint shader_compiled;
	GLuint shader;
	const GLchar *source;

	source = file_read(path, NULL);
	if (!source)
		ERROR("Failed to load shader file %s: %s", path, strerror(errno));

	shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_compiled);
	if (!shader_compiled)
		ERROR("Failed to compile shader: %s", path);
	free((char*)source);
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
	dst->texture = glGetUniformLocation(program, "texture");
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

	glGenBuffers(1, &sdl.gl.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, 4 * 4 * sizeof(GLfloat), vertex_data, GL_STATIC_DRAW);

	glGenBuffers(1, &sdl.gl.ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * sizeof(GLuint), index_data, GL_STATIC_DRAW);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);


	return 0;
}

int gfx_init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		ERROR("SDL_Init failed: %s", SDL_GetError());

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	sdl.format = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);
	sdl.window =  SDL_CreateWindow("XSystem4",
				       SDL_WINDOWPOS_UNDEFINED,
				       SDL_WINDOWPOS_UNDEFINED,
				       config.view_width,
				       config.view_height,
				       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!sdl.window)
		ERROR("SDL_CreateWindow failed: %s", SDL_GetError());

	sdl.gl.context = SDL_GL_CreateContext(sdl.window);
	if (!sdl.gl.context)
		ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
		ERROR("glewInit failed");

	SDL_GL_SetSwapInterval(0);
	gl_initialize();
	gfx_draw_init();
	gfx_set_window_size(config.view_width, config.view_height);
	atexit(gfx_fini);
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

void gfx_fullscreen(bool on)
{
	if (on && !sdl.fs_on) {
		sdl.fs_on = true;
		SDL_SetWindowFullscreen(sdl.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	} else if (!on && sdl.fs_on) {
		sdl.fs_on = false;
		SDL_SetWindowFullscreen(sdl.window, 0);
	}
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

void gfx_set_window_size(int w, int h)
{
	sdl.w = w;
	sdl.h = h;
	world_view_transform[0] = 2.0 / w;
	world_view_transform[5] = 2.0 / h;
	SDL_SetWindowSize(sdl.window, w, h);
	main_surface_init(w, h);
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
	glClear(GL_COLOR_BUFFER_BIT);

	static GLfloat mw_transform[16] = {
		[0]  = 1,
		[5]  = 1,
		[10] = 1,
		[15] = 1
	};
	static GLfloat wv_transform[16] = {
		[0]  =  2,
		[5]  = -2,
		[10] =  1,
		[12] = -1,
		[13] =  1,
		[15] =  1
	};
	struct gfx_render_job job = {
		.shader = &default_shader.s,
		.texture = main_surface.handle,
		.world_transform = mw_transform,
		.view_transform = wv_transform,
		.data = &main_surface
	};
	gfx_render(&job);

	SDL_GL_SwapWindow(sdl.window);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
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
	glEnableVertexAttribArray(job->shader->vertex);

	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.vbo);
	glVertexAttribPointer(job->shader->vertex, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.ibo);
	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);

	glDisableVertexAttribArray(job->shader->vertex);
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
	default:
		// FIXME: why doesn't this work?
		//if (t->alpha_mod != 255) {
		//	glBlendColor(0, 0, 0, t->alpha_mod / 255.0);
		//	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_CONSTANT_ALPHA, GL_ZERO);
		//}
		break;
	}

	t->world_transform[12] = r->x;
	t->world_transform[13] = r->y;

	struct gfx_render_job job = {
		.shader = &default_shader.s,
		.texture = t->handle,
		.world_transform = t->world_transform,
		.view_transform = world_view_transform,
		.data = t
	};
	gfx_render(&job);

	if (t->draw_method != DRAW_METHOD_NORMAL || t->alpha_mod != 255)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels, GLenum format)
{
	glGenTextures(1, &t->handle);
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);

	t->world_transform[0] = w;
	t->world_transform[5] = h;
	t->world_transform[10] = 1;
	// z ???
	t->world_transform[15] = 1;

	t->w = w;
	t->h = h;

	t->has_alpha = true;
	t->alpha_mod = 255;
	t->draw_method = DRAW_METHOD_NORMAL;
}

void gfx_init_texture_with_cg(struct texture *t, struct cg *cg)
{
	gfx_init_texture_with_pixels(t, cg->metrics.w, cg->metrics.h, cg->pixels, GL_RGBA);
	t->has_alpha = cg->metrics.has_alpha;
}

void gfx_init_texture_with_color(struct texture *t, int w, int h, SDL_Color color)
{
	// create pixel data
	int c = SDL_MapRGBA(sdl.format, color.r, color.g, color.b, color.a);
	uint32_t *pixels = xmalloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w*h; i++) {
		pixels[i] = c;
	}

	gfx_init_texture_with_pixels(t, w, h, pixels, GL_RGBA);
	free(pixels);
}

void gfx_init_texture_blank(struct texture *t, int w, int h)
{
	gfx_init_texture_with_pixels(t, w, h, NULL, GL_RGBA);
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
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
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
