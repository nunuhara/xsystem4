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

#endif /* SYSTEM4_GRAPHICS_H */
