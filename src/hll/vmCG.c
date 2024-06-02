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

#include "system4.h"
#include "system4/cg.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "vmSurface.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmCG, New, void);
//int vmCG_Release(void);
HLL_WARN_UNIMPLEMENTED( , void, vmCG, SetType, int type);
//int vmCG_EnumHandle(void);

static int vmCG_Exist(int num)
{
	return asset_exists(ASSET_CG, num);
}

static int vmCG_Load(int num)
{
	struct cg *cg = asset_cg_load(num);
	if (!cg)
		return 0;
	struct vm_surface *sf = vm_surface_create(cg->metrics.w, cg->metrics.h);
	gfx_init_texture_with_cg(&sf->texture, cg);
	sf->has_pixel = cg->metrics.has_pixel;
	sf->has_alpha = cg->metrics.has_alpha;
	sf->cg_no = num;
	cg_free(cg);
	return sf->no;
}

static void vmCG_Unload(int surface)
{
	vm_surface_delete(surface);
}

static int vmCG_Trans(int surface, int x, int y, int num)
{
	struct vm_surface *sf = vm_surface_get(surface);
	if (!sf)
		return 0;

	struct cg *cg = asset_cg_load(num);
	if (!cg)
		return 0;

	Texture src;
	gfx_init_texture_with_cg(&src, cg);
	cg_free(cg);

	if (sf->has_alpha)
		gfx_copy_with_alpha_map(&sf->texture, x, y, &src, 0, 0, src.w, src.h);
	else
		gfx_blend_amap(&sf->texture, x, y, &src, 0, 0, src.w, src.h);
	gfx_delete_texture(&src);
	return 1;
}

HLL_LIBRARY(vmCG,
	    HLL_EXPORT(New, vmCG_New),
	    HLL_TODO_EXPORT(Release, vmCG_Release),
	    HLL_EXPORT(SetType, vmCG_SetType),
	    HLL_TODO_EXPORT(EnumHandle, vmCG_EnumHandle),
	    HLL_EXPORT(Exist, vmCG_Exist),
	    HLL_EXPORT(Load, vmCG_Load),
	    HLL_EXPORT(Unload, vmCG_Unload),
	    HLL_EXPORT(Trans, vmCG_Trans)
	    );
