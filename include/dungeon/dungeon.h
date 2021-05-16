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

struct dgn;
struct dtx;
struct tes;
struct mesh;
struct skybox;
struct event_markers;
struct dungeon_map;

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
	int surface;
	bool loaded;
	bool draw_enabled;
	struct dungeon_map *map;
	struct camera camera;
	struct dgn *dgn;
	struct dtx *dtx;
	struct tes *tes;
	struct mesh **meshes;
	int nr_meshes;
	struct skybox *skybox;
	struct event_markers *events;
	GLuint depth_buffer;
};

struct dungeon_context *dungeon_context_create(int surface);
void dungeon_context_free(struct dungeon_context *ctx);
bool dungeon_load(struct dungeon_context *ctx, int num);
void dungeon_set_camera(int surface, float x, float y, float z, float angle, float angle_p);
void dungeon_render(struct dungeon_context *ctx);
void dungeon_set_event_floor(int surface, int x, int y, int z, int event);
void dungeon_set_event_blend_rate(int surface, int x, int y, int z, int rate);
void dungeon_set_texture_floor(int surface, int x, int y, int z, int texture);
void dungeon_set_texture_ceiling(int surface, int x, int y, int z, int texture);
void dungeon_set_texture_north(int surface, int x, int y, int z, int texture);
void dungeon_set_texture_south(int surface, int x, int y, int z, int texture);
void dungeon_set_texture_east(int surface, int x, int y, int z, int texture);
void dungeon_set_texture_west(int surface, int x, int y, int z, int texture);
void dungeon_set_door_north(int surface, int x, int y, int z, int texture);
void dungeon_set_door_south(int surface, int x, int y, int z, int texture);
void dungeon_set_door_east(int surface, int x, int y, int z, int texture);
void dungeon_set_door_west(int surface, int x, int y, int z, int texture);

struct dungeon_context *dungeon_get_context(int surface);
void dungeon_update(void);

#endif /* SYSTEM4_DUNGEON_H */
