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
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "gfx/gfx.h"
#include "hll.h"
#include "ChrLoader.h"
#include "vmSurface.h"
#include "xsystem4.h"

static void vmChrLoader_Init(possibly_unused void *imainsystem, possibly_unused struct string *string)
{
	chr_loader_init("Chr");
}

static int vmChrLoader_Exist(int id)
{
	return chr_loader_exists(id) ? 1 : 0;
}

HLL_QUIET_UNIMPLEMENTED(, void, vmChrLoader, ReleaseMemory, int id, int index);

static struct string *vmChrLoader_GetMsg(int id, int index)
{
	struct string *msg = chr_loader_get_string(id, index);
	return msg ? msg : string_ref(&EMPTY_STRING);
}

static struct string *vmChrLoader_GetName(int id)
{
	struct string *name = chr_loader_get_name(id);
	return name ? name : string_ref(&EMPTY_STRING);
}

static int vmChrLoader_MemoryToSurface(int memory, int x, int y, int surface)
{
	struct vm_surface *sf = vm_surface_get(surface);
	if (!sf)
		return 0;

	size_t size;
	void *data = chr_loader_get_blob(memory, &size);
	if (!data)
		return 0;

	struct cg *cg = cg_load_buffer(data, size);
	if (!cg)
		return 0;

	Texture tex;
	gfx_init_texture_with_cg(&tex, cg);
	cg_free(cg);

	gfx_copy(&sf->texture, x, y, &tex, 0, 0, tex.w, tex.h);
	gfx_delete_texture(&tex);
	return 1;
}

HLL_LIBRARY(vmChrLoader,
	    HLL_EXPORT(Init, vmChrLoader_Init),
	    HLL_EXPORT(Exist, vmChrLoader_Exist),
	    HLL_EXPORT(GetID, chr_loader_get_id),
	    HLL_EXPORT(GetStatus, chr_loader_get_status),
	    HLL_EXPORT(GetMemory, chr_loader_get_blob_handle),
	    HLL_EXPORT(ReleaseMemory, vmChrLoader_ReleaseMemory),
	    HLL_EXPORT(GetMsg, vmChrLoader_GetMsg),
	    HLL_EXPORT(GetName, vmChrLoader_GetName),
	    HLL_EXPORT(MemoryToSurface, vmChrLoader_MemoryToSurface)
	    );
