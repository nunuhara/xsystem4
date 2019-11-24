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

void sdl_set_window_size(int x, int y, int w, int h);
void sdl_fullscreen(bool on);

void sdl_update_screen(void);
void sdl_draw_surface(SDL_Surface *s, Rectangle *dst);
SDL_Surface *sdl_make_rectangle(int w, int h, SDL_Color *color);

enum font_weight {
	FW_NORMAL = 400,
	FW_BOLD   = 700
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
