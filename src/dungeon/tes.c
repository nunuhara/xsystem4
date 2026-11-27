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

#include "dungeon/tes.h"

struct tes *tes_parse(uint8_t *data, size_t size)
{
	if (!data || size < 16)
		return NULL;
	struct buffer r;
	buffer_init(&r, data, size);
	if (memcmp(buffer_strdata(&r), "TES\0\0\0\0\0", 8))
		return NULL;
	buffer_skip(&r, 8);

	int nr_columns = buffer_read_int32(&r);
	int nr_rows = buffer_read_int32(&r);
	if (nr_columns < 0 || nr_rows < 0 ||
	    (nr_rows > 0 && (size_t)nr_columns > (buffer_remaining(&r) / 4) / (size_t)nr_rows)) {
		return NULL;
	}
	size_t nr_items = (size_t)nr_rows * (size_t)nr_columns;
	struct tes *tes = xcalloc(1, sizeof(struct tes));
	tes->nr_columns = nr_columns;
	tes->nr_rows = nr_rows;
	tes->entries = xcalloc(nr_items, sizeof(int));
	for (size_t i = 0; i < nr_items; i++)
		tes->entries[i] = buffer_read_int32(&r);
	return tes;
}

void tes_free(struct tes *tes)
{
	free(tes->entries);
	free(tes);
}
