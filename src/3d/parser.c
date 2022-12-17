/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/buffer.h"

#include "3d_internal.h"

#define METERS_PER_INCH 0.0254

static char *read_cstring(struct buffer *r)
{
	char *s = strdup(buffer_strdata(r));
	buffer_skip(r, strlen(s) + 1);
	return s;
}

static void read_position(struct buffer *r, vec3 v)
{
	// left->right handed system
	v[0] = buffer_read_float(r) * METERS_PER_INCH;
	v[1] = buffer_read_float(r) * METERS_PER_INCH;
	v[2] = -buffer_read_float(r) * METERS_PER_INCH;
}

static void read_direction(struct buffer *r, vec3 v)
{
	// left->right handed system
	v[0] = buffer_read_float(r);
	v[1] = buffer_read_float(r);
	v[2] = -buffer_read_float(r);
}

static void read_quaternion(struct buffer *r, versor q)
{
	// left->right handed system
	float w = buffer_read_float(r);
	float x = -buffer_read_float(r);
	float y = -buffer_read_float(r);
	float z = buffer_read_float(r);
	glm_quat_init(q, x, y, z, w);
	glm_quat_normalize(q);
}

static uint32_t parse_material_attributes(const char *name)
{
	uint32_t flags = 0;
	if (strstr(name, "(sprite)"))
		flags |= MATERIAL_SPRITE;
	return flags;
}

static void parse_material(struct buffer *r, struct pol_material *m)
{
	m->name = read_cstring(r);
	m->flags = parse_material_attributes(m->name);

	int nr_textures = buffer_read_int32(r);
	for (int i = 0; i < nr_textures; i++) {
		char *filename = read_cstring(r);
		int type = buffer_read_int32(r);
		if ((unsigned)type < MAX_TEXTURE_TYPE) {
			m->textures[type] = filename;
		} else {
			WARNING("invalid texture type %d", type);
			free(filename);
		}
	}
}

static void destroy_material(struct pol_material *m)
{
	free(m->name);
	for (int i = 0; i < MAX_TEXTURE_TYPE; i++)
		free(m->textures[i]);
}

static void parse_material_group(struct buffer *r, struct pol_material_group *mg)
{
	parse_material(r, &mg->m);

	mg->nr_children = buffer_read_int32(r);
	if (mg->nr_children > 0) {
		mg->children = xcalloc(mg->nr_children, sizeof(struct pol_material));
		for (uint32_t i = 0; i < mg->nr_children; i++) {
			parse_material(r, &mg->children[i]);
		}
	}
}

static void destroy_material_group(struct pol_material_group *mg)
{
	destroy_material(&mg->m);
	if (mg->children) {
		for (uint32_t i = 0; i < mg->nr_children; i++)
			destroy_material(&mg->children[i]);
		free(mg->children);
	}
}

static void parse_vertex(struct buffer *r, int pol_version, struct pol_vertex *v)
{
	read_position(r, v->pos);
	v->nr_weights = pol_version == 1 ? buffer_read_int32(r) : buffer_read_u16(r);
	if (v->nr_weights) {
		v->weights = xcalloc(v->nr_weights, sizeof(struct pol_bone_weight));
		for (uint32_t i = 0; i < v->nr_weights; i++) {
			v->weights[i].bone = pol_version == 1 ? buffer_read_int32(r) : buffer_read_u16(r);
			v->weights[i].weight = buffer_read_float(r);
		}
	}
}

static void destroy_vertex(struct pol_vertex *v)
{
	if (v->weights)
		free(v->weights);
}

static void parse_triangle(struct buffer *r, struct pol_mesh *mesh, int triangle_index, int unknowns_length)
{
	struct pol_triangle *t = &mesh->triangles[triangle_index];
	t->vert_index[0] = buffer_read_int32(r);
	t->vert_index[1] = buffer_read_int32(r);
	t->vert_index[2] = buffer_read_int32(r);
	t->uv_index[0] = buffer_read_int32(r);
	t->uv_index[1] = buffer_read_int32(r);
	t->uv_index[2] = buffer_read_int32(r);
	if (mesh->light_uvs) {
		t->light_uv_index[0] = buffer_read_int32(r) - mesh->nr_uvs;
		t->light_uv_index[1] = buffer_read_int32(r) - mesh->nr_uvs;
		t->light_uv_index[2] = buffer_read_int32(r) - mesh->nr_uvs;
	}

	buffer_skip(r, unknowns_length);

	read_direction(r, t->normals[0]);
	read_direction(r, t->normals[1]);
	read_direction(r, t->normals[2]);
	t->material = buffer_read_int32(r);
}

static uint32_t parse_mesh_attributes(const char *name)
{
	uint32_t flags = 0;
	if (strstr(name, "(nolighting)"))
		flags |= MESH_NOLIGHTING;
	if (strstr(name, "(nomakeshadow)"))
		flags |= MESH_NOMAKESHADOW;
	if (strstr(name, "(env)"))
		flags |= MESH_ENVMAP;
	if (strstr(name, "(both)"))
		flags |= MESH_BOTH;
	return flags;
}

static struct pol_mesh *parse_mesh(struct buffer *r, int pol_version)
{
	int type = buffer_read_int32(r);
	if (type != 0) {
		if (type != -1)
			WARNING("unknown mesh type: %d", type);
		return NULL;
	}
	struct pol_mesh *mesh = xcalloc(1, sizeof(struct pol_mesh));
	mesh->name = read_cstring(r);
	mesh->flags = parse_mesh_attributes(mesh->name);
	mesh->material = buffer_read_int32(r);

	mesh->nr_vertices = buffer_read_int32(r);
	mesh->vertices = xcalloc(mesh->nr_vertices, sizeof(struct pol_vertex));
	for (uint32_t i = 0; i < mesh->nr_vertices; i++) {
		parse_vertex(r, pol_version, &mesh->vertices[i]);
	}

	mesh->nr_uvs = buffer_read_int32(r);
	mesh->uvs = xcalloc(mesh->nr_uvs, sizeof(vec2));
	for (uint32_t i = 0; i < mesh->nr_uvs; i++) {
		mesh->uvs[i][0] = buffer_read_float(r);
		mesh->uvs[i][1] = buffer_read_float(r);
	}

	mesh->nr_light_uvs = buffer_read_int32(r);
	if (mesh->nr_light_uvs > 0) {
		mesh->light_uvs = xcalloc(mesh->nr_light_uvs, sizeof(vec2));
		for (uint32_t i = 0; i < mesh->nr_light_uvs; i++) {
			mesh->light_uvs[i][0] = buffer_read_float(r);
			mesh->light_uvs[i][1] = buffer_read_float(r);
		}
	}

	int triangle_unknowns_length = 12;
	if (pol_version == 1) {
		int nr_unknown_vecs = buffer_read_int32(r);
		buffer_skip(r, nr_unknown_vecs * 12);
	} else {
		int nr_unknown_ints = buffer_read_int32(r);
		buffer_skip(r, nr_unknown_ints * 4);
		int nr_unknown_bytes = buffer_read_int32(r);
		if (nr_unknown_bytes) {
			buffer_skip(r, nr_unknown_bytes);
			triangle_unknowns_length += 12;
		}
	}

	mesh->nr_triangles = buffer_read_int32(r);
	mesh->triangles = xcalloc(mesh->nr_triangles, sizeof(struct pol_triangle));
	for (uint32_t i = 0; i < mesh->nr_triangles; i++) {
		parse_triangle(r, mesh, i, triangle_unknowns_length);
	}

	if (pol_version == 1) {
		if (buffer_read_int32(r) != 1)
			WARNING("unexpected mesh footer");
		if (buffer_read_int32(r) != 0)
			WARNING("unexpected mesh footer");
	}

	return mesh;
}

static void free_mesh(struct pol_mesh *mesh)
{
	if (!mesh)
		return;
	free(mesh->name);
	for (uint32_t i = 0; i < mesh->nr_vertices; i++)
		destroy_vertex(&mesh->vertices[i]);
	free(mesh->vertices);
	free(mesh->uvs);
	free(mesh->light_uvs);
	free(mesh->triangles);
	free(mesh);
}

static void parse_bone(struct buffer *r, struct pol_bone *bone)
{
	bone->name = read_cstring(r);
	bone->id = buffer_read_int32(r);
	bone->parent = buffer_read_int32(r);
	read_position(r, bone->pos);
	read_quaternion(r, bone->rotq);
}

static void destroy_bone(struct pol_bone *bone)
{
	free(bone->name);
}

struct pol *pol_parse(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (memcmp(buffer_strdata(&r), "POL\0", 4))
		return NULL;
	buffer_skip(&r, 4);

	struct pol *pol = xcalloc(1, sizeof(struct pol));
	pol->version = buffer_read_int32(&r);
	if (pol->version != 1 && pol->version != 2) {
		WARNING("unknown POL version: %d", pol->version);
		free(pol);
		return NULL;
	}
	pol->nr_materials = buffer_read_int32(&r);
	pol->materials = xcalloc(pol->nr_materials, sizeof(struct pol_material_group));
	for (uint32_t i = 0; i < pol->nr_materials; i++) {
		parse_material_group(&r, &pol->materials[i]);
	}

	pol->nr_meshes = buffer_read_int32(&r);
	pol->meshes = xcalloc(pol->nr_meshes, sizeof(struct pol_mesh *));
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		pol->meshes[i] = parse_mesh(&r, pol->version);
	}

	pol->nr_bones = buffer_read_int32(&r);
	if (pol->nr_bones) {
		pol->bones = xcalloc(pol->nr_bones, sizeof(struct pol_bone));
		for (uint32_t i = 0; i < pol->nr_bones; i++) {
			parse_bone(&r, &pol->bones[i]);
		}
	}

	if (buffer_remaining(&r) != 0) {
		WARNING("extra data at end");
	}
	return pol;
}

void pol_free(struct pol *pol)
{
	for (uint32_t i = 0; i < pol->nr_materials; i++)
		destroy_material_group(&pol->materials[i]);
	free(pol->materials);
	for (uint32_t i = 0; i < pol->nr_meshes; i++)
		free_mesh(pol->meshes[i]);
	free(pol->meshes);
	for (uint32_t i = 0; i < pol->nr_bones; i++)
		destroy_bone(&pol->bones[i]);
	free(pol->bones);
	free(pol);
}

void pol_compute_aabb(struct pol *pol, vec3 dest[2])
{
	vec3 aabb[2];
	glm_aabb_invalidate(aabb);
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		struct pol_mesh *mesh = pol->meshes[i];
		if (!mesh)
			continue;
		for (uint32_t j = 0; j < mesh->nr_vertices; j++) {
			glm_vec3_minv(mesh->vertices[j].pos, aabb[0], aabb[0]);
			glm_vec3_maxv(mesh->vertices[j].pos, aabb[1], aabb[1]);
		}
	}
	glm_vec3_copy(aabb[0], dest[0]);
	glm_vec3_copy(aabb[1], dest[1]);
}

struct pol_bone *pol_find_bone(struct pol *pol, uint32_t id)
{
	for (uint32_t i = 0; i < pol->nr_bones; i++) {
		if (pol->bones[i].id == id)
			return &pol->bones[i];
	}
	return NULL;
}

struct mot *mot_parse(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (memcmp(buffer_strdata(&r), "MOT\0", 4))
		return NULL;
	buffer_skip(&r, 4);
	uint32_t version = buffer_read_int32(&r);
	if (version != 0) {
		WARNING("unknown MOT version: %d", version);
		return NULL;
	}
	uint32_t nr_frames = buffer_read_int32(&r);
	uint32_t nr_bones = buffer_read_int32(&r);

	struct mot *mot = xcalloc(1, sizeof(struct mot) + nr_bones * sizeof(struct mot_bone *));
	mot->nr_frames = nr_frames;
	mot->nr_bones = nr_bones;
	for (uint32_t i = 0; i < nr_bones; i++) {
		struct mot_bone *m = xcalloc(1, sizeof(struct mot_bone) + sizeof(struct mot_frame) * nr_frames);
		m->name = read_cstring(&r);
		m->id = buffer_read_int32(&r);
		m->parent = buffer_read_int32(&r);
		for (uint32_t j = 0; j < nr_frames; j++) {
			read_position(&r, m->frames[j].pos);
			read_quaternion(&r, m->frames[j].rotq);
			buffer_skip(&r, 16);  // another quaternion?
		}
		mot->motions[i] = m;
	}
	if (buffer_remaining(&r) != 0) {
		WARNING("extra data at end");
	}
	return mot;
}

void mot_free(struct mot *mot)
{
	for (uint32_t i = 0; i < mot->nr_bones; i++) {
		free(mot->motions[i]->name);
		free(mot->motions[i]);
	}
	free(mot);
}

struct amt *amt_parse(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (memcmp(buffer_strdata(&r), "AMT\0", 4))
		return NULL;
	buffer_skip(&r, 4);

	uint32_t version = buffer_read_int32(&r);
	int nr_fields;
	switch (version) {
	case 1: nr_fields = 3; break;
	case 2: nr_fields = 5; break;
	case 3: nr_fields = 6; break;
	case 4: nr_fields = 7; break;
	case 5: nr_fields = 11; break;
	case 6: nr_fields = 15; break;
	default:
		WARNING("unknown MOT version: %d", version);
		return NULL;
	}
	int nr_materials = buffer_read_int32(&r);

	struct amt *amt = xmalloc(sizeof(struct amt) + sizeof(struct amt_material *) * nr_materials);
	amt->version = version;
	amt->nr_materials = nr_materials;
	for (int i = 0; i < nr_materials; i++) {
		struct amt_material *m = amt->materials[i] =
			xmalloc(sizeof(struct amt_material) + sizeof(float) * nr_fields);
		m->name = read_cstring(&r);
		for (int j = 0; j < nr_fields; j++)
			m->fields[j] = buffer_read_float(&r);
	}

	if (buffer_remaining(&r) != 0) {
		WARNING("extra data at end");
	}
	return amt;
}

void amt_free(struct amt *amt)
{
	for (int i = 0; i < amt->nr_materials; i++) {
		free(amt->materials[i]->name);
		free(amt->materials[i]);
	}
	free(amt);
}

struct amt_material *amt_find_material(struct amt *amt, const char *name)
{
	for (int i = 0; i < amt->nr_materials; i++) {
		if (!strcmp(amt->materials[i]->name, name))
			return amt->materials[i];
	}
	return NULL;
}
