/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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
#include <string.h>
#include "system4.h"
#include "system4/buffer.h"
#include "system4/cg.h"

#include "dungeon/dtx.h"

struct dtx *dtx_parse(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (strncmp(buffer_strdata(&r), "DTEX", 4)) {
		WARNING("dtx_parse: not a DTX file");
		return NULL;
	}
	buffer_skip(&r, 4);

	struct dtx *dtx = xcalloc(1, sizeof(struct dtx));
	dtx->version = buffer_read_int32(&r);
	dtx->nr_rows = buffer_read_int32(&r);
	dtx->nr_columns = buffer_read_int32(&r);
	dtx->entries = xcalloc(dtx->nr_rows * dtx->nr_columns, sizeof(struct dtx_entry));
	for (int i = 0; i < dtx->nr_rows; i++) {
		int nr_items = buffer_read_int32(&r);
		if (nr_items != dtx->nr_columns) {
			WARNING("dtx_parse: unexpected number of items");
			dtx_free(dtx);
			return NULL;
		}
		for (int j = 0; j < nr_items; j++) {
			uint32_t size = buffer_read_int32(&r);
			if (!size)
				continue;
			uint8_t *data = xmalloc(size);
			buffer_read_bytes(&r, data, size);
			dtx->entries[i * nr_items + j].size = size;
			dtx->entries[i * nr_items + j].data = data;
		}
	}
	return dtx;
}

void dtx_free(struct dtx *dtx)
{
	int nr_entries = dtx->nr_rows * dtx->nr_columns;
	for (int i = 0; i < nr_entries; i++) {
		if (dtx->entries[i].data)
			free(dtx->entries[i].data);
	}
	free(dtx->entries);
	free(dtx);
}

struct cg *dtx_create_cg(struct dtx *dtx, int type, int index)
{
	if (type < 0 || type >= dtx->nr_rows || index < 0 || index >= dtx->nr_columns)
		return NULL;
	struct dtx_entry *entry = &dtx->entries[type * dtx->nr_columns + index];
	if (!entry->data)
		return NULL;
	return cg_load_buffer(entry->data, entry->size);
}
