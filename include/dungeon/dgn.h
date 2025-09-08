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

#ifndef SYSTEM4_DGN_H
#define SYSTEM4_DGN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cglm/cglm.h>

/*
 * Dungeon data is a collection of cells indexed by three integers (x, y, z).
 * X increases from west to east, Y increases from down to up, Z increases from
 * south to north.
 */

enum dgn_version {
	DGN_VER_RANCE6 = 10,
	DGN_VER_GALZOO = 13,
};

struct dgn_cell {
	int32_t floor;
	int32_t ceiling;
	int32_t north_wall;
	int32_t south_wall;
	int32_t east_wall;
	int32_t west_wall;
	int32_t north_door;
	int32_t south_door;
	int32_t east_door;
	int32_t west_door;
	int32_t stairs_texture;
	int32_t stairs_orientation;
	int32_t lightmap_floor;
	int32_t lightmap_ceiling;
	int32_t lightmap_north;
	int32_t lightmap_south;
	int32_t lightmap_east;
	int32_t lightmap_west;
	// int32_t unknown[7];
	int32_t enterable;
	int32_t enterable_north;
	int32_t enterable_south;
	int32_t enterable_east;
	int32_t enterable_west;
	int32_t floor_event;
	int32_t north_event;
	int32_t south_event;
	int32_t east_event;
	int32_t west_event;
	// struct {
	//     uint32_t i;
	//     char *s;
	// } pairs[6];
	// int32_t unknown;
	int32_t battle_background;

	// The below exist only in version 13
	int32_t polyobj_index;
	float polyobj_scale;
	float polyobj_rotation_y;
	float polyobj_rotation_z;
	float polyobj_rotation_x;
	float polyobj_position_x;
	float polyobj_position_y;
	float polyobj_position_z;
	int32_t roof_orientation;
	int32_t roof_texture;
	// int32_t unused1;  // always -1
	int32_t roof_underside_texture;
	// int32_t unused2;  // always -1

	// Runtime data (not stored in .dgn)
	int x, y, z;
	int walked;
	int event_blend_rate;
	float north_door_angle;
	float south_door_angle;
	float east_door_angle;
	float west_door_angle;
	int north_door_lock;
	int south_door_lock;
	int east_door_lock;
	int west_door_lock;
	int32_t floor_event2;
	int32_t north_event2;
	int32_t south_event2;
	int32_t east_event2;
	int32_t west_event2;
	int pathfinding_cost;
	struct dgn_cell **visible_cells;
};

struct pvs_run_lengths {
	int32_t invisible_cells;
	int32_t visible_cells;
};

struct packed_pvs {
	struct pvs_run_lengths *run_lengths;
	int nr_run_lengths;
	int nr_visible_cells;
};

struct dgn {
	uint32_t size_x;
	uint32_t size_y;
	uint32_t size_z;
	// uint32_t unknown[10];
	struct dgn_cell *cells;
	struct packed_pvs *pvs;
	vec3 sphere_theta;
	float sphere_color_top;
	float sphere_color_bottom;

	// for generated dungeons
	int start_x, start_y;
	int exit_x, exit_y;

	// DrawField
	int32_t back_color_r;
	int32_t back_color_g;
	int32_t back_color_b;
};

struct dgn *dgn_new(uint32_t size_x, uint32_t size_y, uint32_t size_z);
struct dgn *dgn_parse(uint8_t *data, size_t size, bool for_draw_field);
void dgn_free(struct dgn *dgn);

int dgn_cell_index(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z);
struct dgn_cell *dgn_cell_at(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z);
int dgn_calc_conquer(struct dgn *dgn);

static inline int dgn_nr_cells(struct dgn *dgn)
{
	return dgn->size_x * dgn->size_y * dgn->size_z;
}

static inline bool dgn_is_in_map(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z)
{
	return x < dgn->size_x && y < dgn->size_y && z < dgn->size_z;
}

// Returns a list of cells visible from (x, y, z), sorted by distance from (x, y, z).
struct dgn_cell **dgn_get_visible_cells(struct dgn *dgn, int x, int y, int z, int *nr_cells_out);
void dgn_calc_lightmap(struct dgn *dgn);

#endif /* SYSTEM4_DGN_H */
