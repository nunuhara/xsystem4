/* Copyright (C) 2000- Fumihiko Murata <fmurata@p1.tcnet.ne.jp>
 *               2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdbool.h>
#include <SDL.h>
#include "system4.h"
#include "graphics.h"
#include "sdl_core.h"
#include "sdl_private.h"
#include "cg.h"

bool sdl_dirty = false;

void sdl_update_screen(void)
{
	if (!sdl.dirty)
		return;
	SDL_UpdateTexture(sdl.texture, NULL, sdl.display->pixels, sdl.display->pitch);
	SDL_RenderClear(sdl.renderer);
	SDL_RenderCopy(sdl.renderer, sdl.texture, NULL, NULL);
	SDL_RenderPresent(sdl.renderer);
	sdl.dirty = false;
}

SDL_Surface *sdl_make_rectangle(int w, int h, SDL_Color *color)
{
	Rectangle rect = { .x = 0, .y = 0, .w = w, .h = w };
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB32);
	uint32_t c = SDL_MapRGBA(s->format, color->r, color->g, color->b, color->a);
	SDL_FillRect(s, &rect, c);
	return s;
}

void sdl_draw_rectangle(Rectangle *_rect, int pixels, uint8_t r, uint8_t g, uint8_t b)
{
	SDL_Rect  rect[4];

	// top
	rect[0] = *_rect;
	rect[0].h = pixels;
	// bot
	rect[1] = *_rect;
	rect[1].y += _rect->h - pixels;
	rect[1].h = pixels;
	// left
	rect[2] = *_rect;
	rect[2].w = pixels;
	// right
	rect[3] = *_rect;
	rect[3].x += _rect->w - pixels;
	rect[3].w = pixels;

	uint32_t c = SDL_MapRGB(sdl.display->format, r, g, b);
	SDL_FillRects(sdl.display, rect, 4, c);
	sdl.dirty = true;
}

void sdl_draw_cg(struct cg *cg, Rectangle *dst)
{
	Rectangle src = { .x = 0, .y = 0, .w = cg->s->w, .h = cg->s->h };
	SDL_BlitSurface(cg->s, &src, sdl.display, dst);
	sdl.dirty = true;
}
