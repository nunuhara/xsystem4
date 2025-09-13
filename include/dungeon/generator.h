/* Copyright (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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
#ifndef SYSTEM4_DUNGEON_GENERATOR_H
#define SYSTEM4_DUNGEON_GENERATOR_H

struct dgn;

enum drawfield_cell_flag {
	DF_FLAG_ITEM          = 1 << 0,
	DF_FLAG_ENEMY         = 1 << 1,
	DF_FLAG_TRAP          = 1 << 2,
	DF_FLAG_NO_TREASURE   = 1 << 3,
	DF_FLAG_NO_MONSTER    = 1 << 4,
	DF_FLAG_NO_TRAP       = 1 << 5,
	DF_FLAG_TREASURE_ROOM = 1 << 6
};

struct dgn *dgn_generate_drawdungeon2(int level);
void dgn_paint_step(struct dgn *dgn, int x, int y);

struct dgn *dgn_generate_drawfield(
	int floor, int complexity, int wall_arrange_method, int floor_arrange_method,
	int field_size_x, int field_size_y, int door_lock_percent, uint32_t seed,
	uint8_t *cell_flags);

#endif /* SYSTEM4_DUNGEON_GENERATOR_H */
