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

#ifndef SYSTEM4_DUNGEON_H
#define SYSTEM4_DUNGEON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <GL/glew.h>

struct page;
struct dgn;
struct dtx;
struct tes;
struct dungeon_renderer;
struct dungeon_map;

enum draw_dungeon_version {
	DRAW_DUNGEON_1,    // Rance VI
	// DRAW_DUNGEON_2, // Dungeons & Dolls
	DRAW_DUNGEON_14    // GALZOO Island
};

struct camera {
	float pos[3];
	float angle;
	float angle_p;
};

/*
 * Dungeon rendering context.
 *
 * A dungeon cell at (x, y, z) is rendered in a 2x2x2 cube centered at
 * (2*x, 2*y, -2*z) in the world coordinates. Note that Z is flipped, because
 * OpenGL uses a right-handed coordinate system while dungeon data is left-
 * handed.
 */

struct dungeon_context {
	enum draw_dungeon_version version;
	int surface;
	bool loaded;
	bool draw_enabled;
	struct dungeon_map *map;
	struct camera camera;
	struct dgn *dgn;
	struct dtx *dtx;
	struct tes *tes;
	struct dungeon_renderer *renderer;
	GLuint depth_buffer;
};

struct dungeon_context *dungeon_context_create(enum draw_dungeon_version version, int surface);
void dungeon_context_free(struct dungeon_context *ctx);
bool dungeon_load(struct dungeon_context *ctx, int num);
void dungeon_set_camera(int surface, float x, float y, float z, float angle, float angle_p);
void dungeon_render(struct dungeon_context *ctx);
void dungeon_set_walked(int surface, int x, int y, int z, int flag);
int dungeon_get_walked(int surface, int x, int y, int z);
int dungeon_calc_conquer(int surface);
bool dungeon_load_walk_data(int surface, int map, struct page **page);
bool dungeon_save_walk_data(int surface, int map, struct page **page);

struct dungeon_context *dungeon_get_context(int surface);
void dungeon_update(void);

#endif /* SYSTEM4_DUNGEON_H */
