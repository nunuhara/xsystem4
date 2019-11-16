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

#include <string.h>
#include <SDL.h>
#include "system4.h"
#include "sdl_private.h"

int sdl_initialize(void)
{
	memset(&sdl, 0, sizeof(struct sdl_private_data));

	// make toplevel window
	SDL_Init(SDL_INIT_VIDEO);
	sdl.window = SDL_CreateWindow("XSystem4",
				      SDL_WINDOWPOS_UNDEFINED,
				      SDL_WINDOWPOS_UNDEFINED,
				      config.view_width,
				      config.view_height,
				      SDL_WINDOW_RESIZABLE);
	sdl.renderer = SDL_CreateRenderer(sdl.window, -1, 0);

	// offscreen pixmap
	//makeDIB(config.view_width, config.view_height, SYS4_DEFAULT_DEPTH);

	// init cursor
	//sdl_cursor_init();

	//sdl_setWindowSize(0, 0, config.view_width, config.view_height);

	//sdl_shadow_init();

	//joy_open();
	return 0;
}

void sdl_remove(void)
{
	if (!sdl.window)
		return;

	//SDL_FreeSurface(sdl.dib);
	SDL_DestroyRenderer(sdl.renderer);
	//joy_close();
	SDL_Quit();
}

void sdl_fullscreen(bool on)
{
	if (on && !sdl.fs_on) {
		sdl.fs_on = true;
		SDL_SetWindowFullscreen(sdl.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	} else if (!on && sdl.fs_on) {
		sdl.fs_on = false;
		SDL_SetWindowFullscreen(sdl.window, 0);
	}
}

void sdl_set_window_size(int x, int y, int w, int h)
{
	sdl.view.x = x;
	sdl.view.y = y;

	if (w == sdl.view.w && h == sdl.view.h)
		return;

	sdl.view.w = w;
	sdl.view.h = h;

	SDL_SetWindowSize(sdl.window, w, h);
	SDL_RenderSetLogicalSize(sdl.renderer, w, h);
	if (sdl.display)
		SDL_FreeSurface(sdl.display);
	if (sdl.texture)
		SDL_DestroyTexture(sdl.texture);
	sdl.display = SDL_CreateRGBSurface(0, w, h, 32, 0, 0, 0, 0);
	sdl.texture = SDL_CreateTexture(sdl.renderer, sdl.display->format->format,
					SDL_TEXTUREACCESS_STATIC, w, h);
}
