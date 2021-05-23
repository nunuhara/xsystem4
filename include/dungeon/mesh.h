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

#ifndef SYSTEM4_DUNGEON_MESH_H
#define SYSTEM4_DUNGEON_MESH_H

#include <GL/glew.h>
#include <cglm/types.h>

/*
 * A mesh represents a set of objects with the same shape and texture.
 */

struct mesh;

enum mesh_type {
	MESH_WALL,
	MESH_STAIRS,
	MESH_TYPE_MAX,
};

void mesh_init(void);
void mesh_fini(void);
struct mesh *mesh_create(enum mesh_type type, GLuint texture, bool has_alpha);
void mesh_free(struct mesh *m);
bool mesh_is_transparent(struct mesh *m);
void mesh_render(struct mesh *m, const mat4 local_transform, const mat4 view_transform, const mat4 proj_transform);
void mesh_add_floor(struct mesh *m, int x, int y, int z);
void mesh_add_ceiling(struct mesh *m, int x, int y, int z);
void mesh_add_north_wall(struct mesh *m, int x, int y, int z);
void mesh_add_south_wall(struct mesh *m, int x, int y, int z);
void mesh_add_east_wall(struct mesh *m, int x, int y, int z);
void mesh_add_west_wall(struct mesh *m, int x, int y, int z);
void mesh_add_stairs(struct mesh *m, int x, int y, int z, int orientation);
void mesh_remove_floor(struct mesh *m, int x, int y, int z);
void mesh_remove_ceiling(struct mesh *m, int x, int y, int z);
void mesh_remove_north_wall(struct mesh *m, int x, int y, int z);
void mesh_remove_south_wall(struct mesh *m, int x, int y, int z);
void mesh_remove_east_wall(struct mesh *m, int x, int y, int z);
void mesh_remove_west_wall(struct mesh *m, int x, int y, int z);

#endif /* SYSTEM4_DUNGEON_MESH_H */
