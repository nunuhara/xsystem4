/* sdl_private.h  SDL only private data
 *
 * Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#ifndef SYSTEM4_SDL_PRIVATE_H
#define SYSTEM4_SDL_PRIVATE_H

#include <stdbool.h>
#include <SDL.h>

//#include "ags.h"
//#include "font.h"

struct sdl_private_data {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_PixelFormat *format;
	int w, h;
	//FONT *font;
	bool dirty;
	bool ms_active; /* mouse is active */
	bool fs_on;
};

struct sdl_private_data sdl;

extern void sdl_cursor_init(void);
extern void sdl_shadow_init(void);
extern int sdl_nearest_color(int r, int g, int b);

#define SDL_AllocSurface(flags,w,h,d,r,g,b,a) SDL_CreateRGBSurface(0,w,h,d,r,g,b,a)

#define setRect(r,xx,yy,ww,hh) (r).x=(xx),(r).y=(yy),(r).w=(ww),(r).h=(hh)
#define setOffset(s,x,y) (s->pixels) + (x) * (s->format->BytesPerPixel) + (y) * s->pitch

#endif /* SYSTEM4_SDL_PRIVATE_H */
