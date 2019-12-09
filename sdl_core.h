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

#ifndef SYSTEM4_SDL_CORE_H
#define SYSTEM4_SDL_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>
#include "graphics.h"

struct cg;

int sdl_initialize(void);
void sdl_remove(void);

void sdl_set_window_size(int w, int h);
void sdl_fullscreen(bool on);

// surface/texture creation
SDL_Texture *sdl_create_texture(int w, int h);
void sdl_update_texture(SDL_Texture *t, SDL_Surface *s);
SDL_Texture *sdl_surface_to_texture(SDL_Surface *s);
SDL_Surface *sdl_create_surface(int w, int h, SDL_Color *color);

// rendering
void sdl_render_clear(void);
void sdl_render_present(void);
void sdl_render_texture(SDL_Texture *t, Rectangle *r);

// drawing
void sdl_copy(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);
void sdl_copy_bright(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int rate);
void sdl_copy_amap(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);
void sdl_copy_sprite(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int r, int g, int b);
void sdl_copy_use_amap_under(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int a_threshold);
void sdl_copy_use_amap_border(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int a_threshold);
void sdl_copy_amap_max(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);
void sdl_copy_amap_min(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);
void sdl_blend_amap(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);
void sdl_blend_amap_alpha(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int alpha);
void sdl_fill(struct cg *dst, int x, int y, int w, int h, int r, int g, int b);
void sdl_fill_alpha_color(struct cg *dst, int x, int y, int w, int h, int r, int g, int b, int a);
void sdl_fill_amap(struct cg *dst, int x, int y, int w, int h, int a);
void sdl_add_da_daxsa(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);
void sdl_copy_stretch(struct cg *dst, int dx, int dy, int dw, int dh, struct cg *src, int sx, int sy, int sw, int sh);
void sdl_copy_stretch_amap(struct cg *dst, int dx, int dy, int dw, int dh, struct cg *src, int sx, int sy, int sw, int sh);
void sdl_copy_reverse_LR(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);
void sdl_copy_reverse_amap_LR(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h);

static inline uint32_t *sdl_get_pixel(SDL_Surface *s, int x, int y)
{
	return (uint32_t*)(((uint8_t*)s->pixels) + s->pitch*y + s->format->BytesPerPixel*x);
}

enum font_weight {
	FW_NORMAL = 400,
	FW_BOLD   = 700,
	FW_NORMAL2 = 1400,
	FW_BOLD2   = 1700
};

enum font_face {
	FONT_GOTHIC = 0,
	FONT_MINCHO = 1
};

struct text_metrics {
	SDL_Color color;
	SDL_Color outline_color;
	unsigned int size;
	enum font_weight weight;
	enum font_face face;
	int outline_left;
	int outline_up;
	int outline_right;
	int outline_down;
};

void sdl_text_init(void);
bool sdl_set_font(int face, unsigned int size);
int sdl_render_text(SDL_Surface *dst, Point pos, char *msg, struct text_metrics *tm);

#endif /* SYSTEM4_SDL_CORE_H */
