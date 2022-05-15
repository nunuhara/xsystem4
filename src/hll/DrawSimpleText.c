/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4/string.h"

#include "hll.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "sact.h"

static int size = 20;
static int weight = 1;  // in pixels
static int pitch = 0;

static bool DrawSimpleText_DrawToAlphaMap(int sp_no, int x, int y, struct string *s)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return NULL;
	sprite_dirty(sp);

	int orig_weight = gfx_get_font_weight();
	int orig_size = gfx_get_font_size();
	int orig_space = gfx_get_font_space();
	gfx_set_font_weight(weight <= 1 ? FW_NORMAL : FW_BOLD);
	gfx_set_font_size(size);
	gfx_set_font_space(pitch);

	gfx_draw_text_to_amap(sprite_get_texture(sp), x, y, s->text);

	gfx_set_font_weight(orig_weight);
	gfx_set_font_size(orig_size);
	gfx_set_font_space(orig_space);
	return true;
}

static void DrawSimpleText_SetSize(int n)
{
	size = n;
}

static int DrawSimpleText_GetSize(void)
{
	return size;
}

static void DrawSimpleText_SetWeight(int n)
{
	weight = n;
}

static int DrawSimpleText_GetWeight(void)
{
	return weight;
}

static void DrawSimpleText_SetPitch(int n)
{
	pitch = n;
}

static int DrawSimpleText_GetPitch(void)
{
	return pitch;
}

HLL_LIBRARY(DrawSimpleText,
	    HLL_EXPORT(DrawToAlphaMap, DrawSimpleText_DrawToAlphaMap),
	    HLL_EXPORT(SetSize, DrawSimpleText_SetSize),
	    HLL_EXPORT(GetSize, DrawSimpleText_GetSize),
	    HLL_EXPORT(SetWeight, DrawSimpleText_SetWeight),
	    HLL_EXPORT(GetWeight, DrawSimpleText_GetWeight),
	    HLL_EXPORT(SetPitch, DrawSimpleText_SetPitch),
	    HLL_EXPORT(GetPitch, DrawSimpleText_GetPitch)
	    );
