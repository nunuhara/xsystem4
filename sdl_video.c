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
#include "sdl_core.h"
#include "sdl_private.h"

#define PIXEL_FORMAT SDL_PIXELFORMAT_ARGB32

int sdl_initialize(void)
{
	memset(&sdl, 0, sizeof(struct sdl_private_data));

	// make toplevel window
	SDL_Init(SDL_INIT_VIDEO);
	sdl.format = SDL_AllocFormat(PIXEL_FORMAT);
	sdl.window = SDL_CreateWindow("XSystem4",
				      SDL_WINDOWPOS_UNDEFINED,
				      SDL_WINDOWPOS_UNDEFINED,
				      config.view_width,
				      config.view_height,
				      SDL_WINDOW_RESIZABLE);

	sdl.renderer = SDL_CreateRenderer(sdl.window, -1, 0);
	sdl_set_window_size(config.view_width, config.view_height);
	atexit(sdl_remove);
	return 0;
}

void sdl_remove(void)
{
	if (!sdl.window)
		return;

	SDL_DestroyRenderer(sdl.renderer);
	SDL_FreeFormat(sdl.format);
	SDL_Quit();
	sdl.window = NULL;
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

void sdl_set_window_size(int w, int h)
{
	if (w == sdl.w && h == sdl.h)
		return;

	sdl.w = w;
	sdl.h = h;

	SDL_SetWindowSize(sdl.window, w, h);
	SDL_RenderSetLogicalSize(sdl.renderer, w, h);
}

SDL_Texture *sdl_create_texture(int w, int h)
{
	SDL_Texture *t = SDL_CreateTexture(sdl.renderer, PIXEL_FORMAT, SDL_TEXTUREACCESS_STATIC, w, h);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

SDL_Surface *sdl_create_surface(int w, int h, SDL_Color *color)
{
	Rectangle rect = { .x = 0, .y = 0, .w = w, .h = w };
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB32);
	if (color) {
		uint32_t c = SDL_MapRGBA(s->format, color->r, color->g, color->b, color->a);
		SDL_FillRect(s, &rect, c);
	}
	return s;
}

void sdl_update_texture(SDL_Texture *t, SDL_Surface *s)
{
	if (SDL_MUSTLOCK(s))
		SDL_LockSurface(s);
	SDL_UpdateTexture(t, NULL, s->pixels, s->pitch);
	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
}

SDL_Texture *sdl_surface_to_texture(SDL_Surface *s)
{
	SDL_Texture *t = SDL_CreateTexture(sdl.renderer, PIXEL_FORMAT, SDL_TEXTUREACCESS_STATIC, s->w, s->h);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_UpdateTexture(t, NULL, s->pixels, s->pitch);
	return t;
}

void sdl_render_clear(void)
{
	SDL_RenderClear(sdl.renderer);
}

void sdl_render_present(void)
{
	SDL_RenderPresent(sdl.renderer);
}

void sdl_render_texture(SDL_Texture *t, Rectangle *r)
{
	SDL_RenderCopy(sdl.renderer, t, NULL, r);
}
