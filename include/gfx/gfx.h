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
	int w, h;
} Texture;

struct gfx_render_job;

typedef struct shader {
	GLuint program;
	// variables
	GLuint world_transform;
	GLuint view_transform;
	GLuint texture;
	GLuint vertex_pos;
	GLuint vertex_uv;
	void (*prepare)(struct gfx_render_job *, void *);
} Shader;

enum gfx_shape {
	GFX_RECTANGLE,
	GFX_LINE,
	GFX_QUADRILATERAL,
};

struct gfx_render_job {
	struct shader *shader;
	enum gfx_shape shape;
	GLuint texture;
	GLfloat *world_transform;
	GLfloat *view_transform;
	void *data;
};

struct gfx_vertex {
	GLfloat x, y, z, w;
	GLfloat u, v;
};

int gfx_init(void);
void gfx_fini(void);

Texture *gfx_main_surface(void);
void gfx_set_window_logical_size(int w, int h);
void gfx_update_screen_scale(void);
void gfx_set_wait_vsync(bool wait);
float gfx_get_frame_rate(void);

void gfx_load_shader(struct shader *dst, const char *vertex_shader_path, const char *fragment_shader_path);
GLuint gfx_load_shader_file(const char *path, GLenum type, const char *defines);

// rendering
void gfx_set_clear_color(int r, int g, int b, int a);
void gfx_set_view_offset(int x, int y);
void gfx_clear(void);
void gfx_swap(void);
void gfx_set_view(struct texture *t);
void gfx_reset_view(void);
void gfx_prepare_job(struct gfx_render_job *job);
void gfx_run_job(struct gfx_render_job *job);
void gfx_render(struct gfx_render_job *job);
void _gfx_render_texture(struct shader *s, struct texture *t, Rectangle *r, void *data);
void gfx_render_texture(struct texture *t, Rectangle *r);
void gfx_render_quadrilateral(struct texture *t, struct gfx_vertex vertices[4]);

// texture management
void gfx_init_texture_blank(struct texture *t, int w, int h);
void gfx_init_texture_with_cg(struct texture *t, struct cg *cg);
void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color);
void gfx_init_texture_rgb(struct texture *t, int w, int h, SDL_Color color);
void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels);
void gfx_init_texture_amap(struct texture *t, int w, int h, uint8_t *amap, SDL_Color color);
void gfx_init_texture_rmap(struct texture *t, int w, int h, uint8_t *rmap);
void gfx_update_texture_with_pixels(struct texture *t, void *pixels);
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
void gfx_blend_amap_color_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int r, int g, int b, int a);
void gfx_blend_amap_alpha(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h, int a);
void gfx_blend_amap_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int rate);
void gfx_blend_amap_alpha_src_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int alpha, int rate);
void gfx_blend_use_amap_color(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int r, int g, int b, int rate);
void gfx_blend_screen(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_blend_multiply(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_blend_screen_alpha(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int a);
void gfx_fill(struct texture *dst, int x, int y, int w, int h, int r, int g, int b);
void gfx_fill_alpha_color(struct texture *dst, int x, int y, int w, int h, int r, int g, int b, int a);
void gfx_fill_amap(struct texture *dst, int x, int y, int w, int h, int a);
void gfx_fill_amap_over_border(Texture *dst, int x, int y, int w, int h, int alpha, int border);
void gfx_fill_amap_under_border(Texture *dst, int x, int y, int w, int h, int alpha, int border);
void gfx_fill_amap_gradation_ud(Texture *dst, int x, int y, int w, int h, int up_a, int down_a);
void gfx_fill_screen(Texture *dst, int x, int y, int w, int h, int r, int g, int b);
void gfx_fill_multiply(Texture *dst, int x, int y, int w, int h, int r, int g, int b);
void gfx_satur_dp_dpxsa(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_screen_da_daxsa(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_add_da_daxsa(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_blend_da_daxsa(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_sub_da_daxsa(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h);
void gfx_bright_dest_only(Texture *dst, int x, int y, int w, int h, int rate);
void gfx_copy_stretch(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh);
void gfx_copy_stretch_amap(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh);
void gfx_copy_stretch_blend(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh, int a);
void gfx_copy_stretch_blend_screen(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh);
void gfx_copy_stretch_blend_amap(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh);
void gfx_copy_stretch_blend_amap_alpha(struct texture *dst, int dx, int dy, int dw, int dh, struct texture *src, int sx, int sy, int sw, int sh, int a);
void gfx_copy_rot_zoom(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag);
void gfx_copy_rot_zoom_amap(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag);
void gfx_copy_rot_zoom_use_amap(Texture *dst, Texture *src, int sx, int sy, int w, int h, float rotate, float mag);
void gfx_copy_rot_zoom2(Texture *dst, float cx, float cy, Texture *src, float scx, float scy, float rot, float mag);
void gfx_copy_reverse_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_copy_reverse_UD(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_copy_reverse_amap_LR(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_copy_reverse_LR_with_alpha_map(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
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
void gfx_copy_grayscale(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h);
void gfx_draw_line(Texture *dst, int x0, int y0, int x1, int y1, int r, int g, int b);
void gfx_draw_line_to_amap(Texture *dst, int x0, int y0, int x1, int y1, int a);
void gfx_draw_glyph(Texture *dst, float dx, int dy, Texture *glyph, SDL_Color color, float scale_x, float bold_width, bool blend);
void gfx_draw_glyph_to_pmap(Texture *dst, float dx, int dy, Texture *glyph, Rectangle glyph_pos, SDL_Color color, float scale_x);
void gfx_draw_glyph_to_amap(Texture *dst, float dx, int dy, Texture *glyph, Rectangle glyph_pos, float scale_x);
void gfx_draw_quadrilateral(Texture *dst, Texture *src, struct gfx_vertex vertices[4]);

#endif /* SYSTEM4_SDL_CORE_H */
