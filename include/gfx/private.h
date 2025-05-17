/* gfx/private.h  SDL/OpenGL only private data
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

#ifndef SYSTEM4_GFX_PRIVATE_H
#define SYSTEM4_GFX_PRIVATE_H

#include <stdbool.h>
#include <SDL.h>
#include "gfx/gl.h"

struct sdl_private {
	SDL_Window *window;
	SDL_PixelFormat *format;
	struct {
		SDL_GLContext context;
		GLuint vao;
		GLuint vbo;
		GLuint quad_vbo;
		GLuint rect_ibo;
		GLuint line_ibo;
	} gl;
	int w, h;
	SDL_Rect viewport;
	bool ms_active; /* mouse is active */
};
extern struct sdl_private sdl;

#endif /* SYSTEM4_GFX_PRIVATE_H */
