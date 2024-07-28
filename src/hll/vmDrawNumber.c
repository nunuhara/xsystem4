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

#include <stdio.h>

#include "system4.h"
#include "system4/cg.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "id_pool.h"
#include "vmSurface.h"

enum {
	ALIGN_LEFT = 0,
	ZERO_PAD = 1,
	ALIGN_RIGHT = 2,
	ALIGN_CENTER = 4,
};

struct vm_drawnumber {
	struct texture digits;
	struct texture sign;
	int surface;
	int kind;
	int figures;
	int space;
};

static struct id_pool pool;

static void vmDrawNumber_New(void)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmDrawNumber_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmDrawNumber_Open(int cg_no, int kind, int figure, int space)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;

	int width = (cg->metrics.w / 10 + space) * figure;
	int height = cg->metrics.h;

	int handle = id_pool_get_unused(&pool);
	struct vm_drawnumber *dn = xcalloc(1, sizeof(struct vm_drawnumber));
	id_pool_set(&pool, handle, dn);

	gfx_init_texture_with_cg(&dn->digits, cg);
	cg_free(cg);
	struct vm_surface *sf = vm_surface_create(width, height);
	gfx_init_texture_blank(&sf->texture, width, height);
	dn->surface = sf->no;
	dn->kind = kind;
	dn->figures = figure;
	dn->space = space;
	return handle;
}

static void vmDrawNumber_Close(int handle)
{
	struct vm_drawnumber *dn = id_pool_release(&pool, handle);
	if (!dn)
		return;
	gfx_delete_texture(&dn->digits);
	gfx_delete_texture(&dn->sign);
	vm_surface_delete(dn->surface);
	free(dn);
}

static int vmDrawNumber_Release(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmDrawNumber_Close(i);
	id_pool_delete(&pool);
	return 1;
}

static void vmDrawNumber_ModuleFini(void)
{
	vmDrawNumber_Release();
}

static int vmDrawNumber_GetSurface(int handle)
{
	struct vm_drawnumber *dn = id_pool_get(&pool, handle);
	return dn ? dn->surface : 0;
}

static void vmDrawNumber_Draw(int handle, int num)
{
	struct vm_drawnumber *dn = id_pool_get(&pool, handle);
	if (!dn)
		return;
	struct vm_surface *sf = vm_surface_get(dn->surface);
	if (!sf)
		return;

	char buf[16];
	if (dn->kind == ZERO_PAD) {
		snprintf(buf, min(sizeof(buf), dn->figures + 1), "%0*d", dn->figures, num);
	} else {
		snprintf(buf, min(sizeof(buf), dn->figures + 1), "%d", num);
	}

	int dw = dn->digits.w / 10;
	int w = (dw + dn->space) * strlen(buf);
	int h = dn->digits.h;
	int left;

	switch (dn->kind) {
	case ALIGN_LEFT:
	case ZERO_PAD:
		left = 0;
		break;
	case ALIGN_RIGHT:
		left = sf->texture.w - w;
		break;
	case ALIGN_CENTER:
		left = (sf->texture.w - w) / 2;
		break;
	default:
		WARNING("vmDrawNumber.Draw: unknown kind %d", dn->kind);
		left = 0;
		break;
	}

	gfx_fill_amap(&sf->texture, 0, 0, sf->texture.w, sf->texture.h, 0);
	for (int i = 0; buf[i]; i++) {
		switch (buf[i]) {
		case '-':
			gfx_copy_with_alpha_map(&sf->texture, left + i * (dw + dn->space), 0, &dn->sign, 0, 0, dw, h);
			break;
		default:
			gfx_copy_with_alpha_map(&sf->texture, left + i * (dw + dn->space), 0, &dn->digits, (buf[i] - '0') * dw, 0, dw, h);
			break;
		}
	}
}

int vmDrawNumber_SetSign(int handle, int cg_no)
{
	struct vm_drawnumber *dn = id_pool_get(&pool, handle);
	if (!dn)
		return 0;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;

	gfx_init_texture_with_cg(&dn->sign, cg);
	cg_free(cg);
	return 1;
}

HLL_LIBRARY(vmDrawNumber,
	    HLL_EXPORT(_ModuleFini, vmDrawNumber_ModuleFini),
	    HLL_EXPORT(New, vmDrawNumber_New),
	    HLL_EXPORT(Release, vmDrawNumber_Release),
	    HLL_EXPORT(EnumHandle, vmDrawNumber_EnumHandle),
	    HLL_EXPORT(Open, vmDrawNumber_Open),
	    HLL_EXPORT(Close, vmDrawNumber_Close),
	    HLL_EXPORT(GetSurface, vmDrawNumber_GetSurface),
	    HLL_EXPORT(Draw, vmDrawNumber_Draw),
	    HLL_EXPORT(SetSign, vmDrawNumber_SetSign)
	    );
