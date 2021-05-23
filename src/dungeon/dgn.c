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

#include "dungeon/dgn.h"
#include "vm.h"

struct dgn *dgn_parse(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (strncmp(buffer_strdata(&r), "DUGN", 4))
		return NULL;
	buffer_skip(&r, 4);

	struct dgn *dgn = xcalloc(1, sizeof(struct dgn));
	dgn->version = buffer_read_int32(&r);
	if (dgn->version != DGN_VER_RANCE6 && dgn->version != DGN_VER_GALZOO) {
		WARNING("unknown DGN version: %d", dgn->version);
		free(dgn);
		return NULL;
	}
	dgn->size_x = buffer_read_int32(&r);
	dgn->size_y = buffer_read_int32(&r);
	dgn->size_z = buffer_read_int32(&r);
	buffer_skip(&r, 40);

	int nr_cells = dgn->size_x * dgn->size_y * dgn->size_z;
	dgn->cells = xcalloc(nr_cells, sizeof(struct dgn_cell));
	for (int i = 0; i < nr_cells; i++) {
		struct dgn_cell *cell = &dgn->cells[i];
		cell->floor = buffer_read_int32(&r);
		cell->ceiling = buffer_read_int32(&r);
		cell->north_wall = buffer_read_int32(&r);
		cell->south_wall = buffer_read_int32(&r);
		cell->east_wall = buffer_read_int32(&r);
		cell->west_wall = buffer_read_int32(&r);
		cell->north_door = buffer_read_int32(&r);
		cell->south_door = buffer_read_int32(&r);
		cell->east_door = buffer_read_int32(&r);
		cell->west_door = buffer_read_int32(&r);
		cell->stairs_texture = buffer_read_int32(&r);
		cell->stairs_orientation = buffer_read_int32(&r);
		cell->attr12 = buffer_read_int32(&r);
		cell->attr13 = buffer_read_int32(&r);
		cell->attr14 = buffer_read_int32(&r);
		cell->attr15 = buffer_read_int32(&r);
		cell->attr16 = buffer_read_int32(&r);
		cell->attr17 = buffer_read_int32(&r);
		cell->attr18 = buffer_read_int32(&r);
		cell->attr19 = buffer_read_int32(&r);
		cell->attr20 = buffer_read_int32(&r);
		cell->attr21 = buffer_read_int32(&r);
		cell->attr22 = buffer_read_int32(&r);
		cell->attr23 = buffer_read_int32(&r);
		cell->attr24 = buffer_read_int32(&r);
		cell->enterable = buffer_read_int32(&r);
		cell->enterable_north = buffer_read_int32(&r);
		cell->enterable_south = buffer_read_int32(&r);
		cell->enterable_east = buffer_read_int32(&r);
		cell->enterable_west = buffer_read_int32(&r);
		cell->floor_event = buffer_read_int32(&r);
		cell->north_event = buffer_read_int32(&r);
		cell->south_event = buffer_read_int32(&r);
		cell->east_event = buffer_read_int32(&r);
		cell->west_event = buffer_read_int32(&r);
		// Skip (int32, string) pairs
		for (int j = 0; j < 6; j++) {
			buffer_skip(&r, 4);
			buffer_skip(&r, strlen(buffer_strdata(&r)) + 1);
		}
		cell->unknown = buffer_read_int32(&r);
		cell->battle_background = buffer_read_int32(&r);
		if (dgn->version != DGN_VER_GALZOO)
			continue;

		cell->polyobj_index = buffer_read_int32(&r);
		cell->polyobj_scale = buffer_read_float(&r);
		cell->polyobj_rotation_y = buffer_read_float(&r);
		cell->polyobj_rotation_z = buffer_read_float(&r);
		cell->polyobj_rotation_x = buffer_read_float(&r);
		cell->polyobj_position_x = buffer_read_float(&r);
		cell->polyobj_position_y = buffer_read_float(&r);
		cell->polyobj_position_z = buffer_read_float(&r);
		cell->roof_orientation = buffer_read_int32(&r);
		cell->roof_texture = buffer_read_int32(&r);
		buffer_skip(&r, 4);
		cell->roof_underside_texture = buffer_read_int32(&r);
		buffer_skip(&r, 4);
	}
	return dgn;
}

void dgn_free(struct dgn *dgn)
{
	free(dgn->cells);
	free(dgn);
}

int dgn_cell_index(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z)
{
	if (x >= dgn->size_x || y >= dgn->size_y || z >= dgn->size_z)
		VM_ERROR("cell location out of range: (%d, %d, %d)", x, y, z);
	return (z * dgn->size_y + y) * dgn->size_x + x;
}

struct dgn_cell *dgn_cell_at(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z)
{
	return &dgn->cells[dgn_cell_index(dgn, x, y, z)];
}
