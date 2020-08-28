/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <math.h>
#include <SDL.h>
#include <GL/glew.h>
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "system4.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

struct copy_shader {
	Shader s;
	GLint bot_left;
	GLint top_right;
	// optional
	GLint color;
	GLint threshold;
};

struct copy_data {
	int dx, dy, sx, sy, w, h;
	int vpx, vpy, vpw, vph;
	int sw, sh; // used to compute scale factor for stretch
	// optional
	float r, g, b, a;
	float threshold;
};

#define ROTATE_DATA(dst, _sx, _sy, _w, _h)		\
	{ .dx = 0, .dy = 0, .sx=_sx, .sy=_sy, .w=_w, .h=_h, .sw=_w, .sh=_h, .vpx=0, .vpy=0, .vpw=dst->w, .vph=dst->h }

// set copy_data required for a stretch operation
#define STRETCH_DATA(_dx, _dy, _dw, _dh, _sx, _sy, _sw, _sh)		\
	{ .dx=_dx, .dy=_dy, .sx=_sx, .sy=_sy, .w=_dw, .h=_dh, .sw=_sw, .sh=_sh, .vpx=_dx, .vpy=_dy, .vpw=_dw, .vph=_dh }

// set copy_data required for a simple copy operation
#define COPY_DATA(_dx, _dy, _sx, _sy, _w, _h)			\
	STRETCH_DATA(_dx, _dy, _w, _h, _sx, _sy, _w, _h)

static struct copy_shader copy_shader;
static struct copy_shader copy_key_shader;
static struct copy_shader copy_use_amap_under_shader;
static struct copy_shader copy_use_amap_border_shader;
static struct copy_shader blend_amap_alpha_shader;
static struct copy_shader fill_shader;
static struct copy_shader hitbox_shader;
static struct copy_shader hitbox_noblend_shader;

static void prepare_copy_shader(struct gfx_render_job *job, void *data)
{
	struct copy_shader *s = (struct copy_shader*)job->shader;
	struct copy_data *d = (struct copy_data*)data;
	glUniform2f(s->bot_left, d->sx, d->sy);
	glUniform2f(s->top_right, d->sx + d->w, d->sy + d->h);

	glUniform4f(s->color, d->r, d->g, d->b, d->a);
	glUniform1f(s->threshold, d->threshold);
}

static void load_copy_shader(struct copy_shader *s, const char *v_path, const char *f_path)
{
	gfx_load_shader(&s->s, v_path, f_path);
	s->bot_left = glGetUniformLocation(s->s.program, "bot_left");
	s->top_right = glGetUniformLocation(s->s.program, "top_right");
	s->color = glGetUniformLocation(s->s.program, "color");
	s->threshold = glGetUniformLocation(s->s.program, "threshold");
	s->s.prepare = prepare_copy_shader;
}

// load shaders
void gfx_draw_init(void)
{
	// generic copy shader; discards fragments outside of a given rectangle
	load_copy_shader(&copy_shader, "shaders/render.v.glsl", "shaders/copy.f.glsl");

	// copy shader which also discards fragments matching a color key
	load_copy_shader(&copy_key_shader, "shaders/render.v.glsl", "shaders/copy_key.f.glsl");

	// copy shader that only keeps fragments below a certain alpha threshold
	load_copy_shader(&copy_use_amap_under_shader, "shaders/render.v.glsl", "shaders/copy_use_amap_under.f.glsl");

	// copy shader that only keeps fragments above a certain alpha threshold
	load_copy_shader(&copy_use_amap_border_shader, "shaders/render.v.glsl", "shaders/copy_use_amap_border.f.glsl");

	// copy shader that multiples the source alpha by a constant
	load_copy_shader(&blend_amap_alpha_shader, "shaders/render.v.glsl", "shaders/blend_amap_alpha.f.glsl");

	// basic fill shader
	load_copy_shader(&fill_shader, "shaders/render.v.glsl", "shaders/fill.f.glsl");

	// shader that discards texels that fail a hitbox test
	load_copy_shader(&hitbox_shader, "shaders/render.v.glsl", "shaders/hitbox.f.glsl");

	// hitbox shader which ignores alpha component of texture (a=1)
	load_copy_shader(&hitbox_noblend_shader, "shaders/render.v.glsl", "shaders/hitbox_noblend.f.glsl");
}

static void run_draw_shader(Shader *s, Texture *dst, Texture *src, GLfloat *mw_transform, GLfloat *wv_transform, struct copy_data *data)
{
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, data->vpx, data->vpy, data->vpw, data->vph);

	struct gfx_render_job job = {
		.shader = s,
		.texture = src ? src->handle : 0,
		.world_transform = mw_transform,
		.view_transform = wv_transform,
		.data = data
	};
	gfx_render(&job);

	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

static void run_copy_shader(Shader *s, Texture *dst, Texture *src, struct copy_data *data)
{
	GLfloat src_w = src ? src->w : data->w;
	GLfloat src_h = src ? src->h : data->h;
	GLfloat scale_x = (GLfloat)data->w / data->sw;
	GLfloat scale_y = (GLfloat)data->h / data->sh;
	GLfloat mw_transform[16] = {
		[0]  = src_w * scale_x,
		[5]  = src_h * scale_y,
		[10] = 1,
		[12] = -data->sx * scale_y,
		[13] = -data->sy * scale_y,
		[15] = 1
	};
	GLfloat wv_transform[16] = {
		[0]  =  2.0 / data->w,
		[5]  =  2.0 / data->h,
		[10] =  2,
		[12] = -1,
		[13] = -1,
		[14] = -1,
		[15] =  1
	};
	run_draw_shader(s, dst, src, mw_transform, wv_transform, data);
}

static void restore_blend_mode(void)
{
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
	glBlendColor(0, 0, 0 ,0);
}

void gfx_copy(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_amap(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int rate)
{
	GLfloat f_rate = rate / 255.0;
	glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
	glBlendColor(f_rate, f_rate, f_rate, f_rate);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_sprite(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, SDL_Color color)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = color.r / 255.0;
	data.g = color.g / 255.0;
	data.b = color.b / 255.0;
	run_copy_shader(&copy_key_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_use_amap_under(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int threshold)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.threshold = threshold / 255.0;
	run_copy_shader(&copy_use_amap_under_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_use_amap_border(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int threshold)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.threshold = threshold / 255.0;
	run_copy_shader(&copy_use_amap_border_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_amap_max(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_amap_min(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendEquationSeparate(GL_FUNC_ADD, GL_MIN);
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a)
{
	glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ZERO, GL_ONE);
	glBlendColor(0, 0, 0, a / 255.0);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_amap(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_amap_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.threshold = a / 255.0;
	run_copy_shader(&blend_amap_alpha_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_fill(Texture *dst, int x, int y, int w, int h, int r, int g, int b)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = 1;
	run_copy_shader(&fill_shader.s, dst, NULL, &data);

	restore_blend_mode();
}

void gfx_fill_alpha_color(Texture *dst, int x, int y, int w, int h, int r, int g, int b, int a)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = a / 255.0;
	run_copy_shader(&fill_shader.s, dst, NULL, &data);

	restore_blend_mode();
}

void gfx_fill_amap(Texture *dst, int x, int y, int w, int h, int a)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.a = a / 255.0;
	run_copy_shader(&fill_shader.s, dst, NULL, &data);

	restore_blend_mode();
}

void gfx_add_da_daxsa(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ONE); // ???

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

// FIXME: DrawGraph.dll does some really strange clipping on the src/dst rectangles
//        before drawing. This implementation does what you would normally expect
//        a stretch function to do when passed negative (x,y) coordinates.
//        Probably no games depend on this behavior, but we'll see.
void gfx_copy_stretch(Texture *dst, int dx, int dy, int dw, int dh, Texture *src, int sx, int sy, int sw, int sh)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

// FIXME: as above
void gfx_copy_stretch_amap(Texture *dst, int dx, int dy, int dw, int dh, Texture *src, int sx, int sy, int sw, int sh)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

// FIXME: this doesn't work correctly when the src rectangle crosses the edge of the CG.
static void copy_rot_zoom(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag, Shader *shader)
{
	double theta = -rotate * (M_PI / 180.0);
	double mag_cos_theta = cos(theta) * mag;
	double mag_sin_theta = sin(theta) * mag;

	// 1. scale to src texture size
	// 2. translate so that center point of copy region is at origin
	// 3. rotate
	// 4. tranlate to center of dst texture
	GLfloat mw_transform[16] = {
		[0]  =  mag_cos_theta * src->w,
		[1]  =  mag_sin_theta * src->w,
		[4]  = -mag_sin_theta * src->h,
		[5]  =  mag_cos_theta * src->h,
		[10] =  1,
		[12] =  (dst->w - (w*mag_cos_theta) - (2*sx*mag_cos_theta) + (h*mag_sin_theta) + (2*sy*mag_sin_theta)) / 2.0,
		[13] =  (dst->h - (h*mag_cos_theta) - (2*sy*mag_cos_theta) - (w*mag_sin_theta) - (2*sx*mag_sin_theta)) / 2.0,
		[15] =  1,
	};
	GLfloat wv_transform[16] = {
		[0]  =  2.0 / dst->w,
		[5]  =  2.0 / dst->h,
		[10] =  2,
		[12] = -1,
		[13] = -1,
		[14] = -1,
		[15] = 1
	};

	struct copy_data data = ROTATE_DATA(dst, sx, sy, w, h);
	run_draw_shader(shader, dst, src, mw_transform, wv_transform, &data);
}

void gfx_copy_rot_zoom(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag)
{
	copy_rot_zoom(dst, src, sx, sy, w, h, rotate, mag, &hitbox_noblend_shader.s);
}

void gfx_copy_rot_zoom_amap(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag)
{
	gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 0);

	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
	copy_rot_zoom(dst, src, sx, sy, w, h, rotate, mag, &hitbox_shader.s);
	restore_blend_mode();
}

void gfx_copy_rot_zoom_use_amap(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag)
{
	copy_rot_zoom(dst, src, sx, sy, w, h, rotate, mag, &hitbox_shader.s);
}

void gfx_copy_reverse_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	GLfloat mw_transform[16] = {
		[0]  = -src->w,
		[5]  =  src->h,
		[10] =  1,
		[12] =  sx + w,
		[13] = -sy,
		[15] =  1
	};
	GLfloat wv_transform[16] = {
		[0]  =  2.0 / w,
		[5]  =  2.0 / h,
		[10] =  2,
		[12] = -1,
		[13] = -1,
		[14] = -1,
		[15] =  1
	};

	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}

void gfx_copy_reverse_amap_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	GLfloat mw_transform[16] = {
		[0]  = -src->w,
		[5]  =  src->h,
		[10] =  1,
		[12] =  sx + w,
		[13] = -sy,
		[15] =  1
	};
	GLfloat wv_transform[16] = {
		[0]  =  2.0 / w,
		[5]  =  2.0 / h,
		[10] =  2,
		[12] = -1,
		[13] = -1,
		[14] = -1,
		[15] =  1
	};

	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}
