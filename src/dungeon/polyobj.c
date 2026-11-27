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
#include "system4/cg.h"
#include "system4/file.h"

#include "dungeon/polyobj.h"

struct polyobj *polyobj_load(const char *path)
{
	size_t size;
	uint8_t *data = file_read(path, &size);
	if (!data)
		return NULL;
	struct buffer r;
	buffer_init(&r, data, size);
	if (size < 16 || memcmp(buffer_strdata(&r), "POL\0\0\0\0\0", 8)) {
		free(data);
		return NULL;
	}
	buffer_skip(&r, 8);

	struct polyobj *po = xcalloc(1, sizeof(struct polyobj));
	po->data = data;
	po->size = size;
	po->nr_textures = buffer_read_int32(&r);
	if (po->nr_textures < 0 || (size_t)po->nr_textures > buffer_remaining(&r) / 8) {
		polyobj_free(po);
		return NULL;
	}
	po->textures = xcalloc(po->nr_textures, sizeof(struct polyobj_location));
	for (int i = 0; i < po->nr_textures; i++) {
		po->textures[i].offset = buffer_read_int32(&r);
		po->textures[i].length = buffer_read_int32(&r);
	}
	if (buffer_remaining(&r) < 4) {
		polyobj_free(po);
		return NULL;
	}
	po->nr_objects = buffer_read_int32(&r);
	if (po->nr_objects < 0 || (size_t)po->nr_objects > buffer_remaining(&r) / 8) {
		polyobj_free(po);
		return NULL;
	}
	po->objects = xcalloc(po->nr_objects, sizeof(struct polyobj_location));
	for (int i = 0; i < po->nr_objects; i++) {
		po->objects[i].offset = buffer_read_int32(&r);
		po->objects[i].length = buffer_read_int32(&r);
	}
	return po;
}

void polyobj_free(struct polyobj *po)
{
	free(po->textures);
	free(po->objects);
	free(po->data);
	free(po);
}

struct cg *polyobj_get_texture(struct polyobj *po, int index)
{
	if (!po || index < 0 || index >= po->nr_textures)
		return NULL;
	struct polyobj_location *loc = &po->textures[index];
	if (loc->offset > po->size || loc->length > po->size - loc->offset)
		return NULL;
	return cg_load_buffer(po->data + loc->offset, loc->length);
}

struct polyobj_object *polyobj_get_object(struct polyobj *po, int index)
{
	if (!po || index < 0 || index >= po->nr_objects)
		return NULL;
	struct polyobj_location *loc = &po->objects[index];
	if (loc->offset > po->size || loc->length > po->size - loc->offset)
		return NULL;
	struct buffer r;
	buffer_init(&r, po->data + loc->offset, loc->length);

	const char *name_start = buffer_strdata(&r);
	const char *name_end = memchr(name_start, '\0', buffer_remaining(&r));
	if (!name_end)
		return NULL;
	size_t name_len = (size_t)(name_end - name_start);
	struct polyobj_object *obj = xcalloc(1, sizeof(struct polyobj_object));
	obj->name = xmalloc(name_len + 1);
	memcpy(obj->name, name_start, name_len);
	obj->name[name_len] = '\0';
	buffer_skip(&r, name_len + 1);
	if (buffer_remaining(&r) < 8 || memcmp(buffer_strdata(&r), "POO\0\0\0\0\0", 8)) {
		polyobj_object_free(obj);
		return NULL;
	}
	buffer_skip(&r, 8);
	obj->nr_materials = buffer_read_int32(&r);
	obj->materials = xcalloc(obj->nr_materials, sizeof(int));
	for (int i = 0; i < obj->nr_materials; i++)
		obj->materials[i] = buffer_read_int32(&r);
	obj->nr_parts = buffer_read_int32(&r);
	obj->parts = xcalloc(obj->nr_parts, sizeof(struct polyobj_part));
	for (int i = 0; i < obj->nr_parts; i++) {
		struct polyobj_part *part = &obj->parts[i];
		part->nr_vertices = buffer_read_int32(&r);
		part->vertices = xcalloc(part->nr_vertices, sizeof(vec3));
		for (int j = 0; j < part->nr_vertices; j++) {
			part->vertices[j][0] = buffer_read_float(&r);
			part->vertices[j][1] = buffer_read_float(&r);
			part->vertices[j][2] = buffer_read_float(&r);
		}

		part->nr_triangles = buffer_read_int32(&r);
		part->triangles = xcalloc(part->nr_triangles, sizeof(struct polyobj_triangle));
		for (int j = 0; j < part->nr_triangles; j++) {
			struct polyobj_triangle *triangle = &part->triangles[j];
			triangle->material = buffer_read_int32(&r);
			triangle->index[0] = buffer_read_int32(&r);
			triangle->index[1] = buffer_read_int32(&r);
			triangle->index[2] = buffer_read_int32(&r);
			triangle->uv[0][0] = buffer_read_float(&r);
			triangle->uv[0][1] = buffer_read_float(&r);
			triangle->uv[1][0] = buffer_read_float(&r);
			triangle->uv[1][1] = buffer_read_float(&r);
			triangle->uv[2][0] = buffer_read_float(&r);
			triangle->uv[2][1] = buffer_read_float(&r);
		}
	}
	return obj;
}

void polyobj_object_free(struct polyobj_object *obj)
{
	for (int i = 0; i < obj->nr_parts; i++) {
		struct polyobj_part *part = &obj->parts[i];
		free(part->vertices);
		free(part->triangles);
	}
	free(obj->parts);
	free(obj->materials);
	free(obj->name);
	free(obj);
}
