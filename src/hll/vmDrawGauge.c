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
#include "gfx/gfx.h"
#include "hll.h"
#include "id_pool.h"
#include "vmSurface.h"

struct vm_drawgauge {
	struct texture tex;
	int surface;
};

static struct id_pool pool;

static void vmDrawGauge_New(void)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmDrawGauge_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmDrawGauge_Open(int cg_no, int origin, int phase)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;

	int w = cg->metrics.w;
	int h = cg->metrics.h;

	int handle = id_pool_get_unused(&pool);
	struct vm_drawgauge *dg = xcalloc(1, sizeof(struct vm_drawgauge));
	id_pool_set(&pool, handle, dg);

	gfx_init_texture_with_cg(&dg->tex, cg);
	cg_free(cg);

	struct vm_surface *sf = vm_surface_create(w, h);
	gfx_init_texture_blank(&sf->texture, w, h);
	dg->surface = sf->no;

	return handle;
}

static void vmDrawGauge_Close(int handle)
{
	struct vm_drawgauge *dg = id_pool_release(&pool, handle);
	if (!dg)
		return;
	gfx_delete_texture(&dg->tex);
	vm_surface_delete(dg->surface);
	free(dg);
}

static int vmDrawGauge_Release(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmDrawGauge_Close(i);
	id_pool_delete(&pool);
	return 1;
}

static void vmDrawGauge_ModuleFini(void)
{
	vmDrawGauge_Release();
}

static int vmDrawGauge_GetSurface(int handle)
{
	struct vm_drawgauge *dg = id_pool_get(&pool, handle);
	return dg ? dg->surface : 0;
}

static void vmDrawGauge_Draw(int handle, int now, int max)
{
	struct vm_drawgauge *dg = id_pool_get(&pool, handle);
	if (!dg)
		return;
	struct vm_surface *sf = vm_surface_get(dg->surface);
	if (!sf)
		return;

	gfx_fill_with_alpha(&sf->texture, 0, 0, sf->texture.w, sf->texture.h, 255, 0, 255, 0);
	if (max > 0) {
		int w = sf->texture.w * now / max;
		gfx_copy_with_alpha_map(&sf->texture, 0, 0, &dg->tex, 0, 0, w, sf->texture.h);
	}
}

HLL_LIBRARY(vmDrawGauge,
	    HLL_EXPORT(_ModuleFini, vmDrawGauge_ModuleFini),
	    HLL_EXPORT(New, vmDrawGauge_New),
	    HLL_EXPORT(Release, vmDrawGauge_Release),
	    HLL_EXPORT(EnumHandle, vmDrawGauge_EnumHandle),
	    HLL_EXPORT(Open, vmDrawGauge_Open),
	    HLL_EXPORT(Close, vmDrawGauge_Close),
	    HLL_EXPORT(GetSurface, vmDrawGauge_GetSurface),
	    HLL_EXPORT(Draw, vmDrawGauge_Draw)
	    );
