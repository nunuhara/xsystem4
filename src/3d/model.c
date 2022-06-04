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
#include "system4/aar.h"
#include "system4/cg.h"

#include "3d_internal.h"
#include "reign.h"

struct vertex {
	GLfloat pos[3];
	GLfloat normal[3];
	GLfloat uv[2];
};

static bool init_material(struct material *material, const struct pol_material *m, struct archive *aar, const char *path)
{
	if (!m->textures[COLOR_MAP]) {
		WARNING("No color texture");
		return false;
	}
	char *texture_path = xmalloc(strlen(path) + strlen(m->textures[COLOR_MAP]) + 2);
	sprintf(texture_path, "%s\\%s", path, m->textures[COLOR_MAP]);
	struct archive_data *dfile = archive_get_by_name(aar, texture_path);
	if (!dfile) {
		WARNING("cannot load texture %s", texture_path);
		free(texture_path);
		return false;
	}
	struct cg *cg = cg_load_data(dfile);
	archive_free_data(dfile);
	if (!cg) {
		WARNING("cg_load_data failed: %s", texture_path);
		free(texture_path);
		return false;
	}
	free(texture_path);

	glGenTextures(1, &material->color_map);
	glBindTexture(GL_TEXTURE_2D, material->color_map);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	material->opaque = !cg->metrics.has_alpha;

	cg_free(cg);
	return true;
}

static void destroy_material(struct material *material)
{
	if (material->color_map)
		glDeleteTextures(1, &material->color_map);
}

static void add_mesh(struct model *model, struct pol_mesh *m, int material_index, int material, struct RE_renderer *r)
{
	struct vertex *buf = xmalloc(m->nr_triangles * 3 * sizeof(struct vertex));
	int v = 0;
	for (uint32_t i = 0; i < m->nr_triangles; i++) {
		struct pol_triangle *t = &m->triangles[i];
		if (material_index >= 0 && t->material != (uint32_t)material_index)
			continue;
		for (int j = 0; j < 3; j++) {
			glm_vec3_copy(m->vertices[t->vert_index[j]].pos, buf[v].pos);
			glm_vec3_copy(t->normals[j], buf[v].normal);
			glm_vec2_copy(m->uvs[t->uv_index[j]], buf[v].uv);
			v++;
		}
	}
	if (v == 0) {
		free(buf);
		return;
	}
	model->meshes = xrealloc_array(model->meshes, model->nr_meshes, model->nr_meshes + 1, sizeof(struct mesh));
	struct mesh *mesh = &model->meshes[model->nr_meshes++];
	mesh->material = material;
	mesh->nr_vertices = v;

	glGenVertexArrays(1, &mesh->vao);
	glBindVertexArray(mesh->vao);
	glGenBuffers(1, &mesh->attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, mesh->attr_buffer);

	glEnableVertexAttribArray(r->shader.vertex);
	glVertexAttribPointer(r->shader.vertex, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)0);
	glEnableVertexAttribArray(r->vertex_normal);
	glVertexAttribPointer(r->vertex_normal, 3, GL_FLOAT, GL_TRUE, sizeof(struct vertex), (const void *)offsetof(struct vertex, normal));
	glEnableVertexAttribArray(r->vertex_uv);
	glVertexAttribPointer(r->vertex_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)offsetof(struct vertex, uv));
	glBufferData(GL_ARRAY_BUFFER, mesh->nr_vertices * sizeof(struct vertex), buf, GL_STATIC_DRAW);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	free(buf);
}

static void destroy_mesh(struct mesh *mesh)
{
	glDeleteVertexArrays(1, &mesh->vao);
	glDeleteBuffers(1, &mesh->attr_buffer);
}

struct model *model_load(struct archive *aar, const char *path, struct RE_renderer *r)
{
	if (path[0] == '\0')
		return NULL;

	const char *basename = strrchr(path, '\\');
	basename = basename ? basename + 1 : path;
	char *polname = xmalloc(strlen(path) + strlen(basename) + 6);
	sprintf(polname, "%s\\%s.POL", path, basename);

	struct archive_data *dfile = archive_get_by_name(aar, polname);
	if (!dfile) {
		WARNING("%s: not found", polname);
		free(polname);
		return NULL;
	}

	struct pol *pol = pol_parse(dfile->data, dfile->size);
	archive_free_data(dfile);
	if (!pol) {
		WARNING("%s: parse error", polname);
		free(polname);
		return NULL;
	}

	struct model *model = xcalloc(1, sizeof(struct model));
	model->name = polname;

	// Materials
	int *material_offsets = xmalloc(pol->nr_materials * sizeof(int));
	for (uint32_t i = 0; i < pol->nr_materials; i++) {
		material_offsets[i] = model->nr_materials;
		if (pol->materials[i].nr_children)
			model->nr_materials += pol->materials[i].nr_children;
		else
			model->nr_materials++;
	}
	model->materials = xcalloc(model->nr_materials, sizeof(struct material));
	for (uint32_t i = 0; i < pol->nr_materials; i++) {
		if (pol->materials[i].nr_children == 0) {
			init_material(&model->materials[material_offsets[i]],
				      &pol->materials[i].m, aar, path);
			continue;
		}
		for (uint32_t j = 0; j < pol->materials[i].nr_children; j++) {
			init_material(&model->materials[material_offsets[i] + j],
				      &pol->materials[i].children[j], aar, path);
		}
	}

	// Meshes
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		if (!pol->meshes[i])
			continue;
		struct pol_material_group *mg = &pol->materials[pol->meshes[i]->material];
		int m_off = material_offsets[pol->meshes[i]->material];
		if (mg->nr_children == 0) {
			add_mesh(model, pol->meshes[i], -1, m_off, r);
			continue;
		}
		for (uint32_t j = 0; j < mg->nr_children; j++) {
			add_mesh(model, pol->meshes[i], j, m_off + j, r);
		}
	}

	free(material_offsets);
	pol_free(pol);
	return model;
}

void model_free(struct model *model)
{
	for (int i = 0; i < model->nr_meshes; i++)
		destroy_mesh(&model->meshes[i]);
	free(model->meshes);

	for (int i = 0; i < model->nr_materials; i++)
		destroy_material(&model->materials[i]);
	free(model->materials);

	free(model->name);
	free(model);
}
