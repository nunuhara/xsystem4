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

#include <SDL.h>
#include <SDL_ttf.h>
#include "system4.h"
#include "sdl_core.h"
#include "graphics.h"
#include "utfsjis.h"

#define MAX_FONT_SIZE 128

// TODO: install fonts on system, and provide run-time configuration option
const char *font_paths[] = {
	[FONT_GOTHIC] = "fonts/mincho.otf",
	[FONT_MINCHO] = "fonts/MTLc3m.ttf"
};

struct font {
	unsigned int size;
	enum font_face face;
	enum font_weight weight;
	TTF_Font *font;
};

struct _font {
	unsigned int allocated;
	struct font *fonts;
};

static struct _font font_table[2];

static struct font *font;

bool sdl_set_font(int face, unsigned int size)
{
	struct _font *ff = &font_table[face];

	if (size > MAX_FONT_SIZE) {
		WARNING("Font size too large: %d", size);
		return false;
	}

	// realloc if needed
	if ((size+1) > ff->allocated) {
		ff->fonts = xrealloc(ff->fonts, sizeof(struct font) * (size+1));
		memset(ff->fonts + ff->allocated, 0, sizeof(struct font) * ((size+1) - ff->allocated));
		ff->allocated = size+1;
	}

	// open font if needed
	if (!ff->fonts[size].size) {
		if (!(ff->fonts[size].font = TTF_OpenFont(font_paths[face], size))) {
			WARNING("Error opening font: %s:%d", font_paths[face], size);
			return false;
		}
		ff->fonts[size].size = size;
		ff->fonts[size].face = face;
		ff->fonts[size].weight = FW_NORMAL;
	}

	// set current font
	font = &ff->fonts[size];
	return true;
}

static void _sdl_render_text(TTF_Font *f, SDL_Surface *dst, Point pos, char *msg, SDL_Color color)
{
	SDL_Surface *s = TTF_RenderUTF8_Blended(f, msg, color);
	Rectangle dst_pos = { .x = pos.x, .y = pos.y, .w = s->w, .h = s->h };
	SDL_BlitSurface(s, NULL, dst, &dst_pos);
	SDL_FreeSurface(s);
}

static int sact_to_sdl_fontstyle(int style)
{
	switch (style) {
	case FW_NORMAL: return 0;
	case FW_BOLD:   return TTF_STYLE_BOLD;
	default:        return 0;
	}
}

int sdl_render_text(SDL_Surface *dst, Point pos, char *msg, struct text_metrics *tm)
{
	if (!font)
		return 0;
	if (font->size != tm->size || font->face != tm->face)
		sdl_set_font(tm->face, tm->size);
	if (font->weight != tm->weight) {
		TTF_SetFontStyle(font->font, sact_to_sdl_fontstyle(tm->weight));
		font->weight = tm->weight;
	}

	int width;
	char *conv = sjis2utf(msg, strlen(msg));
	TTF_SizeUTF8(font->font, conv, &width, NULL);

	pos.y -= (TTF_FontAscent(font->font) - font->size * 0.9);

	// XXX: This wont work if the outline size is larger than the stroke size
	//      of the font itself.
	if (tm->outline_left) {
		Point p = { pos.x - tm->outline_left, pos.y };
		_sdl_render_text(font->font, dst, p, conv, tm->outline_color);
	}
	if (tm->outline_up) {
		Point p = { pos.x, pos.y - tm->outline_up };
		_sdl_render_text(font->font, dst, p, conv, tm->outline_color);
	}
	if (tm->outline_right) {
		Point p = { pos.x + tm->outline_right, pos.y };
		_sdl_render_text(font->font, dst, p, conv, tm->outline_color);
	}
	if (tm->outline_down) {
		Point p = { pos.x, pos.y + tm->outline_down };
		_sdl_render_text(font->font, dst, p, conv, tm->outline_color);
	}
	_sdl_render_text(font->font, dst, pos, conv, tm->color);
	free(conv);
	return width;
}

void sdl_text_init(void)
{
	if (TTF_Init() == -1)
		ERROR("Failed to initialize SDL_ttf: %s", TTF_GetError());
	sdl_set_font(FONT_MINCHO, 16);
}
