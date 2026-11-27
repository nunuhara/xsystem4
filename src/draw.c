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

#include <limits.h>
#include <math.h>
#include <SDL.h>
#include "gfx/gl.h"
#include <cglm/cglm.h>

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
	GLint threshold2;
};

struct copy_data {
	int dx, dy, sx, sy, w, h;
	int vpx, vpy, vpw, vph;
	int sw, sh; // used to compute scale factor for stretch
	// optional
	float r, g, b, a;
	float threshold;
	float threshold2;
	GLuint dst_texture;
	int blur_dx, blur_dy, blur_radius, blur_mode;
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
static Shader copy_straight_alpha_shader;
static GLint copy_straight_alpha_dst;
static GLint copy_straight_alpha_origin;
static struct copy_shader copy_color_reverse_shader;
static struct copy_shader copy_key_shader;
static struct copy_shader copy_alpha_key_shader;
static struct copy_shader copy_use_amap_under_shader;
static struct copy_shader copy_use_amap_border_shader;
static struct copy_shader copy_grayscale_shader;
static struct copy_shader blend_amap_color_shader;
static struct copy_shader blend_amap_alpha_bright_shader;
static struct copy_shader blend_use_amap_color_shader;
static struct copy_shader fill_shader;
static struct copy_shader fill_amap_over_border_shader;
static struct copy_shader fill_amap_under_border_shader;
static struct copy_shader fill_amap_gradation_ud_shader;
static struct copy_shader hitbox_shader;
static struct copy_shader hitbox_noblend_shader;
static struct copy_shader amap_saturate_shader;
static struct copy_shader blend_rmap_color_shader;
static struct copy_shader dilate_shader;

struct box_blur_shader {
	Shader s;
	GLint rect_min;
	GLint rect_max;
	GLint blur_dir;
	GLint blur_radius;
	GLint blur_mode;
};
static struct box_blur_shader box_blur_shader;

static void prepare_copy_shader(struct gfx_render_job *job, void *data)
{
	struct copy_shader *s = (struct copy_shader*)job->shader;
	struct copy_data *d = (struct copy_data*)data;
	glUniform2f(s->bot_left, d->sx, d->sy);
	glUniform2f(s->top_right, d->sx + d->w, d->sy + d->h);

	glUniform4f(s->color, d->r, d->g, d->b, d->a);
	glUniform1f(s->threshold, d->threshold);
	glUniform1f(s->threshold2, d->threshold2);
}

static void load_copy_shader(struct copy_shader *s, const char *v_path, const char *f_path)
{
	gfx_load_shader(&s->s, v_path, f_path);
	s->bot_left = glGetUniformLocation(s->s.program, "bot_left");
	s->top_right = glGetUniformLocation(s->s.program, "top_right");
	s->color = glGetUniformLocation(s->s.program, "color");
	s->threshold = glGetUniformLocation(s->s.program, "threshold");
	s->threshold2 = glGetUniformLocation(s->s.program, "threshold2");
	s->s.prepare = prepare_copy_shader;
}

static void prepare_copy_straight_alpha_shader(possibly_unused struct gfx_render_job *job, void *data)
{
	struct copy_data *d = data;
	glUniform1i(copy_straight_alpha_dst, 1);
	glUniform2i(copy_straight_alpha_origin, d->dx, d->dy);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, d->dst_texture);
	glActiveTexture(GL_TEXTURE0);
}

static const GLchar draw_glsl_preamble[] =
#ifdef USE_GLES
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp sampler2D;\n";
#else
	"#version 140\n";
#endif

static const GLchar box_blur_vert_src[] =
	"uniform mat4 world_transform;\n"
	"uniform mat4 view_transform;\n"
	"in vec4 vertex_pos;\n"
	"in vec2 vertex_uv;\n"
	"out vec2 tex_coord;\n"
	"void main() {\n"
	"\tgl_Position = view_transform * world_transform * vertex_pos;\n"
	"\ttex_coord = vertex_uv;\n"
	"}\n";

static const GLchar box_blur_frag_src[] =
	"uniform sampler2D tex;\n"
	"uniform ivec2 rect_min;\n"
	"uniform ivec2 rect_max;\n"
	"uniform ivec2 blur_dir;\n"
	"uniform int blur_radius;\n"
	"uniform int blur_mode;\n"
	"in vec2 tex_coord;\n"
	"out vec4 frag_color;\n"
	"void main() {\n"
	"\tivec2 tex_size = textureSize(tex, 0);\n"
	"\tivec2 base = ivec2(floor(tex_coord * vec2(tex_size)));\n"
	"\tivec2 valid_min = max(rect_min, ivec2(0));\n"
	"\tivec2 valid_max = min(rect_max, tex_size);\n"
	"\tvec4 sum = vec4(0.0);\n"
	"\tbool ortho_ok = (blur_dir.x != 0)\n"
	"\t\t? (base.y >= valid_min.y && base.y < valid_max.y)\n"
	"\t\t: (base.x >= valid_min.x && base.x < valid_max.x);\n"
	"\tif (ortho_ok) {\n"
	"\t\tint lo = (blur_dir.x != 0) ? max(-blur_radius, valid_min.x - base.x)\n"
	"\t\t                           : max(-blur_radius, valid_min.y - base.y);\n"
	"\t\tint hi = (blur_dir.x != 0) ? min(blur_radius, valid_max.x - 1 - base.x)\n"
	"\t\t                           : min(blur_radius, valid_max.y - 1 - base.y);\n"
	"\t\tfor (int i = lo; i <= hi; i++) {\n"
	"\t\t\tsum += texelFetch(tex, base + blur_dir * i, 0);\n"
	"\t\t}\n"
	"\t}\n"
	"\tvec4 blurred = sum / (float(blur_radius) * 2.0 + 1.0);\n"
	"\tif (blur_mode == 1) {\n"
	"\t\tfloat src_a = (base.x >= 0 && base.y >= 0 && base.x < tex_size.x && base.y < tex_size.y)\n"
	"\t\t\t? texelFetch(tex, base, 0).a : 0.0;\n"
	"\t\tfrag_color = vec4(blurred.rgb, src_a);\n"
	"\t} else {\n"
	"\t\tfrag_color = blurred;\n"
	"\t}\n"
	"}\n";

static GLuint compile_inline_shader(const char *label, GLenum type, const GLchar *body)
{
	const GLchar *src[2] = { draw_glsl_preamble, body };
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 2, src, NULL);
	glCompileShader(sh);
	GLint ok = GL_FALSE;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512] = {0};
		glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
		ERROR("Failed to compile inline shader %s: %s", label, log);
	}
	return sh;
}

static GLint clamp_i64_to_glint(int64_t v)
{
	if (v < INT_MIN)
		return INT_MIN;
	if (v > INT_MAX)
		return INT_MAX;
	return (GLint)v;
}

static void prepare_box_blur_shader(possibly_unused struct gfx_render_job *job, void *data)
{
	struct copy_data *d = data;
	glUniform2i(box_blur_shader.rect_min, d->sx, d->sy);
	glUniform2i(box_blur_shader.rect_max,
			clamp_i64_to_glint((int64_t)d->sx + d->w),
			clamp_i64_to_glint((int64_t)d->sy + d->h));
	glUniform2i(box_blur_shader.blur_dir, d->blur_dx, d->blur_dy);
	glUniform1i(box_blur_shader.blur_radius, d->blur_radius);
	glUniform1i(box_blur_shader.blur_mode, d->blur_mode);
}

static void load_box_blur_shader(void)
{
	GLuint program = glCreateProgram();
	GLuint vs = compile_inline_shader("box_blur.v", GL_VERTEX_SHADER, box_blur_vert_src);
	GLuint fs = compile_inline_shader("box_blur.f", GL_FRAGMENT_SHADER, box_blur_frag_src);
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDetachShader(program, vs);
	glDetachShader(program, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512] = {0};
		glGetProgramInfoLog(program, sizeof(log) - 1, NULL, log);
		ERROR("Failed to link inline box_blur shader: %s", log);
	}
	box_blur_shader.s.program = program;
	box_blur_shader.s.world_transform = glGetUniformLocation(program, "world_transform");
	box_blur_shader.s.view_transform = glGetUniformLocation(program, "view_transform");
	box_blur_shader.s.texture = glGetUniformLocation(program, "tex");
	box_blur_shader.s.vertex_pos = glGetAttribLocation(program, "vertex_pos");
	box_blur_shader.s.vertex_uv = glGetAttribLocation(program, "vertex_uv");
	box_blur_shader.s.prepare = prepare_box_blur_shader;
	box_blur_shader.rect_min = glGetUniformLocation(program, "rect_min");
	box_blur_shader.rect_max = glGetUniformLocation(program, "rect_max");
	box_blur_shader.blur_dir = glGetUniformLocation(program, "blur_dir");
	box_blur_shader.blur_radius = glGetUniformLocation(program, "blur_radius");
	box_blur_shader.blur_mode = glGetUniformLocation(program, "blur_mode");
}

// load shaders
static void draw_load_shaders(void)
{
	// generic copy shader; discards fragments outside of a given rectangle
	load_copy_shader(&copy_shader, "shaders/render.v.glsl", "shaders/copy.f.glsl");

	gfx_load_shader(&copy_straight_alpha_shader, "shaders/render.v.glsl", "shaders/copy_straight_alpha.f.glsl");
	copy_straight_alpha_shader.prepare = prepare_copy_straight_alpha_shader;
	copy_straight_alpha_dst = glGetUniformLocation(copy_straight_alpha_shader.program, "dst_tex");
	copy_straight_alpha_origin = glGetUniformLocation(copy_straight_alpha_shader.program, "dst_origin");

	// copy shader which inverts colors
	load_copy_shader(&copy_color_reverse_shader, "shaders/render.v.glsl", "shaders/copy_color_reverse.f.glsl");

	// copy shader which also discards fragments matching a color key
	load_copy_shader(&copy_key_shader, "shaders/render.v.glsl", "shaders/copy_key.f.glsl");

	// copy shader which also discards fragments matching an alpha key
	load_copy_shader(&copy_alpha_key_shader, "shaders/render.v.glsl", "shaders/copy_alpha_key.f.glsl");

	// copy shader that only keeps fragments below a certain alpha threshold
	load_copy_shader(&copy_use_amap_under_shader, "shaders/render.v.glsl", "shaders/copy_use_amap_under.f.glsl");

	// copy shader that only keeps fragments above a certain alpha threshold
	load_copy_shader(&copy_use_amap_border_shader, "shaders/render.v.glsl", "shaders/copy_use_amap_border.f.glsl");

	// copy shader with grayscale conversion
	load_copy_shader(&copy_grayscale_shader, "shaders/render.v.glsl", "shaders/copy_grayscale.f.glsl");

	// copy shader that sets source RGB to a constant
	load_copy_shader(&blend_amap_color_shader, "shaders/render.v.glsl", "shaders/blend_amap_color.f.glsl");

	// copy shader that multiples the source color and alpha by a constant
	load_copy_shader(&blend_amap_alpha_bright_shader, "shaders/render.v.glsl", "shaders/blend_amap_alpha_bright.f.glsl");

	load_copy_shader(&blend_use_amap_color_shader, "shaders/render.v.glsl", "shaders/blend_use_amap_color.f.glsl");

	// basic fill shader
	load_copy_shader(&fill_shader, "shaders/render.v.glsl", "shaders/fill.f.glsl");

	// fill shader that only fills fragments above a certain alpha threshold
	load_copy_shader(&fill_amap_over_border_shader, "shaders/render.v.glsl", "shaders/fill_amap_over_border.f.glsl");

	// fill shader that only fills fragments below a certain alpha threshold
	load_copy_shader(&fill_amap_under_border_shader, "shaders/render.v.glsl", "shaders/fill_amap_under_border.f.glsl");

	// fill shader that fills the alpha map with a gradient
	load_copy_shader(&fill_amap_gradation_ud_shader, "shaders/render.v.glsl", "shaders/fill_amap_gradation_ud.f.glsl");

	// shader that discards texels that fail a hitbox test
	load_copy_shader(&hitbox_shader, "shaders/render.v.glsl", "shaders/hitbox.f.glsl");

	// hitbox shader which ignores alpha component of texture (a=1)
	load_copy_shader(&hitbox_noblend_shader, "shaders/render.v.glsl", "shaders/hitbox_noblend.f.glsl");

	// shader that sets the color to black or white depending on alpha value
	load_copy_shader(&amap_saturate_shader, "shaders/render.v.glsl", "shaders/amap_saturate.f.glsl");

	// shader that sets source RGB to a constant, using source red channel as alpha
	load_copy_shader(&blend_rmap_color_shader, "shaders/render.v.glsl", "shaders/blend_rmap_color.f.glsl");

	// shader that dilates every pixel (for bold/outline text rendering)
	load_copy_shader(&dilate_shader, "shaders/render.v.glsl", "shaders/dilate.f.glsl");

	// single-pass separable box blur shader
	load_box_blur_shader();
}

static void draw_on_gl_reset(possibly_unused void *user)
{
	draw_load_shaders();
}

void gfx_draw_init(void)
{
	draw_load_shaders();
	gfx_register_reset_callback(draw_on_gl_reset, NULL);
}

static void run_draw_shader(Shader *s, Texture *dst, Texture *src, mat4 mw_transform, mat4 wv_transform, struct copy_data *data)
{
	if (dst && !gfx_remap_texture(dst) && !dst->handle)
		return;
	if (src && !gfx_remap_texture(src) && !src->handle)
		return;
	if (data->vpw <= 0 || data->vph <= 0)
		return;
	int64_t sc_x0 = data->vpx;
	int64_t sc_y0 = data->vpy;
	int64_t sc_x1 = sc_x0 + data->vpw;
	int64_t sc_y1 = sc_y0 + data->vph;
	if (dst) {
		if (sc_x0 < 0) sc_x0 = 0;
		if (sc_y0 < 0) sc_y0 = 0;
		if (sc_x1 > dst->w) sc_x1 = dst->w;
		if (sc_y1 > dst->h) sc_y1 = dst->h;
	}
	if (sc_x1 <= sc_x0 || sc_y1 <= sc_y0)
		return;
	if (src && dst && src->handle && src->handle == dst->handle)
		gfx_flush_draw_target(dst);
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, data->vpx, data->vpy, data->vpw, data->vph);
	glEnable(GL_SCISSOR_TEST);
	glScissor((GLint)sc_x0, (GLint)sc_y0, (GLsizei)(sc_x1 - sc_x0), (GLsizei)(sc_y1 - sc_y0));

	struct gfx_render_job job = {
		.shader = s,
		.shape = GFX_RECTANGLE,
		.texture = src ? src->handle : 0,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = data
	};
	gfx_render(&job);

	glDisable(GL_SCISSOR_TEST);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

static void _run_copy_shader(Shader *s, Texture *dst, Texture *src, GLfloat src_w, GLfloat src_h, struct copy_data *data)
{
	if (data->w <= 0 || data->h <= 0 || data->sw <= 0 || data->sh <= 0)
		return;
	GLfloat scale_x = (GLfloat)data->w / data->sw;
	GLfloat scale_y = (GLfloat)data->h / data->sh;

	mat4 mw_transform = MAT4(
	     src_w * scale_x, 0,               0, -data->sx * scale_x,
	     0,               src_h * scale_y, 0, -data->sy * scale_y,
	     0,               0,               1, 0,
	     0,               0,               0, 1);
	mat4 wv_transform = WV_TRANSFORM(data->w, data->h);
	run_draw_shader(s, dst, src, mw_transform, wv_transform, data);
}

static void run_copy_shader(Shader *s, Texture *dst, Texture *src, struct copy_data *data)
{
	GLfloat src_w = src ? src->w : data->w;
	GLfloat src_h = src ? src->h : data->h;
	_run_copy_shader(s, dst, src, src_w, src_h, data);
}

static void run_fill_shader(Shader *s, Texture *dst, struct copy_data *data)
{
	if (!dst || !dst->handle || data->w <= 0 || data->h <= 0)
		return;
	// Copy the target sub-region of `dst` into a temporary texture first so `dst`
	// is never simultaneously bound as the draw FBO attachment and GL_TEXTURE0
	// (avoiding GLES 3.0 feedback loops) and UVs map directly to `(dx, dy, w, h)`.
	Texture tmp = {0};
	if (data->dx < 0 || data->dy < 0 ||
			(int64_t)data->dx + data->w > dst->w ||
			(int64_t)data->dy + data->h > dst->h) {
		gfx_init_texture_rgba(&tmp, data->w, data->h, (SDL_Color){0, 0, 0, 0});
	} else {
		gfx_init_texture_blank(&tmp, data->w, data->h);
	}
	glBindTexture(GL_TEXTURE_2D, tmp.handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
	struct copy_data copy = COPY_DATA(0, 0, data->dx, data->dy, data->w, data->h);
	run_copy_shader(&copy_shader.s, &tmp, dst, &copy);
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
	_run_copy_shader(s, dst, &tmp, data->w, data->h, data);
	gfx_delete_texture(&tmp);
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

void gfx_sprite_copy_amap(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int alpha_key)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.a = alpha_key / 255.0;
	run_copy_shader(&copy_alpha_key_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_color_reverse(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_color_reverse_shader.s, dst, src, &data);

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

void gfx_blend_src_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a, int rate)
{
	GLfloat f_rate = rate / 255.0;
	f_rate *= (a / 255.0);
	glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ZERO, GL_ONE);
	glBlendColor(f_rate, f_rate, f_rate, a / 255.0);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_add_satur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE);

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

void gfx_blend_amap_src_only(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_amap_color(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int r, int g, int b)
{
	// color = (r,g,b) * src_alpha + dst_color * (1 - src_alpha)
	// alpha = dst_alpha
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = 1.0;
	run_copy_shader(&blend_amap_color_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_amap_color_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int r, int g, int b, int a)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = a / 255.0;
	run_copy_shader(&blend_amap_color_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_amap_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = 1.0;
	data.g = 1.0;
	data.b = 1.0;
	data.a = a / 255.0;
	run_copy_shader(&blend_amap_alpha_bright_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_amap_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int rate)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = rate / 255.0;
	data.g = rate / 255.0;
	data.b = rate / 255.0;
	data.a = 1.0;
	run_copy_shader(&blend_amap_alpha_bright_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_amap_alpha_src_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int alpha, int rate)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = rate / 255.0;
	data.g = rate / 255.0;
	data.b = rate / 255.0;
	data.a = alpha / 255.0;
	run_copy_shader(&blend_amap_alpha_bright_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_use_amap_color(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int r, int g, int b, int rate)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = 1.0;
	data.threshold = rate / 255.0;
	run_copy_shader(&blend_use_amap_color_shader.s, dst, src, &data);

	restore_blend_mode();

}

void gfx_blend_screen(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_multiply(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_screen_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a)
{
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	data.r = a / 255.0;
	data.g = a / 255.0;
	data.b = a / 255.0;
	data.a = 1.0;
	run_copy_shader(&blend_amap_alpha_bright_shader.s, dst, src, &data);

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

void gfx_fill_amap_over_border(Texture *dst, int x, int y, int w, int h, int alpha, int border)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.a = alpha / 255.0;
	data.threshold = border / 255.0;
	run_fill_shader(&fill_amap_over_border_shader.s, dst, &data);

	restore_blend_mode();
}

void gfx_fill_amap_under_border(Texture *dst, int x, int y, int w, int h, int alpha, int border)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.a = alpha / 255.0;
	data.threshold = border / 255.0;
	run_fill_shader(&fill_amap_under_border_shader.s, dst, &data);

	restore_blend_mode();
}

void gfx_fill_amap_gradation_ud(Texture *dst, int x, int y, int w, int h, int up_a, int down_a)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.threshold = up_a / 255.0;
	data.threshold2 = down_a / 255.0;
	run_fill_shader(&fill_amap_gradation_ud_shader.s, dst, &data);

	restore_blend_mode();
}

void gfx_fill_screen(Texture *dst, int x, int y, int w, int h, int r, int g, int b)
{
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = 1.0;
	run_copy_shader(&fill_shader.s, dst, NULL, &data);

	restore_blend_mode();
}

void gfx_fill_add(Texture *dst, int x, int y, int w, int h, int r, int g, int b)
{
	glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = 1.0;
	run_copy_shader(&fill_shader.s, dst, NULL, &data);

	restore_blend_mode();
}

void gfx_fill_multiply(Texture *dst, int x, int y, int w, int h, int r, int g, int b)
{
	glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = 1.0;
	run_copy_shader(&fill_shader.s, dst, NULL, &data);

	restore_blend_mode();
}

void gfx_satur_dp_dpxsa(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&amap_saturate_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_screen_da_daxsa(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_add_da_daxsa(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	// color = dst_color
	// alpha = src_alpha + dst_alpha
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_blend_da_daxsa(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	// color = dst_color
	// alpha = src_alpha * dst_alpha
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ZERO, GL_SRC_ALPHA);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_sub_da_daxsa(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h)
{
	// color = dst_color
	// alpha = dst_alpha - src_alpha
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_REVERSE_SUBTRACT);
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_bright_dest_only(Texture *dst, int x, int y, int w, int h, int rate)
{
	GLfloat f_rate = rate / 255.0;
	glBlendFuncSeparate(GL_ZERO, GL_CONSTANT_COLOR, GL_ZERO, GL_ONE);
	glBlendColor(f_rate, f_rate, f_rate, 1.0);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	run_copy_shader(&copy_shader.s, dst, NULL, &data);

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

void gfx_copy_stretch_blend(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh, int a)
{
	glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ZERO, GL_ONE);
	glBlendColor(0, 0, 0, a / 255.0);

	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_stretch_blend_screen(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh)
{
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);

	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_stretch_blend_amap(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_stretch_blend_amap_alpha(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh, int a)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	data.r = 1.0;
	data.g = 1.0;
	data.b = 1.0;
	data.a = a / 255.0;
	run_copy_shader(&blend_amap_alpha_bright_shader.s, dst, src, &data);

	restore_blend_mode();
}

// FIXME: this doesn't work correctly when the src rectangle crosses the edge of the CG.
static void copy_rot_zoom(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag, Shader *shader)
{
	if (!dst || !src || dst->w <= 0 || dst->h <= 0 || w <= 0 || h <= 0)
		return;
	if (!gfx_remap_texture(dst) || !gfx_remap_texture(src))
		return;
	w = min(w, src->w - sx);
	h = min(h, src->h - sy);
	// 1. scale src vertices to texture size
	// 2. translate so that center point of copy region is at origin
	// 3. rotate
	// 4. scale
	// 4. tranlate to center of dst texture
	// (applied in reverse order)
	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3){ dst->w/2.0, dst->h/2.0, 0 });
	glm_scale(mw_transform, (vec3){ mag, mag, 0 });
	glm_rotate_z(mw_transform, -rotate * (M_PI/180.0), mw_transform);
	glm_translate(mw_transform, (vec3){ -(sx+w/2.0), -(sy+h/2.0), 0 });
	glm_scale(mw_transform, (vec3){ src->w, src->h, 1 });

	mat4 wv_transform = WV_TRANSFORM(dst->w, dst->h);

	struct copy_data data = ROTATE_DATA(dst, sx, sy, w, h);
	run_draw_shader(shader, dst, src, mw_transform, wv_transform, &data);
}

void gfx_copy_rot_zoom(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag)
{
	gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 0);
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

void gfx_copy_rot_zoom2(Texture *dst, float cx, float cy, Texture *src, float scx, float scy, float rot, float mag)
{
	if (!dst || !src || dst->w <= 0 || dst->h <= 0)
		return;
	gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 0);
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

	// 1. scale src vertices to texture size
	// 2. translate so that center point of copy region is at origin
	// 3. rotate
	// 4. scale
	// 4. tranlate to center point of dst texture
	// (applied in reverse order)
	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3){ cx, cy, 0 });
	glm_scale(mw_transform, (vec3){ mag, mag, 0 });
	glm_rotate_z(mw_transform, -rot * (M_PI/180.0), mw_transform);
	glm_translate(mw_transform, (vec3){ -scx, -scy, 0 });
	glm_scale(mw_transform, (vec3){ src->w, src->h, 1 });

	mat4 wv_transform = WV_TRANSFORM(dst->w, dst->h);

	struct copy_data data = ROTATE_DATA(dst, 0, 0, src->w, src->h);
	run_draw_shader(&hitbox_shader.s, dst, src, mw_transform, wv_transform, &data);
	restore_blend_mode();
}

static void copy_rotate_y(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag, Shader *shader)
{
	if (!dst || !front || !back || dst->w <= 0 || dst->h <= 0)
		return;
	Texture *src = front;
	vec3 scale = { src->w * mag, src->h * mag, 0 };
	if (rot > 90.0 && rot <= 270.0) {
		src = back;
		scale[0] *= -1.0f;
	}

	gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 0);

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_rotate_y(mw_transform, rot * (M_PI/180.0), mw_transform);
	glm_scale(mw_transform, scale);
	glm_translate(mw_transform, (vec3){ -0.5, -0.5, 0});

	mat4 proj_transform;
	float fov = 45.0 * (M_PI / 180.0);
	// calculate the camera distance such that the height at the pivot is unchanged
	float cam_off = ((float)dst->h * 0.5) / tanf(fov * 0.5);
	glm_perspective(fov, (float)dst->w / dst->h, 0.1, cam_off + w * mag, proj_transform);
	glm_translate(proj_transform, (vec3){ 0, 0, -cam_off });

	struct copy_data data = ROTATE_DATA(dst, sx, sy, w, h);
	run_draw_shader(shader, dst, src, mw_transform, proj_transform, &data);
}

void gfx_copy_rotate_y(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag)
{
	copy_rotate_y(dst, front, back, sx, sy, w, h, rot, mag, &hitbox_noblend_shader.s);
}

void gfx_copy_rotate_y_use_amap(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag)
{
	copy_rotate_y(dst, front, back, sx, sy, w, h, rot, mag, &hitbox_shader.s);
}

static void copy_rotate_x(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag, Shader *shader)
{
	if (!dst || !front || !back || dst->w <= 0 || dst->h <= 0)
		return;
	Texture *src = front;
	float flip_y = 1.0;
	rot = 360.0 - rot;
	if (rot > 90.0 && rot <= 270.0) {
		src = back;
		flip_y = -1.0;
	}


	gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 0);

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_rotate_x(mw_transform, rot * (M_PI/180.0), mw_transform);
	glm_scale(mw_transform, (vec3){ src->w * mag, src->h * mag * flip_y, 0 });
	glm_translate(mw_transform, (vec3){ -0.5, -0.5, 0});

	mat4 proj_transform;
	float fov = 22.5 * (M_PI / 180.0);
	// calculate the camera distance such that the height at the pivot is unchanged
	float cam_off = ((float)dst->h * 0.5) / tanf(fov * 0.5);
	glm_perspective(fov, (float)dst->w / dst->h, 0.1, cam_off + w * mag, proj_transform);
	glm_translate(proj_transform, (vec3){ 0, 0, -cam_off });

	struct copy_data data = ROTATE_DATA(dst, sx, sy, w, h);
	run_draw_shader(shader, dst, src, mw_transform, proj_transform, &data);
}

void gfx_copy_rotate_x(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag)
{
	copy_rotate_x(dst, front, back, sx, sy, w, h, rot, mag, &hitbox_noblend_shader.s);
}

void gfx_copy_rotate_x_use_amap(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag)
{
	copy_rotate_x(dst, front, back, sx, sy, w, h, rot, mag, &hitbox_shader.s);
}

void gfx_copy_reverse_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;
	mat4 mw_transform = MAT4(
	     -src->w, 0,      0, sx + w,
	     0,       src->h, 0, -sy,
	     0,       0,      1, 0,
	     0,       0,      0, 1);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}

void gfx_copy_reverse_UD(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;
	mat4 mw_transform = MAT4(
	     src->w, 0,       0, -sx,
	     0,      -src->h, 0, sy + h,
	     0,      0,       1, 0,
	     0,      0,       0, 1);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}

void gfx_copy_reverse_amap_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;
	mat4 mw_transform = MAT4(
	     -src->w, 0,      0, sx + w,
	     0,       src->h, 0, -sy,
	     0,       0,      1, 0,
	     0,       0,      0, 1);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}

void gfx_copy_reverse_amap_UD(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;
	mat4 mw_transform = MAT4(
	     src->w, 0,       0, -sx,
	     0,      -src->h, 0, sy + h,
	     0,      0,       1, 0,
	     0,      0,       0, 1);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}

void gfx_copy_reverse_LR_with_alpha_map(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;
	mat4 mw_transform = MAT4(
	     -src->w, 0,      0, sx + w,
	     0,       src->h, 0, -sy,
	     0,       0,      1, 0,
	     0,       0,      0, 1);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}

static void run_box_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy,
		int w, int h, int blur, int blur_dx, int blur_dy, bool amap)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;
	if ((!gfx_remap_texture(dst) && !dst->handle) || (!gfx_remap_texture(src) && !src->handle))
		return;

	Texture tmp = {0};
	Texture *blur_src = src;
	int blur_sx = sx;
	int blur_sy = sy;
	if (src->handle == dst->handle) {
		int64_t cx0 = max((int64_t)sx, (int64_t)0);
		int64_t cy0 = max((int64_t)sy, (int64_t)0);
		int64_t cx1 = min((int64_t)sx + w, (int64_t)src->w);
		int64_t cy1 = min((int64_t)sy + h, (int64_t)src->h);
		if (cx1 <= cx0 || cy1 <= cy0)
			return;
		int cw = (int)(cx1 - cx0);
		int ch = (int)(cy1 - cy0);
		gfx_init_texture_blank(&tmp, cw, ch);
		gfx_copy_with_alpha_map(&tmp, 0, 0, src, (int)cx0, (int)cy0, cw, ch);
		blur_src = &tmp;
		blur_sx = sx - (int)cx0;
		blur_sy = sy - (int)cy0;
	}

	if (blur <= 0) {
		if (amap)
			gfx_copy_amap(dst, dx, dy, blur_src, blur_sx, blur_sy, w, h);
		else
			gfx_copy(dst, dx, dy, blur_src, blur_sx, blur_sy, w, h);
		if (tmp.handle)
			gfx_delete_texture(&tmp);
		return;
	}

	if (amap)
		glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
	else
		glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, blur_sx, blur_sy, w, h);
	data.blur_dx = blur_dx;
	data.blur_dy = blur_dy;
	data.blur_radius = blur;
	data.blur_mode = 0;
	run_copy_shader(&box_blur_shader.s, dst, blur_src, &data);

	if (tmp.handle)
		gfx_delete_texture(&tmp);
	restore_blend_mode();
}

void gfx_copy_width_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur)
{
	run_box_blur(dst, dx, dy, src, sx, sy, w, h, blur, 1, 0, false);
}

void gfx_copy_height_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur)
{
	run_box_blur(dst, dx, dy, src, sx, sy, w, h, blur, 0, 1, false);
}

void gfx_copy_amap_width_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur)
{
	run_box_blur(dst, dx, dy, src, sx, sy, w, h, blur, 1, 0, true);
}

void gfx_copy_amap_height_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur)
{
	run_box_blur(dst, dx, dy, src, sx, sy, w, h, blur, 0, 1, true);
}

void gfx_blur_rgb_to_fresh(Texture *dst, Texture *src, int blur, bool vertical)
{
	if (!dst || !src || src->w <= 0 || src->h <= 0 || dst->w != src->w || dst->h != src->h)
		return;
	if ((!gfx_remap_texture(dst) && !dst->handle) || (!gfx_remap_texture(src) && !src->handle))
		return;
	if (src->handle == dst->handle) {
		run_box_blur(dst, 0, 0, src, 0, 0, src->w, src->h, blur, vertical ? 0 : 1, vertical ? 1 : 0, false);
		return;
	}
	if (blur <= 0) {
		gfx_copy_with_alpha_map(dst, 0, 0, src, 0, 0, src->w, src->h);
		return;
	}
	GLboolean was_blend = glIsEnabled(GL_BLEND);
	if (was_blend)
		glDisable(GL_BLEND);
	struct copy_data data = COPY_DATA(0, 0, 0, 0, src->w, src->h);
	data.blur_dx = vertical ? 0 : 1;
	data.blur_dy = vertical ? 1 : 0;
	data.blur_radius = blur;
	data.blur_mode = 1;
	run_copy_shader(&box_blur_shader.s, dst, src, &data);
	if (was_blend)
		glEnable(GL_BLEND);
}

void gfx_copy_with_alpha_map(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_fill_with_alpha(Texture *dst, int x, int y, int w, int h, int r, int g, int b, int a)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(x, y, 0, 0, w, h);
	data.r = r / 255.0;
	data.g = g / 255.0;
	data.b = b / 255.0;
	data.a = a / 255.0;
	run_copy_shader(&fill_shader.s, dst, NULL, &data);

	restore_blend_mode();
}

void gfx_copy_stretch_with_alpha_map(Texture *dst, int dx, int dy, int dw, int dh, Texture *src, int sx, int sy, int sw, int sh)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	run_copy_shader(&copy_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_grayscale(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_grayscale_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_grayscale_with_alpha_map(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_grayscale_shader.s, dst, src, &data);

	restore_blend_mode();
}

void gfx_copy_grayscale_reverse_LR_with_alpha_map(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;
	mat4 mw_transform = MAT4(
	     -src->w, 0,      0, sx + w,
	     0,       src->h, 0, -sy,
	     0,       0,      1, 0,
	     0,       0,      0, 1);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_draw_shader(&copy_grayscale_shader.s, dst, src, mw_transform, wv_transform, &data);

	restore_blend_mode();
}

static void draw_line(Texture *dst, int x0, int y0, int x1, int y1, struct copy_data *data)
{
	if (!dst || dst->w <= 0 || dst->h <= 0)
		return;
	GLfloat w = dst->w;
	GLfloat h = dst->h;

	mat4 mw_transform = MAT4(
	     x1 - x0, 0,       0, x0,
	     0,       y1 - y0, 0, y0,
	     0,       0,       1, 0,
	     0,       0,       0, 1);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, 0, 0, w, h);

	struct gfx_render_job job = {
		.shader = &fill_shader.s,
		.shape = GFX_LINE,
		.texture = 0,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = data
	};
	gfx_render(&job);

	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

void gfx_draw_line(Texture *dst, int x0, int y0, int x1, int y1, int r, int g, int b)
{
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);

	struct copy_data data = {
		.r = r / 255.0,
		.g = g / 255.0,
		.b = b / 255.0,
		.a = 1
	};
	draw_line(dst, x0, y0, x1, y1, &data);

	restore_blend_mode();
}

void gfx_draw_line_to_amap(Texture *dst, int x0, int y0, int x1, int y1, int a)
{
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);

	struct copy_data data = { .a = a / 255.0 };
	draw_line(dst, x0, y0, x1, y1, &data);

	restore_blend_mode();
}

void gfx_draw_quadrilateral(Texture *dst, Texture *src, struct gfx_vertex vertices[4])
{
	if (!dst || !src)
		return;
	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, 0, 0, dst->w, dst->h);
	gfx_render_quadrilateral(src, vertices);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	restore_blend_mode();
}

void gfx_blend_quadrilateral(Texture *dst, Texture *src, struct gfx_vertex vertices[4], int a)
{
	if (!dst || !src)
		return;
	glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ZERO, GL_ONE);
	glBlendColor(0, 0, 0, a / 255.0);
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, 0, 0, dst->w, dst->h);
	gfx_render_quadrilateral(src, vertices);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	restore_blend_mode();
}

void gfx_blend_amap_quadrilateral(Texture *dst, Texture *src, struct gfx_vertex vertices[4])
{
	if (!dst || !src)
		return;
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, 0, 0, dst->w, dst->h);
	gfx_render_quadrilateral(src, vertices);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	restore_blend_mode();
}

void gfx_blend_amap_alpha_quadrilateral(Texture *dst, Texture *src, struct gfx_vertex vertices[4], int a)
{
	if (!dst || !src)
		return;
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	struct copy_data data = { .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = a / 255.0f };
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, 0, 0, dst->w, dst->h);
	_gfx_render_quadrilateral(&blend_amap_alpha_bright_shader.s, src, vertices, &data);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	restore_blend_mode();
}

// XXX: Not an actual DrawGraph function; used for rendering text
void gfx_draw_glyph_mode(Texture *dst, float dx, int dy, Texture *glyph, SDL_Color color, float scale_x, float bold_width, enum glyph_blend_mode mode)
{
	switch (mode) {
	case GLYPH_BLEND:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		break;
	case GLYPH_COPY:
		glBlendFunc(GL_ONE, GL_ZERO);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
		break;
	case GLYPH_BLEND_ALPHA:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
		break;
	case GLYPH_ALPHA_ONLY:
		glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ONE);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
		break;
	}

	dx = roundf(dx);
	struct copy_data data = STRETCH_DATA(
			dx, dy, glyph->w * scale_x, glyph->h,
			0,  0,  glyph->w,           glyph->h);
	data.r = color.r / 255.0;
	data.g = color.g / 255.0;
	data.b = color.b / 255.0;
	data.a = 0.01;  // discard threshold

	if (bold_width < 0.01) {
		run_copy_shader(&blend_rmap_color_shader.s, dst, glyph, &data);
	} else {
		data.threshold = bold_width;
		run_copy_shader(&dilate_shader.s, dst, glyph, &data);
	}

	restore_blend_mode();
}

void gfx_draw_glyph(Texture *dst, float dx, int dy, Texture *glyph, SDL_Color color, float scale_x, float bold_width, bool blend)
{
	gfx_draw_glyph_mode(dst, dx, dy, glyph, color, scale_x, bold_width,
			blend ? GLYPH_BLEND : GLYPH_COPY);
}

void gfx_draw_glyph_to_pmap(Texture *dst, float dx, int dy, Texture *glyph, Rectangle glyph_pos, SDL_Color color, float scale_x)
{
	struct copy_data data = STRETCH_DATA(
			dx,          dy,          glyph_pos.w * scale_x, glyph_pos.h,
			glyph_pos.x, glyph_pos.y, glyph_pos.w,           glyph_pos.h);
	data.r = color.r / 255.0;
	data.g = color.g / 255.0;
	data.b = color.b / 255.0;
	data.a = 0.01;  // discard threshold
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	run_copy_shader(&blend_rmap_color_shader.s, dst, glyph, &data);
	restore_blend_mode();
}

void gfx_draw_glyph_to_amap(Texture *dst, float dx, int dy, Texture *glyph, Rectangle glyph_pos, float scale_x)
{
	struct copy_data data = STRETCH_DATA(
			dx,          dy,          glyph_pos.w * scale_x, glyph_pos.h,
			glyph_pos.x, glyph_pos.y, glyph_pos.w,           glyph_pos.h);
	data.r = 1.0;
	data.g = 1.0;
	data.b = 1.0;
	data.a = 0.0;  // discard threshold
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
	run_copy_shader(&blend_rmap_color_shader.s, dst, glyph, &data);
	restore_blend_mode();
}

void gfx_copy_stretch_blend_straight_alpha(Texture *dst, int dx, int dy, int dw, int dh,
		Texture *src, int sx, int sy, int sw, int sh)
{
	if (!dst || !dst->handle || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
		return;

	Texture under = {0};
	if (dx < 0 || dy < 0 ||
			(int64_t)dx + dw > dst->w ||
			(int64_t)dy + dh > dst->h) {
		gfx_init_texture_rgba(&under, dw, dh, (SDL_Color){0, 0, 0, 0});
	} else {
		gfx_init_texture_blank(&under, dw, dh);
	}
	gfx_copy_with_alpha_map(&under, 0, 0, dst, dx, dy, dw, dh);

	glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
	struct copy_data data = STRETCH_DATA(dx, dy, dw, dh, sx, sy, sw, sh);
	data.dst_texture = under.handle;
	run_copy_shader(&copy_straight_alpha_shader, dst, src, &data);
	gfx_delete_texture(&under);
	restore_blend_mode();
}

void gfx_blend_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h)
{
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	struct copy_data data = COPY_DATA(dx, dy, sx, sy, w, h);
	run_copy_shader(&copy_shader.s, dst, src, &data);
	restore_blend_mode();
}

void gfx_draw_shape_mask(Texture *dst, int dx, int dy, int w, int h, const uint8_t *cov, SDL_Color color, int mode)
{
	if (!dst || (!gfx_remap_texture(dst) && !dst->handle) || w <= 0 || h <= 0 || !cov)
		return;
	size_t n = (size_t)w * h;
	SDL_Color *pixels = xmalloc(n * sizeof(SDL_Color));
	if (mode == 1 || mode == 2) {
		for (size_t i = 0; i < n; i++) {
			uint8_t c = cov[i];
			pixels[i] = (SDL_Color){ c, c, c, c };
		}
	} else if (mode == 3 || mode == 4) {
		for (size_t i = 0; i < n; i++) {
			uint8_t eff = (uint8_t)((cov[i] * (int)color.a + 127) / 255);
			pixels[i] = (SDL_Color){ color.r, color.g, color.b, eff };
		}
	} else {
		for (size_t i = 0; i < n; i++) {
			pixels[i] = (SDL_Color){ color.r, color.g, color.b, cov[i] };
		}
	}

	Texture src;
	gfx_init_texture_with_pixels(&src, w, h, pixels);
	free(pixels);

	struct copy_data data = COPY_DATA(dx, dy, 0, 0, w, h);
	if (mode == 1) {
		// WithAlpha: dst.rgba = mix(dst.rgba, color.rgba, cov)
		glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
		run_copy_shader(&copy_shader.s, dst, &src, &data);
		if (color.r || color.g || color.b || color.a) {
			glBlendColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
			glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE, GL_CONSTANT_ALPHA, GL_ONE);
			run_copy_shader(&copy_shader.s, dst, &src, &data);
		}
	} else if (mode == 2) {
		// AMap: dst.a = mix(dst.a, color.a, cov), dst.rgb unchanged
		glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
		run_copy_shader(&copy_shader.s, dst, &src, &data);
		if (color.a) {
			glBlendColor(0.0f, 0.0f, 0.0f, color.a / 255.0f);
			glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_CONSTANT_ALPHA, GL_ONE);
			run_copy_shader(&copy_shader.s, dst, &src, &data);
		}
	} else if (mode == 4) {
		// AlphaBlend: Porter-Duff src-over
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		run_copy_shader(&copy_shader.s, dst, &src, &data);
	} else {
		// Mode 0 (RGB) & Mode 3 (Blend): blend RGB, preserve dst.a
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		run_copy_shader(&copy_shader.s, dst, &src, &data);
	}

	restore_blend_mode();
	gfx_delete_texture(&src);
}
