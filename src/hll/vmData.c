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

#include "system4/archive.h"
#include "system4/buffer.h"

#include "asset_manager.h"
#include "hll.h"
#include "id_pool.h"
#include "vm/heap.h"
#include "vm/page.h"

struct vm_data {
	struct archive_data *dfile;
	struct buffer buf;
};

static struct id_pool pool;

static void vmData_New(void)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

HLL_QUIET_UNIMPLEMENTED(, void, vmData, SetType, int type);

static int vmData_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmData_Exist(int num)
{
	return asset_exists(ASSET_DATA, num);
}

static int vmData_Load(int num)
{
	struct archive_data *dfile = asset_get(ASSET_DATA, num);
	if (!dfile)
		return 0;

	int id = id_pool_get_unused(&pool);
	struct vm_data *obj = xcalloc(1, sizeof(struct vm_data));
	id_pool_set(&pool, id, obj);
	obj->dfile = dfile;
	buffer_init(&obj->buf, dfile->data, dfile->size);
	return id;
}

static void vmData_Unload(int id)
{
	struct vm_data *obj = id_pool_release(&pool, id);
	if (!obj)
		return;
	archive_free_data(obj->dfile);
	free(obj);
}

static int vmData_Release(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmData_Unload(i);
	id_pool_delete(&pool);
	return 1;
}

static int vmData_ReadArrayChar(int id, struct page **array_)
{
	struct vm_data *obj = id_pool_get(&pool, id);
	if (!obj)
		return 0;

	struct page *array = *array_;
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		VM_ERROR("Type error");

	for (int i = 0; i < array->nr_vars; i++) {
		variable_set(array, i, AIN_INT, vm_int((int8_t)buffer_read_u8(&obj->buf)));
	}
	return 1;
}

static int vmData_ReadArrayShort(int id, struct page **array_)
{
	struct vm_data *obj = id_pool_get(&pool, id);
	if (!obj)
		return 0;

	struct page *array = *array_;
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		VM_ERROR("Type error");

	for (int i = 0; i < array->nr_vars; i++) {
		variable_set(array, i, AIN_INT, vm_int((int16_t)buffer_read_u16(&obj->buf)));
	}
	return 1;
}

static int vmData_ReadArrayInt(int id, struct page **array_)
{
	struct vm_data *obj = id_pool_get(&pool, id);
	if (!obj)
		return 0;

	struct page *array = *array_;
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		VM_ERROR("Type error");

	for (int i = 0; i < array->nr_vars; i++) {
		variable_set(array, i, AIN_INT, vm_int(buffer_read_int32(&obj->buf)));
	}
	return 1;
}

//int vmData_ReadArrayFloat(int id, struct page **pIVMArray);

static int vmData_ReadArrayString(int id, struct page **array_)
{
	struct vm_data *obj = id_pool_get(&pool, id);
	if (!obj)
		return 0;

	struct page *array = *array_;
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_STRING || array->array.rank != 1)
		VM_ERROR("Type error");

	for (int i = 0; i < array->nr_vars; i++) {
		struct string *s = buffer_read_string(&obj->buf);
		variable_set(array, i, AIN_STRING, vm_int(heap_alloc_string(s)));
	}
	return 1;
}

//int vmData_ReadArrayStruct(int id, struct page **pIVMArray);

static void vmData_SeekChar(int id, int size, int absolute)
{
	struct vm_data *obj = id_pool_get(&pool, id);
	if (!obj)
		return;
	if (absolute)
		buffer_seek(&obj->buf, size);
	else
		buffer_skip(&obj->buf, size);
}

static void vmData_SeekShort(int id, int size, int absolute)
{
	vmData_SeekChar(id, size * 2, absolute);
}

static void vmData_SeekInt(int id, int size, int absolute)
{
	vmData_SeekChar(id, size * 4, absolute);
}

static void vmData_SeekFloat(int id, int size, int absolute)
{
	vmData_SeekChar(id, size * 4, absolute);
}

static void vmData_SeekString(int id, int size, int absolute)
{
	struct vm_data *obj = id_pool_get(&pool, id);
	if (!obj)
		return;
	if (absolute)
		buffer_seek(&obj->buf, 0);
	while (size-- > 0)
		buffer_skip_string(&obj->buf);
}

HLL_LIBRARY(vmData,
	    HLL_EXPORT(New, vmData_New),
	    HLL_EXPORT(Release, vmData_Release),
	    HLL_EXPORT(SetType, vmData_SetType),
	    HLL_EXPORT(EnumHandle, vmData_EnumHandle),
	    HLL_EXPORT(Exist, vmData_Exist),
	    HLL_EXPORT(Load, vmData_Load),
	    HLL_EXPORT(Unload, vmData_Unload),
	    HLL_EXPORT(ReadArrayChar, vmData_ReadArrayChar),
	    HLL_EXPORT(ReadArrayShort, vmData_ReadArrayShort),
	    HLL_EXPORT(ReadArrayInt, vmData_ReadArrayInt),
	    HLL_TODO_EXPORT(ReadArrayFloat, vmData_ReadArrayFloat),
	    HLL_EXPORT(ReadArrayString, vmData_ReadArrayString),
	    HLL_TODO_EXPORT(ReadArrayStruct, vmData_ReadArrayStruct),
	    HLL_EXPORT(SeekChar, vmData_SeekChar),
	    HLL_EXPORT(SeekShort, vmData_SeekShort),
	    HLL_EXPORT(SeekInt, vmData_SeekInt),
	    HLL_EXPORT(SeekFloat, vmData_SeekFloat),
	    HLL_EXPORT(SeekString, vmData_SeekString)
	    );
