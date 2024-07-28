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

#include "system4/cg.h"

#include "asset_manager.h"
#include "hll.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vmSurface.h"

HLL_QUIET_UNIMPLEMENTED(, void, vmDalkGaiden, Init, void);

#define STRUCT_BANK_POS_SLOT 1
#define MAP_WIDTH 45
#define MAP_HEIGHT 45
#define MAP_BACKGROUND_CG 1057
#define MAP_SPRITE_CG 1058

static int vmDalkGaiden_GrepEnemyIndex(int pos, struct page *bank, int *id_out)
{
	if (pos < 0 || !bank || bank->type != ARRAY_PAGE || bank->a_type != AIN_ARRAY_STRUCT)
		return 0;

	for (int i = 0; i < bank->nr_vars; i++) {
		struct page *elem = heap_get_page(bank->values[i].i);
		if (elem->values[STRUCT_BANK_POS_SLOT].i == pos) {
			*id_out = i;
			return 1;
		}
	}
	return 0;
}

static void vmDalkGaiden_PutFullMap(int surface, struct page *map_w1, struct page *map_w2, struct page *map_w3, struct page *mask)
{
	struct vm_surface *sf = vm_surface_get(surface);
	if (!sf)
		return;

	struct cg *cg = asset_cg_load(MAP_BACKGROUND_CG);
	if (!cg)
		return;
	Texture tex;
	gfx_init_texture_with_cg(&tex, cg);
	cg_free(cg);
	gfx_blend_amap(&sf->texture, 0, 0, &tex, 0, 0, tex.w, tex.h);
	gfx_delete_texture(&tex);

	cg = asset_cg_load(MAP_SPRITE_CG);
	if (!cg)
		return;
	gfx_init_texture_with_cg(&tex, cg);
	cg_free(cg);

	for (int y = 0; y < MAP_HEIGHT; y++) {
		for (int x = 0; x < MAP_WIDTH; x++) {
			if (y == MAP_HEIGHT - 1 && x % 2 == 1)
				continue;
			int dx = 15 + 10 * x;
			int dy = 15 + 10 * y + x % 2 * 5;
			if (mask->values[y * MAP_WIDTH + x].i) {
				gfx_blend_amap(&sf->texture, dx, dy, &tex, 0, 0, 12, 9);
				continue;
			}

			int w1 = map_w1->values[y * MAP_WIDTH + x].i;
			int w2 = map_w2->values[y * MAP_WIDTH + x].i;
			int w3 = map_w3->values[y * MAP_WIDTH + x].i;
			gfx_blend_amap(&sf->texture, dx, dy, &tex, w1 < 0 ? 12 : 24, 0, 12, 9);

			int sprite = -1;
			if (w3 >= 0) {
				sprite = (w3 < 8) ? 4
					: (w3 < 16) ? 3
					: (w3 >= 32) ? 6
					: (w3 - 16) / 2 + 8;
			} else if (w2 >= 0) {
				sprite = 5;
			}
			if (sprite >= 0) {
				int sx = sprite % 8 * 12;
				int sy = sprite / 8 * 9;
				gfx_blend_amap(&sf->texture, dx, dy, &tex, sx, sy, 12, 9);
			}
		}
	}

	gfx_delete_texture(&tex);
}

HLL_LIBRARY(vmDalkGaiden,
	    HLL_EXPORT(Init, vmDalkGaiden_Init),
	    HLL_EXPORT(GrepEnemyIndex, vmDalkGaiden_GrepEnemyIndex),
	    HLL_EXPORT(PutFullMap, vmDalkGaiden_PutFullMap)
	    );
