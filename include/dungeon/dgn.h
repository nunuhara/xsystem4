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
	int32_t attr12;
	int32_t attr13;
	int32_t attr14;
	int32_t attr15;
	int32_t attr16;
	int32_t attr17;
	int32_t attr18;
	int32_t attr19;
	int32_t attr20;
	int32_t attr21;
	int32_t attr22;
	int32_t attr23;
	int32_t attr24;
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
	int32_t unknown;
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
	int32_t floor_event2;
	int32_t north_event2;
	int32_t south_event2;
	int32_t east_event2;
	int32_t west_event2;
};

struct dgn {
	uint32_t version; // 10: Rance VI, 13: GALZOO Island
	uint32_t size_x;
	uint32_t size_y;
	uint32_t size_z;
	// uint32_t unknown[10];
	struct dgn_cell *cells;
};

struct dgn *dgn_parse(uint8_t *data, size_t size);
void dgn_free(struct dgn *dgn);

int dgn_cell_index(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z);
struct dgn_cell *dgn_cell_at(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z);

static inline bool dgn_is_in_map(struct dgn *dgn, uint32_t x, uint32_t y, uint32_t z)
{
	return x < dgn->size_x && y < dgn->size_y && z < dgn->size_z;
}

#endif /* SYSTEM4_DGN_H */
