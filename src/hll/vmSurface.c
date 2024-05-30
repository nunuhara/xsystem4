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

#include <assert.h>

#include "hll.h"
#include "id_pool.h"
#include "vmSurface.h"

static struct id_pool pool;

struct vm_surface *vm_surface_create(int width, int height)
{
	int no = id_pool_get_unused(&pool);
	struct vm_surface *sf = xcalloc(1, sizeof(struct vm_surface));
	id_pool_set(&pool, no, sf);
	sf->w = width;
	sf->h = height;
	sf->has_pixel = true;
	sf->has_alpha = true;
	sf->no = no;
	return sf;
}

struct vm_surface *vm_surface_get(int no)
{
	return id_pool_get(&pool, no);
}

void vm_surface_delete(int no)
{
	struct vm_surface *sf = id_pool_release(&pool, no);
	if (sf) {
		gfx_delete_texture(&sf->texture);
		free(sf);
	}
}

int vm_surface_get_main_surface(void)
{
	return ID_POOL_HANDLE_BASE;
}

static void vmSurface_New(void)
{
	if (pool.slots)
		return;  // already initialized
	gfx_init();
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);

	// create main surface
	Texture *t = gfx_main_surface();
	struct vm_surface *sf = vm_surface_create(t->w, t->h);
	sf->texture = *t; // XXX: textures normally shouldn't be copied like this...
	assert(sf->no == ID_POOL_HANDLE_BASE);
}

static int vmSurface_Release(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vm_surface_delete(i);
	id_pool_delete(&pool);
	return 1;
}

static void vmSurface_ModuleFini(void)
{
	vmSurface_Release();
}

static int vmSurface_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmSurface_Create(int width, int height, possibly_unused int bpp)
{
	struct vm_surface *sf = vm_surface_create(width, height);
	gfx_init_texture_rgba(&sf->texture, sf->w, sf->h, (SDL_Color){0, 0, 0, 0});
	return sf->no;
}

static int vmSurface_CreatePixelMap(int width, int height, possibly_unused int bpp)
{
	struct vm_surface *sf = vm_surface_create(width, height);
	gfx_init_texture_rgba(&sf->texture, sf->w, sf->h, (SDL_Color){0, 0, 0, 255});
	sf->has_alpha = false;
	return sf->no;
}

static int vmSurface_CreateAlphaMap(int width, int height)
{
	struct vm_surface *sf = vm_surface_create(width, height);
	gfx_init_texture_rgba(&sf->texture, sf->w, sf->h, (SDL_Color){0, 0, 0, 255});
	sf->has_pixel = false;
	return sf->no;
}

static int vmSurface_GetWidth(int surface)
{
	struct vm_surface *sf = id_pool_get(&pool, surface);
	return sf ? sf->w : 0;
}

static int vmSurface_GetHeight(int surface)
{
	struct vm_surface *sf = id_pool_get(&pool, surface);
	return sf ? sf->h : 0;
}

//int vmSurface_GetBpp(int surface);

static int vmSurface_ExistPixel(int surface)
{
	struct vm_surface *sf = id_pool_get(&pool, surface);
	return sf && sf->has_pixel;
}

static int vmSurface_ExistAlpha(int surface)
{
	struct vm_surface *sf = id_pool_get(&pool, surface);
	return sf && sf->has_alpha;
}

HLL_LIBRARY(vmSurface,
	    HLL_EXPORT(_ModuleFini, vmSurface_ModuleFini),
	    HLL_EXPORT(New, vmSurface_New),
	    HLL_EXPORT(Release, vmSurface_Release),
	    HLL_EXPORT(EnumHandle, vmSurface_EnumHandle),
	    HLL_EXPORT(Create, vmSurface_Create),
	    HLL_EXPORT(CreatePixelMap, vmSurface_CreatePixelMap),
	    HLL_EXPORT(CreateAlphaMap, vmSurface_CreateAlphaMap),
	    HLL_EXPORT(Delete, vm_surface_delete),
	    HLL_EXPORT(GetWidth, vmSurface_GetWidth),
	    HLL_EXPORT(GetHeight, vmSurface_GetHeight),
	    HLL_TODO_EXPORT(GetBpp, vmSurface_GetBpp),
	    HLL_EXPORT(ExistPixel, vmSurface_ExistPixel),
	    HLL_EXPORT(ExistAlpha, vmSurface_ExistAlpha)
	    );
