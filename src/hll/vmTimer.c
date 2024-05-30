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

#include "hll.h"
#include "id_pool.h"

struct vm_timer {
	uint64_t origin;
};

static struct id_pool pool;

static void vmTimer_Init(possibly_unused void *imainsystem)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmTimer_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmTimer_Open(void)
{
	int handle = id_pool_get_unused(&pool);
	struct vm_timer *timer = xcalloc(1, sizeof(struct vm_timer));
	id_pool_set(&pool, handle, timer);
	return handle;
}

static void vmTimer_Close(int handle)
{
	struct vm_timer *timer = id_pool_release(&pool, handle);
	if (timer)
		free(timer);
}

static void vmTimer_ModuleFini(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmTimer_Close(i);
	id_pool_delete(&pool);
}

static void vmTimer_Set(int handle, int time)
{
	struct vm_timer *timer = id_pool_get(&pool, handle);
	if (!timer)
		return;
	timer->origin = SDL_GetTicks64() - time;
}

static int vmTimer_Get(int handle)
{
	struct vm_timer *timer = id_pool_get(&pool, handle);
	if (!timer)
		return 0;
	return SDL_GetTicks64() - timer->origin;
}

static void vmTimer_Wait(int time)
{
	SDL_Delay(time);
}

static void vmTimer_Pass(int handle, int time)
{
	struct vm_timer *timer = id_pool_get(&pool, handle);
	if (!timer)
		return;
	int ms = timer->origin + time - SDL_GetTicks64();
	if (ms > 0)
		SDL_Delay(ms);
}

HLL_LIBRARY(vmTimer,
	    HLL_EXPORT(_ModuleFini, vmTimer_ModuleFini),
	    HLL_EXPORT(Init, vmTimer_Init),
	    HLL_EXPORT(EnumHandle, vmTimer_EnumHandle),
	    HLL_EXPORT(Open, vmTimer_Open),
	    HLL_EXPORT(Close, vmTimer_Close),
	    HLL_EXPORT(Set, vmTimer_Set),
	    HLL_EXPORT(Get, vmTimer_Get),
	    HLL_EXPORT(Wait, vmTimer_Wait),
	    HLL_EXPORT(Pass, vmTimer_Pass)
	    );
