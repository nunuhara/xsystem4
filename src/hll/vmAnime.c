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

#include <SDL.h>

#include "system4/cg.h"

#include "asset_manager.h"
#include "audio.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "id_pool.h"

struct vm_anime {
	int cg;
	int length;
	int type;
	int interval;
	Point pos;
	int wave_channel;
};

static struct id_pool pool;

static void vmAnime_New(possibly_unused void *imainsystem)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmAnime_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmAnime_Open(int cg, int length, int type)
{
	int no = id_pool_get_unused(&pool);
	struct vm_anime *anime = xcalloc(1, sizeof(struct vm_anime));
	id_pool_set(&pool, no, anime);
	anime->cg = cg;
	anime->length = length;
	anime->type = type;
	return no;
}

static void vmAnime_Close(int handle)
{
	struct vm_anime *anime = id_pool_release(&pool, handle);
	if (anime)
		free(anime);
}

static int vmAnime_Release(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmAnime_Close(i);
	id_pool_delete(&pool);
	return 1;
}

static void vmAnime_ModuleFini(void)
{
	vmAnime_Release();
}

static void vmAnime_SetInterval(int handle, int interval)
{
	struct vm_anime *anime = id_pool_get(&pool, handle);
	if (!anime)
		return;
	anime->interval = interval;
}

static void vmAnime_SetPos(int handle, int x, int y)
{
	struct vm_anime *anime = id_pool_get(&pool, handle);
	if (!anime)
		return;
	anime->pos.x = x;
	anime->pos.y = y;
}

static void vmAnime_SetWave(int handle, int channel)
{
	struct vm_anime *anime = id_pool_get(&pool, handle);
	if (!anime)
		return;
	anime->wave_channel = channel;
}

static void vmAnime_Draw(int handle, int return_)
{
	struct vm_anime *anime = id_pool_get(&pool, handle);
	if (!anime)
		return;

	if (anime->wave_channel) {
		wav_stop(anime->wave_channel);
		wav_play(anime->wave_channel);
	}

	Texture old;
	gfx_copy_main_surface(&old);
	Texture *dst = gfx_main_surface();

	uint64_t start_time = SDL_GetTicks64();
	for (int i = 0; i < anime->length; i++) {
		struct cg *cg = asset_cg_load(anime->cg + i);
		Texture src;
		gfx_init_texture_with_cg(&src, cg);
		gfx_copy(dst, 0, 0, &old, 0, 0, old.w, old.h);
		gfx_blend_add_satur(dst, anime->pos.x, anime->pos.y, &src, 0, 0, src.w, src.h);
		gfx_delete_texture(&src);
		cg_free(cg);
		gfx_swap();
		SDL_Delay(start_time + (i + 1) * anime->interval - SDL_GetTicks64());
	}
	if (return_) {
		gfx_copy(dst, 0, 0, &old, 0, 0, old.w, old.h);
		gfx_swap();
	}
	gfx_delete_texture(&old);
}

HLL_LIBRARY(vmAnime,
	    HLL_EXPORT(_ModuleFini, vmAnime_ModuleFini),
	    HLL_EXPORT(New, vmAnime_New),
	    HLL_EXPORT(Release, vmAnime_Release),
	    HLL_EXPORT(EnumHandle, vmAnime_EnumHandle),
	    HLL_EXPORT(Open, vmAnime_Open),
	    HLL_EXPORT(Close, vmAnime_Close),
	    HLL_EXPORT(SetInterval, vmAnime_SetInterval),
	    HLL_EXPORT(SetPos, vmAnime_SetPos),
	    HLL_EXPORT(SetWave, vmAnime_SetWave),
	    HLL_EXPORT(Draw, vmAnime_Draw)
	    );
