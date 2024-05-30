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

#include <stdlib.h>

#include "system4.h"

#include "hll.h"
#include "id_pool.h"
#include "input.h"
#include "vm.h"
#include "xsystem4.h"

#define MAXENT 32

struct vm_key {
	int size;
	int win[MAXENT];
	int ret[MAXENT];
};

static struct id_pool pool;

static void handle_events_throttled(void)
{
	// To prevent taking 100% CPU time in busy input loops,
	// sleep 10ms every 30 input event checks.
	static int count;
	if (++count == 30) {
		count = 0;
		vm_sleep(10);
	}
	handle_events();
}

static void vmKey_Init(possibly_unused void *imainsystem)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmKey_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmKey_Open(void)
{
	int no = id_pool_get_unused(&pool);
	struct vm_key *vk = xcalloc(1, sizeof(struct vm_key));
	id_pool_set(&pool, no, vk);
	return no;
}

static void vmKey_Close(int handle)
{
	struct vm_key *vk = id_pool_release(&pool, handle);
	if (vk)
		free(vk);
}

static void vmKey_ModuleFini(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmKey_Close(i);
	id_pool_delete(&pool);
}

static int vmKey_Set(int handle, int win_key, int ret_key)
{
	struct vm_key *vk = id_pool_get(&pool, handle);
	if (!vk)
		return 0;
	if (vk->size + 1 >= MAXENT)
		VM_ERROR("too many keys");
	vk->win[vk->size] = win_key;
	vk->ret[vk->size] = ret_key;
	vk->size++;
	return 1;
}

static int vmKey_Get(int handle)
{
	struct vm_key *vk = id_pool_get(&pool, handle);
	if (!vk)
		return 0;

	handle_events_throttled();
	int ret = 0;
	for (int i = 0; i < vk->size; i++) {
		if (key_is_down(vk->win[i])) {
			ret |= vk->ret[i];
		}
	}
	return ret;
}

static int vmKey_GetWheelCount(void)
{
	int forward, back;
	handle_events_throttled();
	mouse_get_wheel(&forward, &back);
	mouse_clear_wheel();
	return back - forward;
}

//void vmKey_ClearBuffer(void);

HLL_LIBRARY(vmKey,
	    HLL_EXPORT(_ModuleFini, vmKey_ModuleFini),
	    HLL_EXPORT(Init, vmKey_Init),
	    HLL_EXPORT(EnumHandle, vmKey_EnumHandle),
	    HLL_EXPORT(Open, vmKey_Open),
	    HLL_EXPORT(Close, vmKey_Close),
	    HLL_EXPORT(Set, vmKey_Set),
	    HLL_EXPORT(Get, vmKey_Get),
	    HLL_EXPORT(GetWheelCount, vmKey_GetWheelCount),
	    HLL_TODO_EXPORT(ClearBuffer, vmKey_ClearBuffer)
	    );
