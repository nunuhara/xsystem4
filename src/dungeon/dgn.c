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
	uint32_t x = 0, y = 0, z = 0;
	for (int i = 0; i < nr_cells; i++) {
		struct dgn_cell *cell = &dgn->cells[i];
		cell->x = x;
		cell->y = y;
		cell->z = z;
		if (++x == dgn->size_x) {
			x = 0;
			if (++y == dgn->size_y) {
				y = 0;
				z++;
			}
		}
		cell->event_blend_rate = 255;

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
		buffer_skip(&r, 13 * 4);
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
		buffer_skip(&r, 4);
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
	if (buffer_read_u8(&r) != 0 || buffer_read_int32(&r) != 1) {
		dgn_free(dgn);
		return NULL;
	}
	dgn->pvs = xcalloc(nr_cells, sizeof(struct packed_pvs));
	for (int i = 0; i < nr_cells; i++) {
		struct packed_pvs *pvs = &dgn->pvs[i];
		int32_t len = buffer_read_int32(&r);
		if (len % 8 != 4) {
			WARNING("dgn_parse: unexpected PVS length");
			dgn_free(dgn);
			return NULL;
		}
		if (buffer_read_int32(&r) != nr_cells) {
			WARNING("dgn_parse: bad PVS");
			dgn_free(dgn);
			return NULL;
		}
		pvs->nr_run_lengths = (len - 4) / 8;
		pvs->run_lengths = xmalloc(pvs->nr_run_lengths * sizeof(struct pvs_run_lengths));
		int32_t total = 0;
		for (int j = 0; j < pvs->nr_run_lengths; j++) {
			int32_t invisible = buffer_read_int32(&r);
			int32_t visible = buffer_read_int32(&r);
			pvs->run_lengths[j].invisible_cells = invisible;
			pvs->run_lengths[j].visible_cells = visible;
			pvs->nr_visible_cells += visible;
			total += invisible + visible;
		}
		if (total != nr_cells) {
			WARNING("dgn_parse: bad PVS");
			dgn_free(dgn);
			return NULL;
		}
	}
	return dgn;
}

void dgn_free(struct dgn *dgn)
{
	int nr_cells = dgn->size_x * dgn->size_y * dgn->size_z;

	for (int i = 0; i < nr_cells; i++) {
		if (dgn->cells[i].visible_cells)
			free(dgn->cells[i].visible_cells);
	}
	free(dgn->cells);

	if (dgn->pvs) {
		for (int i = 0; i < nr_cells; i++)
			free(dgn->pvs[i].run_lengths);
		free(dgn->pvs);
	}

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

struct pvs_cell {
	struct dgn_cell *cell;
	int distance;
};

static int pvs_cell_compare(const void *a, const void *b)
{
	return ((struct pvs_cell *)a)->distance - ((struct pvs_cell *)b)->distance;
}

struct dgn_cell **dgn_get_visible_cells(struct dgn *dgn, int x, int y, int z, int *nr_cells_out)
{
	int cell_index = dgn_cell_index(dgn, x, y, z);
	struct dgn_cell *cell = &dgn->cells[cell_index];
	struct packed_pvs *pvs = &dgn->pvs[cell_index];
	if (!cell->visible_cells) {
		struct pvs_cell *pvs_cells = xmalloc(pvs->nr_visible_cells * sizeof(struct pvs_cell));

		struct pvs_cell *dst = pvs_cells;
		struct dgn_cell *src = dgn->cells;
		for (int i = 0; i < pvs->nr_run_lengths; i++) {
			src += pvs->run_lengths[i].invisible_cells;
			for (int j = 0; j < pvs->run_lengths[i].visible_cells; j++) {
				int dx = src->x - x;
				int dy = src->y - y;
				int dz = src->z - z;
				*dst++ = (struct pvs_cell){
					.cell = src++,
					.distance = dx * dx + dy * dy + dz * dz
				};
			}
		}
		qsort(pvs_cells, pvs->nr_visible_cells, sizeof(struct pvs_cell), pvs_cell_compare);

		cell->visible_cells = xmalloc(pvs->nr_visible_cells * sizeof(struct cell *));
		for (int i = 0; i < pvs->nr_visible_cells; i++)
			cell->visible_cells[i] = pvs_cells[i].cell;
		free(pvs_cells);
	}
	*nr_cells_out = pvs->nr_visible_cells;
	return cell->visible_cells;
}
