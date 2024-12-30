/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include "effect.h"
#include "hll.h"
#include "sact.h"
#include "xsystem4.h"

static struct {
	bool on;
	int type;
	int dst;
	int old;
	int new;
} draw_effect = {false};

bool DrawEffect_Begin(int writeSurface, int wx, int wy, int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height, int effect)
{
	// Only support fullscreen effects
	if (wx || wy || dx || dy || sx || sy || width != config.view_width || height != config.view_height)
		return false;
	draw_effect.on = true;
	draw_effect.type = effect;
	draw_effect.dst = writeSurface;
	draw_effect.old = destSurface;
	draw_effect.new = srcSurface;
	return true;
}

void DrawEffect_Draw(int time, int totalTime)
{
	if (!draw_effect.on)
		return;
	struct sact_sprite *dst = sact_try_get_sprite(draw_effect.dst);
	struct sact_sprite *old = sact_try_get_sprite(draw_effect.old);
	struct sact_sprite *new = sact_try_get_sprite(draw_effect.new);
	if (!dst || !old || !new)
		return;

	Texture *dst_tex = sprite_get_texture(dst);
	Texture *old_tex = sprite_get_texture(old);
	Texture *new_tex = sprite_get_texture(new);
	effect_update_texture(draw_effect.type, dst_tex, old_tex, new_tex, (float)time / (float)totalTime);
	sprite_dirty(dst);
}

void DrawEffect_End(void)
{
	draw_effect.on = false;
}

HLL_LIBRARY(DrawEffect,
	    HLL_EXPORT(Begin, DrawEffect_Begin),
	    HLL_EXPORT(Draw, DrawEffect_Draw),
	    HLL_EXPORT(End, DrawEffect_End)
	    );
