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

#ifndef SYSTEM4_POLYOBJ_H
#define SYSTEM4_POLYOBJ_H

#include <stddef.h>
#include <stdint.h>
#include <cglm/types.h>

struct polyobj_location {
	uint32_t offset;
	uint32_t length;
};

struct polyobj {
	uint8_t *data;
	size_t size;
	int nr_textures;
	struct polyobj_location *textures;
	int nr_objects;
	struct polyobj_location *objects;
};

struct polyobj_triangle {
	int material;
	int index[3];
	vec2 uv[3];
};

struct polyobj_part {
	int nr_vertices;
	vec3 *vertices;
	int nr_triangles;
	struct polyobj_triangle *triangles;
};

struct polyobj_object {
	char *name;
	int nr_materials;
	int *materials;
	int nr_parts;
	struct polyobj_part *parts;
};

struct polyobj *polyobj_load(const char *path);
void polyobj_free(struct polyobj *po);
struct cg *polyobj_get_texture(struct polyobj *po, int index);
struct polyobj_object *polyobj_get_object(struct polyobj *po, int index);
void polyobj_object_free(struct polyobj_object *obj);

#endif /* SYSTEM4_POLYOBJ_H */
