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
#include "effect.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "system4.h"

const char *effect_names[NR_EFFECTS] = {
	[EFFECT_CROSSFADE]              = "EFFECT_CROSSFADE",
	[EFFECT_FADEOUT]                = "EFFECT_FADEOUT",
	[EFFECT_FADEIN]                 = "EFFECT_FADEIN",
	[EFFECT_WHITEOUT]               = "EFFECT_WHITEOUT",
	[EFFECT_WHITEIN]                = "EFFECT_WHITEIN",
	[EFFECT_CROSSFADE_MOSAIC]       = "EFFECT_CROSSFADE_MOSAIC",
	[EFFECT_BLIND_DOWN]             = "EFFECT_BLIND_DOWN",
	[EFFECT_BLIND_LR]               = "EFFECT_BLIND_LR",
	[EFFECT_BLIND_DOWN_LR]          = "EFFECT_BLIND_DOWN_LR",
	[EFFECT_ZOOM_BLEND_BLUR]        = "EFFECT_ZOOM_BLEND_BLUR",
	[EFFECT_LINEAR_BLUR]            = "EFFECT_LINEAR_BLUR",
	[EFFECT_UP_DOWN_CROSSFADE]      = "EFFECT_UP_DOWN_CROSSFADE",
	[EFFECT_DOWN_UP_CROSSFADE]      = "EFFECT_DOWN_UP_CROSSFADE",
	[EFFECT_PENTAGRAM_IN_OUT]       = "EFFECT_PENTAGRAM_IN_OUT",
	[EFFECT_PENTAGRAM_OUT_IN]       = "EFFECT_PENTAGRAM_OUT_IN",
	[EFFECT_HEXAGRAM_IN_OUT]        = "EFFECT_HEXAGRAM_IN_OUT",
	[EFFECT_HEXAGRAM_OUT_IN]        = "EFFECT_HEXAGRAM_OUT_IN",
	[EFFECT_AMAP_CROSSFADE]         = "EFFECT_AMAP_CROSSFADE",
	[EFFECT_VERTICAL_BAR_BLUR]      = "EFFECT_VERTICAL_BAR_BLUR",
	[EFFECT_ROTATE_OUT]             = "EFFECT_ROTATE_OUT",
	[EFFECT_ROTATE_IN]              = "EFFECT_ROTATE_IN",
	[EFFECT_ROTATE_OUT_CW]          = "EFFECT_ROTATE_OUT_CW",
	[EFFECT_ROTATE_IN_CW]           = "EFFECT_ROTATE_IN_CW",
	[EFFECT_BLOCK_DISSOLVE]         = "EFFECT_BLOCK_DISSOLVE",
	[EFFECT_POLYGON_ROTATE_Y]       = "EFFECT_POLYGON_ROTATE_Y",
	[EFFECT_POLYGON_ROTATE_Y_CW]    = "EFFECT_POLYGON_ROTATE_Y_CW",
	[EFFECT_OSCILLATE]              = "EFFECT_OSCILLATE",
	[EFFECT_POLYGON_ROTATE_X_CW]    = "EFFECT_POLYGON_ROTATE_X_CW",
	[EFFECT_POLYGON_ROTATE_X]       = "EFFECT_POLYGON_ROTATE_X",
	[EFFECT_ROTATE_ZOOM_BLEND_BLUR] = "EFFECT_ROTATE_ZOOM_BLEND_BLUR",
	[EFFECT_ZIGZAG_CROSSFADE]       = "EFFECT_ZIGZAG_CROSSFADE",
	[EFFECT_TV_SWITCH_OFF]          = "EFFECT_TV_SWITCH_OFF",
	[EFFECT_TV_SWITCH_ON]           = "EFFECT_TV_SWITCH_ON",
	[EFFECT_POLYGON_EXPLOSION]      = "EFFECT_POLYGON_EXPLOSION",
	[EFFECT_NOISE_CROSSFADE]        = "EFFECT_NOISE_CROSSFADE",
	[EFFECT_TURN_PAGE]              = "EFFECT_TURN_PAGE",
	[EFFECT_SEPIA_NOISE_CROSSFADE]  = "EFFECT_SEPIA_NOISE_CROSSFADE",
	[EFFECT_CRUMPLED_PAPER_PULL]    = "EFFECT_CRUMPLED_PAPER_PULL",
	[EFFECT_HORIZONTAL_ZIGZAG]      = "EFFECT_HORIZONTAL_ZIGZAG",
	[EFFECT_LINEAR_BLUR_HD]         = "EFFECT_LINEAR_BLUR_HD",
	[EFFECT_VERTICAL_BAR_BLUR_HD]   = "EFFECT_VERTICAL_BAR_BLUR_HD",
	[EFFECT_AMAP_CROSSFADE2]        = "EFFECT_AMAP_CROSSFADE2",
	[EFFECT_ZOOM_LR]                = "EFFECT_ZOOM_LR",
	[EFFECT_ZOOR_RL]                = "EFFECT_ZOOR_RL",
	[EFFECT_CROSSFADE_LR]           = "EFFECT_CROSSFADE_LR",
	[EFFECT_CROSSFADE_RL]           = "EFFECT_CROSSFADE_RL",
	[EFFECT_PIXEL_EXPLOSION]        = "EFFECT_PIXEL_EXPLOSION",
	[EFFECT_ZOOM_IN_CROSSFADE]      = "EFFECT_ZOOM_IN_CROSSFADE",
	[EFFECT_PIXEL_DROP]             = "EFFECT_PIXEL_DROP",
	[EFFECT_BLUR_FADEOUT]           = "EFFECT_BLUR_FADEOUT",
	[EFFECT_BLUR_CROSSFADE]         = "EFFECT_BLUR_CROSSFADE",
	[EFFECT_2ROT_ZOOM_BLEND_BLUR]   = "EFFECT_2ROT_ZOOM_BLEND_BLUR",
};

extern GLuint main_surface_fb;

static void effect_crossfade(Texture *old, Texture *new, float progress)
{
	new->alpha_mod = progress * 255;

	gfx_render_texture(old, NULL);
	gfx_render_texture(new, NULL);
}

typedef void (*effect_callback)(Texture*,Texture*,float);

static effect_callback effects[NR_EFFECTS] = {
	[EFFECT_CROSSFADE] = effect_crossfade
};

int sact_Effect(int type, possibly_unused int time, possibly_unused int key)
{
	if (type <= 0 || type >= NR_EFFECTS) {
		WARNING("Invalid or unknown effect: %d", type);
		return 0;
	}
	if (!effects[type]) {
		WARNING("Unimplemented effect: %s", effect_names[type]);
		return 0;
	}

	// get old & new scene textures
	Texture old, new;
	gfx_copy_main_surface(&old);
	sprite_render_scene();
	gfx_copy_main_surface(&new);

	effect_callback effect = effects[type];
	for (int i = 0; i < time; i += 16) {
		gfx_clear();
		effect(&old, &new, (float)i / (float)time);
		gfx_swap();
		SDL_Delay(16);
	}

	gfx_delete_texture(&old);
	gfx_delete_texture(&new);

	return 1;
}

