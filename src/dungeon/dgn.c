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

struct dgn *dgn_new(uint32_t size_x, uint32_t size_y, uint32_t size_z)
{
	struct dgn *dgn = xcalloc(1, sizeof(struct dgn));
	dgn->size_x = size_x;
	dgn->size_y = size_y;
	dgn->size_z = size_z;
	int nr_cells = dgn_nr_cells(dgn);
	dgn->cells = xcalloc(nr_cells, sizeof(struct dgn_cell));
	uint32_t x = 0, y = 0, z = 0;
	for (int i = 0; i < nr_cells; i++) {
		struct dgn_cell *cell = &dgn->cells[i];
		cell->x = x;
		cell->y = y;
		cell->z = z;
		if (++x == size_x) {
			x = 0;
			if (++y == size_y) {
				y = 0;
				z++;
			}
		}
		cell->event_blend_rate = 255;

		cell->floor = -1;
		cell->ceiling = -1;
		cell->north_wall = -1;
		cell->south_wall = -1;
		cell->east_wall = -1;
		cell->west_wall = -1;
		cell->north_door = -1;
		cell->south_door = -1;
		cell->east_door = -1;
		cell->west_door = -1;
		cell->stairs_texture = -1;
		cell->stairs_orientation = -1;
		cell->lightmap_floor = -1;
		cell->lightmap_ceiling = -1;
		cell->lightmap_north = -1;
		cell->lightmap_south = -1;
		cell->lightmap_east = -1;
		cell->lightmap_west = -1;
		cell->battle_background = -1;
		cell->polyobj_index = -1;
		cell->roof_orientation = -1;
		cell->roof_texture = -1;
		cell->roof_underside_texture = -1;
		cell->pathfinding_cost = -1;
	}
	return dgn;
}

struct dgn *dgn_parse(uint8_t *data, size_t size, bool for_draw_field)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (strncmp(buffer_strdata(&r), "DUGN", 4))
		return NULL;
	buffer_skip(&r, 4);

	uint32_t version = buffer_read_int32(&r);
	uint32_t size_x = buffer_read_int32(&r);
	uint32_t size_y = buffer_read_int32(&r);
	uint32_t size_z = buffer_read_int32(&r);
	buffer_skip(&r, 40);

	struct dgn *dgn = dgn_new(size_x, size_y, size_z);
	int nr_cells = dgn_nr_cells(dgn);
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
		cell->lightmap_floor = buffer_read_int32(&r);
		cell->lightmap_ceiling = buffer_read_int32(&r);
		cell->lightmap_north = buffer_read_int32(&r);
		cell->lightmap_south = buffer_read_int32(&r);
		cell->lightmap_east = buffer_read_int32(&r);
		cell->lightmap_west = buffer_read_int32(&r);
		buffer_skip(&r, 7 * 4);  // lightmap parameters for doors / stairs, 2 unknowns
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
		if (for_draw_field) {
			if (version >= 9) {
				cell->north_door_lock = buffer_read_int32(&r);
				cell->west_door_lock = buffer_read_int32(&r);
				cell->south_door_lock = buffer_read_int32(&r);
				cell->east_door_lock = buffer_read_int32(&r);
				cell->north_door_angle = buffer_read_float(&r);
				cell->west_door_angle = buffer_read_float(&r);
				cell->south_door_angle = buffer_read_float(&r);
				cell->east_door_angle = buffer_read_float(&r);
				cell->walked = buffer_read_int32(&r);
			}
			if (version >= 11) {
				cell->polyobj_index = buffer_read_int32(&r);
				cell->polyobj_scale = buffer_read_float(&r);
				cell->polyobj_rotation_y = buffer_read_float(&r);
				cell->polyobj_rotation_z = buffer_read_float(&r);
				cell->polyobj_rotation_x = buffer_read_float(&r);
				cell->polyobj_position_x = buffer_read_float(&r);
				cell->polyobj_position_y = buffer_read_float(&r);
				cell->polyobj_position_z = buffer_read_float(&r);
			}
		} else {
			buffer_skip(&r, 4);  // unknown
			cell->battle_background = buffer_read_int32(&r);
			if (version != DGN_VER_GALZOO) {
				continue;
			}
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
			buffer_skip(&r, 4);  // unknown
			cell->roof_underside_texture = buffer_read_int32(&r);
			buffer_skip(&r, 4);  // unknown
		}
	}
	if (buffer_read_u8(&r) != 0) {
		dgn_free(dgn);
		return NULL;
	}
	int has_pvs = buffer_read_int32(&r);
	if (has_pvs) {
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
	}
	dgn->sphere_theta[0] = buffer_read_float(&r);
	dgn->sphere_theta[1] = buffer_read_float(&r);
	dgn->sphere_theta[2] = buffer_read_float(&r);
	dgn->sphere_color_top = buffer_read_float(&r);
	dgn->sphere_color_bottom = buffer_read_float(&r);
	buffer_skip(&r, 4);  // unknown
	if (for_draw_field && version >= 10) {
		dgn->back_color_r = buffer_read_int32(&r);
		dgn->back_color_g = buffer_read_int32(&r);
		dgn->back_color_b = buffer_read_int32(&r);
	}
	return dgn;
}

void dgn_free(struct dgn *dgn)
{
	int nr_cells = dgn_nr_cells(dgn);

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

int dgn_calc_conquer(struct dgn *dgn)
{
	int nr_cells = dgn_nr_cells(dgn);
	int enterable = 0;
	int walked = 0;
	for (int i = 0; i < nr_cells; i++) {
		if (!dgn->cells[i].enterable)
			continue;
		enterable++;
		if (dgn->cells[i].walked)
			walked++;
	}
	return walked * 100 / enterable;
}

struct pvs_cell {
	struct dgn_cell *cell;
	int distance;
};

static int pvs_cell_compare(const void *a, const void *b)
{
	return ((struct pvs_cell *)a)->distance - ((struct pvs_cell *)b)->distance;
}

static struct dgn_cell **dgn_get_visible_cells_pvs(struct dgn *dgn, int x, int y, int z, int *nr_cells_out)
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

static struct dgn_cell **dgn_get_visible_cells_nopvs(struct dgn *dgn, int x, int y, int z, int *nr_cells_out)
{
	int cell_index = dgn_cell_index(dgn, x, y, z);
	struct dgn_cell *cell = &dgn->cells[cell_index];
	int nr_cells = dgn_nr_cells(dgn);

	if (!cell->visible_cells) {
		struct pvs_cell *pvs_cells = xmalloc(nr_cells * sizeof(struct pvs_cell));
		for (int i = 0; i < nr_cells; i++) {
			struct dgn_cell *c = &dgn->cells[i];
			int dx = c->x - x;
			int dy = c->y - y;
			int dz = c->z - z;
			pvs_cells[i].cell = c;
			pvs_cells[i].distance = dx * dx + dy * dy + dz * dz;
		}
		qsort(pvs_cells, nr_cells, sizeof(struct pvs_cell), pvs_cell_compare);

		cell->visible_cells = xmalloc(nr_cells * sizeof(struct dgn_cell *));
		for (int i = 0; i < nr_cells; i++) {
			cell->visible_cells[i] = pvs_cells[i].cell;
		}
		free(pvs_cells);
	}
	*nr_cells_out = nr_cells;
	return cell->visible_cells;
}

struct dgn_cell **dgn_get_visible_cells(struct dgn *dgn, int x, int y, int z, int *nr_cells_out)
{
	if (dgn->pvs)
		return dgn_get_visible_cells_pvs(dgn, x, y, z, nr_cells_out);
	else
		return dgn_get_visible_cells_nopvs(dgn, x, y, z, nr_cells_out);
}

#define LM_N (1 << 0)
#define LM_S (1 << 1)
#define LM_W (1 << 2)
#define LM_E (1 << 3)
#define LM_NW (1 << 4)
#define LM_NE (1 << 5)
#define LM_SW (1 << 6)
#define LM_SE (1 << 7)

static int lightmap_texture_index(uint8_t flags)
{
	if (flags & LM_N || flags & LM_W) flags &= ~LM_NW;
	if (flags & LM_N || flags & LM_E) flags &= ~LM_NE;
	if (flags & LM_S || flags & LM_W) flags &= ~LM_SW;
	if (flags & LM_S || flags & LM_E) flags &= ~LM_SE;

	switch (flags) {
	case LM_N | LM_W | LM_E: return 0;
	case LM_W | LM_E: return 1;
	case LM_S | LM_W | LM_E: return 2;
	case LM_N | LM_S | LM_W: return 3;
	case LM_N | LM_S: return 4;
	case LM_N | LM_S | LM_E: return 5;
	case LM_N | LM_W: return 6;
	case LM_N: return 7;
	case LM_N | LM_E: return 8;
	case LM_W: return 9;
	case LM_E: return 10;
	case LM_S | LM_W: return 11;
	case LM_S: return 12;
	case LM_S | LM_E: return 13;
	case LM_N | LM_S | LM_W | LM_E: return 14;
	case LM_W | LM_NE | LM_SE: return 15;
	case LM_N | LM_SW | LM_SE: return 16;
	case LM_E | LM_NW | LM_SW: return 17;
	case LM_S | LM_NW | LM_NE: return 18;
	case LM_NW | LM_NE | LM_SW | LM_SE: return 19;
	case LM_W | LM_NE: return 20;
	case LM_N | LM_SE: return 21;
	case LM_E | LM_SW: return 22;
	case LM_S | LM_NW: return 23;
	case LM_NE: return 24;
	case LM_W | LM_SE: return 25;
	case LM_N | LM_SW: return 26;
	case LM_E | LM_NW: return 27;
	case LM_S | LM_NE: return 28;
	case LM_SE: return 29;
	case LM_NE | LM_SE: return 30;
	case LM_SW | LM_SE: return 31;
	case LM_NW | LM_SW: return 32;
	case LM_NW | LM_NE: return 33;
	case LM_SW: return 34;
	case LM_NW: return 35;
	case LM_N | LM_W | LM_SE: return 36;
	case LM_N | LM_E | LM_SW: return 37;
	case LM_S | LM_E | LM_NW: return 38;
	case LM_S | LM_W | LM_NE: return 39;
	case LM_NW | LM_NE | LM_SW: return 40;
	case LM_NW | LM_NE | LM_SE: return 41;
	case LM_NE | LM_SW | LM_SE: return 42;
	case LM_NW | LM_SW | LM_SE: return 43;
	case LM_NE | LM_SW: return 44;
	case LM_NW | LM_SE: return 45;
	default: return -1;
	};
}

static int calc_floor_lightmap(struct dgn *dgn, struct dgn_cell *cell,
	struct dgn_cell *north_cell, struct dgn_cell *south_cell,
	struct dgn_cell *west_cell, struct dgn_cell *east_cell)
{
	if (cell->floor == -1)
		return -1;

	uint8_t flags = 0;
	if (cell->north_wall != -1 || cell->north_door != -1 ||
		(north_cell && north_cell->stairs_texture != -1 && north_cell->stairs_orientation == 0))
		flags |= LM_N;
	if (cell->south_wall != -1 || cell->south_door != -1 ||
		(south_cell && south_cell->stairs_texture != -1 && south_cell->stairs_orientation == 2))
		flags |= LM_S;
	if (cell->west_wall != -1 || cell->west_door != -1 ||
		(west_cell && west_cell->stairs_texture != -1 && west_cell->stairs_orientation == 1))
		flags |= LM_W;
	if (cell->east_wall != -1 || cell->east_door != -1 ||
		(east_cell && east_cell->stairs_texture != -1 && east_cell->stairs_orientation == 3))
		flags |= LM_E;
	if ((north_cell && (north_cell->west_wall != -1 || north_cell->west_door != -1)) ||
		(west_cell && (west_cell->north_wall != -1 || west_cell->north_door != -1)))
		flags |= LM_NW;
	if ((north_cell && (north_cell->east_wall != -1 || north_cell->east_door != -1)) ||
		(east_cell && (east_cell->north_wall != -1 || east_cell->north_door != -1)))
		flags |= LM_NE;
	if ((south_cell && (south_cell->west_wall != -1 || south_cell->west_door != -1)) ||
		(west_cell && (west_cell->south_wall != -1 || west_cell->south_door != -1)))
		flags |= LM_SW;
	if ((south_cell && (south_cell->east_wall != -1 || south_cell->east_door != -1)) ||
		(east_cell && (east_cell->south_wall != -1 || east_cell->south_door != -1)))
		flags |= LM_SE;
	return lightmap_texture_index(flags);
}

static int calc_ceiling_lightmap(struct dgn *dgn, struct dgn_cell *cell,
	struct dgn_cell *north_cell, struct dgn_cell *south_cell,
	struct dgn_cell *west_cell, struct dgn_cell *east_cell)
{
	if (cell->ceiling == -1)
		return -1;

	uint8_t flags = 0;
	if (cell->north_wall != -1 || cell->north_door != -1)
		flags |= LM_N;
	if (cell->south_wall != -1 || cell->south_door != -1)
		flags |= LM_S;
	if (cell->west_wall != -1 || cell->west_door != -1)
		flags |= LM_E;
	if (cell->east_wall != -1 || cell->east_door != -1)
		flags |= LM_W;
	if ((north_cell && (north_cell->east_wall != -1 || north_cell->east_door != -1)) ||
		(east_cell && (east_cell->north_wall != -1 || east_cell->north_door != -1)))
		flags |= LM_NW;
	if ((north_cell && (north_cell->west_wall != -1 || north_cell->west_door != -1)) ||
		(west_cell && (west_cell->north_wall != -1 || west_cell->north_door != -1)))
		flags |= LM_NE;
	if ((south_cell && (south_cell->east_wall != -1 || south_cell->east_door != -1)) ||
		(east_cell && (east_cell->south_wall != -1 || east_cell->south_door != -1)))
		flags |= LM_SW;
	if ((south_cell && (south_cell->west_wall != -1 || south_cell->west_door != -1)) ||
		(west_cell && (west_cell->south_wall != -1 || west_cell->south_door != -1)))
		flags |= LM_SE;
	return lightmap_texture_index(flags);
}

static int calc_north_lightmap(struct dgn *dgn, struct dgn_cell *cell,
	struct dgn_cell *up_cell, struct dgn_cell *down_cell,
	struct dgn_cell *west_cell, struct dgn_cell *east_cell)
{
	if (cell->north_wall == -1)
		return -1;

	uint8_t flags = 0;
	if (cell->ceiling != -1)
		flags |= LM_N;
	if (cell->floor != -1)
		flags |= LM_S;
	if (cell->west_wall != -1 || cell->west_door != -1)
		flags |= LM_W;
	if (cell->east_wall != -1 || cell->east_door != -1)
		flags |= LM_E;
	if ((up_cell && (up_cell->west_wall != -1 || up_cell->west_door != -1)) ||
		(west_cell && (west_cell->ceiling != -1)))
		flags |= LM_NW;
	if ((up_cell && (up_cell->east_wall != -1 || up_cell->east_door != -1)) ||
		(east_cell && (east_cell->ceiling != -1)))
		flags |= LM_NE;
	if ((down_cell && (down_cell->west_wall != -1 || down_cell->west_door != -1)) ||
		(west_cell && (west_cell->floor != -1)))
		flags |= LM_SW;
	if ((down_cell && (down_cell->east_wall != -1 || down_cell->east_door != -1)) ||
		(east_cell && (east_cell->floor != -1)))
		flags |= LM_SE;
	return lightmap_texture_index(flags);
}

static int calc_south_lightmap(struct dgn *dgn, struct dgn_cell *cell,
	struct dgn_cell *up_cell, struct dgn_cell *down_cell,
	struct dgn_cell *west_cell, struct dgn_cell *east_cell)
{
	if (cell->south_wall == -1)
		return -1;

	uint8_t flags = 0;
	if (cell->ceiling != -1)
		flags |= LM_N;
	if (cell->floor != -1)
		flags |= LM_S;
	if (cell->east_wall != -1 || cell->east_door != -1)
		flags |= LM_W;
	if (cell->west_wall != -1 || cell->west_door != -1)
		flags |= LM_E;
	if ((up_cell && (up_cell->east_wall != -1 || up_cell->east_door != -1)) ||
		(east_cell && (east_cell->ceiling != -1)))
		flags |= LM_NW;
	if ((up_cell && (up_cell->west_wall != -1 || up_cell->west_door != -1)) ||
		(west_cell && (west_cell->ceiling != -1)))
		flags |= LM_NE;
	if ((down_cell && (down_cell->east_wall != -1 || down_cell->east_door != -1)) ||
		(east_cell && (east_cell->floor != -1)))
		flags |= LM_SW;
	if ((down_cell && (down_cell->west_wall != -1 || down_cell->west_door != -1)) ||
		(west_cell && (west_cell->floor != -1)))
		flags |= LM_SE;
	return lightmap_texture_index(flags);
}

static int calc_west_lightmap(struct dgn *dgn, struct dgn_cell *cell,
	struct dgn_cell *up_cell, struct dgn_cell *down_cell,
	struct dgn_cell *north_cell, struct dgn_cell *south_cell)
{
	if (cell->west_wall == -1)
		return -1;

	uint8_t flags = 0;
	if (cell->ceiling != -1)
		flags |= LM_N;
	if (cell->floor != -1)
		flags |= LM_S;
	if (cell->south_wall != -1 || cell->south_door != -1)
		flags |= LM_W;
	if (cell->north_wall != -1 || cell->north_door != -1)
		flags |= LM_E;
	if ((up_cell && (up_cell->south_wall != -1 || up_cell->south_door != -1)) ||
		(south_cell && (south_cell->ceiling != -1)))
		flags |= LM_NW;
	if ((up_cell && (up_cell->north_wall != -1 || up_cell->north_door != -1)) ||
		(north_cell && (north_cell->ceiling != -1)))
		flags |= LM_NE;
	if ((down_cell && (down_cell->south_wall != -1 || down_cell->south_door != -1)) ||
		(south_cell && (south_cell->floor != -1)))
		flags |= LM_SW;
	if ((down_cell && (down_cell->north_wall != -1 || down_cell->north_door != -1)) ||
		(north_cell && (north_cell->floor != -1)))
		flags |= LM_SE;
	return lightmap_texture_index(flags);
}

static int calc_east_lightmap(struct dgn *dgn, struct dgn_cell *cell,
	struct dgn_cell *up_cell, struct dgn_cell *down_cell,
	struct dgn_cell *north_cell, struct dgn_cell *south_cell)
{
	if (cell->east_wall == -1)
		return -1;

	uint8_t flags = 0;
	if (cell->ceiling != -1)
		flags |= LM_N;
	if (cell->floor != -1)
		flags |= LM_S;
	if (cell->north_wall != -1 || cell->north_door != -1)
		flags |= LM_W;
	if (cell->south_wall != -1 || cell->south_door != -1)
		flags |= LM_E;
	if ((up_cell && (up_cell->north_wall != -1 || up_cell->north_door != -1)) ||
		(north_cell && (north_cell->ceiling != -1)))
		flags |= LM_NW;
	if ((up_cell && (up_cell->south_wall != -1 || up_cell->south_door != -1)) ||
		(south_cell && (south_cell->ceiling != -1)))
		flags |= LM_NE;
	if ((down_cell && (down_cell->north_wall != -1 || down_cell->north_door != -1)) ||
		(north_cell && (north_cell->floor != -1)))
		flags |= LM_SW;
	if ((down_cell && (down_cell->south_wall != -1 || down_cell->south_door != -1)) ||
		(south_cell && (south_cell->floor != -1)))
		flags |= LM_SE;
	return lightmap_texture_index(flags);
}

void dgn_calc_lightmap(struct dgn *dgn)
{
	int nr_cells = dgn_nr_cells(dgn);
	for (int i = 0; i < nr_cells; i++) {
		struct dgn_cell *cell = &dgn->cells[i];
		struct dgn_cell *north = cell->z + 1 < dgn->size_z
			? dgn_cell_at(dgn, cell->x, cell->y, cell->z + 1) : NULL;
		struct dgn_cell *south = cell->z > 0
			? dgn_cell_at(dgn, cell->x, cell->y, cell->z - 1) : NULL;
		struct dgn_cell *west = cell->x > 0
			? dgn_cell_at(dgn, cell->x - 1, cell->y, cell->z) : NULL;
		struct dgn_cell *east = cell->x + 1 < dgn->size_x
			? dgn_cell_at(dgn, cell->x + 1, cell->y, cell->z) : NULL;
		struct dgn_cell *up = cell->y + 1 < dgn->size_y
			? dgn_cell_at(dgn, cell->x, cell->y + 1, cell->z) : NULL;
		struct dgn_cell *down = cell->y > 0
			? dgn_cell_at(dgn, cell->x, cell->y - 1, cell->z) : NULL;
		cell->lightmap_floor = calc_floor_lightmap(dgn, cell, north, south, west, east);
		cell->lightmap_ceiling = calc_ceiling_lightmap(dgn, cell, north, south, west, east);
		cell->lightmap_north = calc_north_lightmap(dgn, cell, up, down, west, east);
		cell->lightmap_south = calc_south_lightmap(dgn, cell, up, down, west, east);
		cell->lightmap_west = calc_west_lightmap(dgn, cell, up, down, north, south);
		cell->lightmap_east = calc_east_lightmap(dgn, cell, up, down, north, south);
	}
}
