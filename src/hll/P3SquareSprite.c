/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include "sact.h"
#include "gfx/gfx.h"
#include "vm/page.h"
#include "hll.h"
#include "xsystem4.h"

static bool P3SquareSprite_Copy(int dest_handle, int src_handle, int size, struct page *matrix)
{
	struct sact_sprite *dest = sact_try_get_sprite(dest_handle);
	struct sact_sprite *src = sact_try_get_sprite(src_handle);

	if (!dest || !src)
		return false;

	Texture *dest_tex = sprite_get_texture(dest);
	Texture *src_tex = sprite_get_texture(src);

	int tile_w = src_tex->w;
	int tile_h = src_tex->h;
	int half_w = tile_w / 2;
	int half_h = tile_h / 2;

	int canvas_center_x = dest_tex->w / 2;
	int canvas_center_y = dest_tex->h / 2;

	int side_count = size * 2 + 1;

	if (matrix->nr_vars != side_count * side_count)
		VM_ERROR("Expected %d variables in array, got %d", side_count * side_count, matrix->nr_vars);

	int index = 0;
	for (int y = -size; y <= size; ++y) {
		for (int x = -size; x <= size; ++x) {
			// Check if we should draw this tile
			if (matrix->values[index++].i == 0)
				continue;

			// Isometric coordinate conversion
			int screen_pos_x = canvas_center_x + (x - y - 1) * half_w;
			int screen_pos_y = canvas_center_y + (x + y - 1) * half_h;

			// Draw the sprite
			gfx_copy_use_amap_border(dest_tex, screen_pos_x, screen_pos_y, src_tex, 0, 0, tile_w, tile_h, 1);
			gfx_copy_amap_max(dest_tex, screen_pos_x, screen_pos_y, src_tex, 0, 0, tile_w, tile_h);
		}
	}

	return true;
}

HLL_LIBRARY(P3SquareSprite,
	    HLL_EXPORT(Copy, P3SquareSprite_Copy)
	    );
