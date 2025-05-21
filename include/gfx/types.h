/* Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
 *               1998-                           <masaki-c@is.aist-nara.ac.jp>
 *               2019 Nunuhara Cabbage           <nunuhara@haniwa.technology>
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

#ifndef SYSTEM4_GRAPHICS_H
#define SYSTEM4_GRAPHICS_H

#include <stdint.h>
#include <SDL.h>
#include <cglm/types.h>

typedef SDL_Color Color;

#define COLOR(_r, _g, _b, _a) ((Color){.r=_r, .g=_g, .b=_b, .a=_a})

typedef SDL_Point Point;

#define POINT(_x, _y) ((Point){.x=_x, .y=_y})

typedef struct {
        int w;
        int h;
} Dimension;

#define DIM(_w, _h) ((Dimension){.w=_w, .h=_h})

typedef SDL_Rect Rectangle;

#define RECT(_x, _y, _w, _h) ((Rectangle){.x=_x, .y=_y, .w=_w, .h=_h})

#define MAT4(r0c0, r0c1, r0c2, r0c3, r1c0, r1c1, r1c2, r1c3, r2c0, r2c1, r2c2, r2c3, r3c0, r3c1, r3c2, r3c3) \
	{								\
		{ r0c0, r1c0, r2c0, r3c0 },				\
		{ r0c1, r1c1, r2c1, r3c1 },				\
		{ r0c2, r1c2, r2c2, r3c2 },				\
		{ r0c3, r1c3, r2c3, r3c3 }				\
	}

#define WV_TRANSFORM(w, h)			\
	MAT4(2.0 / w, 0,       0, -1,		\
	     0,       2.0 / h, 0, -1,		\
	     0,       0,       1, -1,		\
	     0,       0,       0,  1)

#define WORLD_TRANSFORM(w, h, x, y)	\
	MAT4(w, 0, 0, x,		\
	     0, h, 0, y,		\
	     0, 0, 1, 0,		\
	     0, 0, 0, 1)

#endif /* SYSTEM4_GRAPHICS_H */
