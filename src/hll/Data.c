/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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
#include <zlib.h>

#include "system4/archive.h"
#include "system4/buffer.h"
#include "system4/little_endian.h"
#include "system4/string.h"

#include "hll.h"
#include "asset_manager.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static struct buffer reader;

static int Data_Exist(int num)
{
	return asset_exists(ASSET_DATA, num);
}

static int Data_Open(int num)
{
	struct archive_data *dfile = asset_get(ASSET_DATA, num);
	if (!dfile)
		return 0;
	unsigned long uncompressed_size = LittleEndian_getDW(dfile->data, 0);
	uint8_t *buf = xmalloc(uncompressed_size);
	int rv = uncompress(buf, &uncompressed_size, (uint8_t *)dfile->data + 4, dfile->size - 4);
	if (rv != Z_OK) {
		WARNING("%s: uncompress failed (%d)", dfile->name, rv);
		archive_free_data(dfile);
		free(buf);
		return 0;
	}
	archive_free_data(dfile);
	if (reader.buf)
		free(reader.buf);
	buffer_init(&reader, buf, uncompressed_size);
	return 1;
}

static int Data_Close(void)
{
	if (!reader.buf)
		return 0;
	free(reader.buf);
	reader.buf = NULL;
	return 1;
}

static void read_struct(struct page *page)
{
	if (page->type != STRUCT_PAGE) {
		VM_ERROR("Data.Read of non-struct");
	}

	for (int i = 0; i < page->nr_vars; i++) {
		int struct_type;
		enum ain_data_type type = variable_type(page, i, &struct_type, NULL);
		union vm_value val;
		switch (type) {
		case AIN_INT:
			val = vm_int(buffer_read_int32(&reader));
			break;
		case AIN_FLOAT:
			val = vm_float(buffer_read_float(&reader));
			break;
		case AIN_STRING:
			{
				struct string *s = cstr_to_string(buffer_strdata(&reader));
				buffer_skip(&reader, s->size + 1);
				val = vm_int(heap_alloc_string(s));
			}
			break;
		case AIN_STRUCT:
			{
				int slot = alloc_struct(struct_type);
				read_struct(heap[slot].page);
				val = vm_int(slot);
			}
			break;
		default:
			VM_ERROR("Data.Read: unsupported data type");
		}
		variable_set(page, i, type, val);
	}
}

static int Data_Read(struct page **page)
{
	if (!reader.buf || buffer_remaining(&reader) == 0) {
		return 0;
	}
	read_struct(*page);
	return 1;
}

HLL_LIBRARY(Data,
	    HLL_EXPORT(Exist, Data_Exist),
	    HLL_EXPORT(Open, Data_Open),
	    HLL_EXPORT(Close, Data_Close),
	    HLL_EXPORT(Read, Data_Read)
	    );
