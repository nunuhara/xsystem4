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

#ifndef SYSTEM4_TES_H
#define SYSTEM4_TES_H

#include <stdint.h>
#include <stddef.h>

struct tes {
	int nr_columns;
	int nr_rows;
	int *entries;
};

struct tes *tes_parse(uint8_t *data, size_t size);
void tes_free(struct tes *tes);

static inline int tes_get(struct tes *tes, int type, int index)
{
	if (type < 0 || type >= tes->nr_rows || index < 0 || index >= tes->nr_columns)
		return -1;
	return tes->entries[type * tes->nr_columns + index];
}

#endif /* SYSTEM4_TES_H */
