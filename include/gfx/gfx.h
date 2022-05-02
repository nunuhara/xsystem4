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

#ifndef SYSTEM4_GFX_CORE_H
#define SYSTEM4_GFX_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>
#include "gfx/gl.h"
#include "gfx/types.h"

struct cg;
struct text_metrics;
enum cg_type;

enum draw_method {
	DRAW_METHOD_NORMAL,
	DRAW_METHOD_SCREEN,
	DRAW_METHOD_MULTIPLY,
	DRAW_METHOD_ADDITIVE,
};

typedef struct texture {
	GLuint handle;
	mat4 world_transform;
	int w, h;
	bool has_alpha;
	int alpha_mod;
	enum draw_method draw_method;
} Texture;

struct gfx_render_job;

typedef struct shader {
	GLuint program;
	// variables
	GLuint world_transform;
	GLuint view_transform;
	GLuint texture;
	GLuint vertex;
	GLuint alpha_mod;
	void (*prepare)(struct gfx_render_job *, void *);
} Shader;

struct gfx_render_job {
	struct shader *shader;
	GLuint texture;
	GLfloat *world_transform;
	GLfloat *view_transform;
	void *data;
};

int gfx_init(void);
void gfx_fini(void);

Texture *gfx_main_surface(void);
void gfx_set_window_logical_size(int w, int h);
void gfx_update_screen_scale(void);
void gfx_set_wait_vsync(bool wait);

void gfx_load_shader(struct shader *dst, const char *vertex_shader_path, const char *fragment_shader_path);

// rendering
void gfx_set_clear_color(int r, int g, int b, int a);
void gfx_clear(void);
void gfx_swap(void);
void gfx_prepare_job(struct gfx_render_job *job);
void gfx_run_job(struct gfx_render_job *job);
void gfx_render(struct gfx_render_job *job);
void gfx_render_texture(struct texture *t, Rectangle *r);

// texture management
void gfx_init_texture_blank(struct texture *t, int w, int h);
void gfx_init_texture_with_cg(struct texture *t, struct cg *cg);
void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color);
void gfx_init_texture_rgb(struct texture *t, int w, int h, SDL_Color color);
void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels);
void gfx_init_texture_amap(struct texture *t, int w, int h, uint8_t *amap, SDL_Color color);
void gfx_copy_main_surface(struct texture *dst);
void gfx_delete_texture(struct texture *t);
GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h);
void gfx_reset_framebuffer(GLenum target, GLuint fbo);
SDL_Color gfx_get_pixel(Texture *t, int x, int y);
void *gfx_get_pixels(Texture *t);
int gfx_save_texture(Texture *t, const char *path, enum cg_type);

// drawing
void gfx_draw_init(void);
void gfx_copy(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_copy_bright(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, int rate);
void gfx_copy_amap(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_copy_sprite(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, SDL_Color color);
void gfx_sprite_copy_amap(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, int alpha_key);
void gfx_copy_color_reverse(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_copy_use_amap_under(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, int threshold);
void gfx_copy_use_amap_border(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, int threshold);
void gfx_copy_amap_max(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_copy_amap_min(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_blend(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a);
void gfx_blend_src_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a, int rate);
void gfx_blend_add_satur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_blend_amap(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_blend_amap_src_only(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_blend_amap_color(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, int r, int g, int b);
void gfx_blend_amap_alpha(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, int a);
void gfx_blend_amap_color_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int r, int g, int b, int a);
void gfx_fill(struct texture *dst, int x, int y, int w, int h, int r, int g, int b);
void gfx_fill_alpha_color(struct texture *dst, int x, int y, int w, int h, int r, int g, int b, int a);
void gfx_fill_amap(struct texture *dst, int x, int y, int w, int h, int a);
void gfx_add_da_daxsa(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_blend_da_daxsa(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_sub_da_daxsa(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_copy_stretch(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh);
void gfx_copy_stretch_amap(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh);
void gfx_copy_stretch_blend(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh, int a);
void gfx_copy_stretch_blend_amap(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh);
void gfx_copy_rot_zoom(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag);
void gfx_copy_rot_zoom_amap(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag);
void gfx_copy_rot_zoom_use_amap(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag);
void gfx_copy_reverse_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_copy_reverse_amap_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_fill_amap_over_border(Texture *dst, int x, int y, int w, int h, int alpha, int border);
void gfx_fill_amap_under_border(Texture *dst, int x, int y, int w, int h, int alpha, int border);
void gfx_copy_rotate_y(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag);
void gfx_copy_rotate_y_use_amap(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag);
void gfx_copy_rotate_x(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag);
void gfx_copy_rotate_x_use_amap(Texture *dst, Texture *front, Texture *back, int sx, int sy, int w, int h, float rot, float mag);
void gfx_copy_width_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur);
void gfx_copy_height_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur);
void gfx_copy_amap_width_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur);
void gfx_copy_amap_height_blur(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int blur);
void gfx_copy_with_alpha_map(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_fill_with_alpha(Texture *dst, int x, int y, int w, int h, int r, int g, int b, int a);
void gfx_copy_stretch_with_alpha_map(Texture *dst, int dx, int dy, int dw, int dh, Texture *src, int sx, int sy, int sw, int sh);
void gfx_draw_glyph(Texture *dst, float dx, int dy, Texture *glyph, SDL_Color color, float scale_x, float bold_width);

enum {
	FW_NORMAL = 400,
	FW_BOLD   = 700,
	FW_NORMAL2 = 1400,
	FW_BOLD2   = 1700
};

enum font_face {
	FONT_GOTHIC = 0,
	FONT_MINCHO = 1
};

extern const char *font_paths[2];

struct text_metrics {
	SDL_Color color;
	SDL_Color outline_color;
	unsigned int size;
	int weight;
	enum font_face face;
	int outline_left;
	int outline_up;
	int outline_right;
	int outline_down;
};

struct font_metrics {
	int size;
	enum font_face face;
	int weight;
	bool underline;
	bool strikeout;
	int space;
	SDL_Color color;
};

void gfx_font_init(void);
bool gfx_set_font(enum font_face face, unsigned int size);

bool gfx_set_font_size(unsigned int size);
bool gfx_set_font_face(enum font_face face);
bool gfx_set_font_weight(int weight);
bool gfx_set_font_underline(bool on);
bool gfx_set_font_strikeout(bool on);
bool gfx_set_font_space(int space);
bool gfx_set_font_color(SDL_Color color);

int gfx_get_font_size(void);
enum font_face gfx_get_font_face(void);
int gfx_get_font_weight(void);
bool gfx_get_font_underline(void);
bool gfx_get_font_strikeout(void);
int gfx_get_font_space(void);
SDL_Color gfx_get_font_color(void);
void gfx_set_font_name(const char *name);

int gfx_render_text(Texture *dst, Point pos, char *msg, struct text_metrics *tm, int char_space);
void gfx_draw_text_to_amap(Texture *dst, int x, int y, char *text);
void gfx_draw_text_to_pmap(Texture *dst, int x, int y, char *text);

struct fnl_font_inst {
	struct fnl_font_face *font;
	float scale;
	SDL_Color color;
	SDL_Color outline_color;
	int outline_size;
};

struct fnl_render_size;

struct text_style {
	unsigned font_type;
	float size;
	float bold_width;
	SDL_Color color;
	float edge_width;
	SDL_Color edge_color;
	float scale_x;
	float space_scale_x;
	float font_spacing;

	struct fnl_render_size *font_size;
};

void fnl_renderer_init(const char *path);
void fnl_renderer_fini(void);
float fnl_draw_text(struct text_style *ts, Texture *dst, float x, int y, char *text);
float fnl_size_text(struct text_style *ts, char *text);
float fnl_get_actual_font_size(int font_type, float size);
float fnl_get_actual_font_size_round_down(int font_type, float size);

#endif /* SYSTEM4_SDL_CORE_H */
