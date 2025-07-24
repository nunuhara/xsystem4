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

#ifndef SYSTEM4_DUNGEON_MAP_H
#define SYSTEM4_DUNGEON_MAP_H

struct dungeon_context;
struct dungeon_map;

struct dungeon_map *dungeon_map_create(enum draw_dungeon_version version);
void dungeon_map_free(struct dungeon_map *map);
void dungeon_map_init(struct dungeon_context *ctx);
void dungeon_map_update_cell(struct dungeon_context *ctx, int x, int y, int z);

void dungeon_map_draw(int surface, int sprite);
void dungeon_map_draw_lmap(int surface, int sprite);
void dungeon_map_set_radar(int surface, int flag);
void dungeon_map_set_all_view(int surface, int flag);
void dungeon_map_set_cg(int surface, int index, int sprite);
void dungeon_map_set_small_map_floor(int surface, int floor);
void dungeon_map_set_large_map_floor(int surface, int floor);
void dungeon_map_reveal(struct dungeon_context *ctx, int x, int y, int z, bool from_walk_data);
void dungeon_map_hide(struct dungeon_context *ctx, int x, int y, int z);

#endif /* SYSTEM4_DUNGEON_MAP_H */
