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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/cg.h"
#include "system4/hashtable.h"

#include "3d_internal.h"
#include "reign.h"

#define FP16_MIN 6.103516e-5f
#define NR_WEIGHTS 4
#define DEFAULT_OUTLINE_THICKNESS 0.003

struct vertex_common {
	GLfloat pos[3];
	GLfloat normal[3];
	GLfloat uv[2];
};

struct vertex_light_uv {
	GLfloat uv[2];
};

struct vertex_color {
	GLfloat color[4];
};

struct vertex_tangent {
	GLfloat tangent[4];
};

struct vertex_bones {
	GLint bone_id[NR_WEIGHTS];
	GLfloat bone_weight[NR_WEIGHTS];
};

static bool is_transparent_mesh(const struct pol_mesh *mesh)
{
	if (re_plugin_version == RE_REIGN_PLUGIN)
		return !(mesh->flags & MESH_SPRITE);
	else
		return mesh->flags & MESH_ALPHA;
}

static bool is_transparent_material(const struct pol_material *material)
{
	if (re_plugin_version == RE_REIGN_PLUGIN)
		return !(material->flags & MATERIAL_SPRITE);
	else
		return material->flags & MATERIAL_ALPHA;
}

struct archive_data *RE_get_aar_entry(struct archive *aar, const char *dir, const char *name, const char *ext)
{
	char *path = xmalloc(strlen(dir) + strlen(name) + strlen(ext) + 2);
	sprintf(path, "%s\\%s%s", dir, name, ext);
	struct archive_data *dfile = archive_get_by_name(aar, path);
	free(path);
	return dfile;
}

static GLuint load_texture(struct archive *aar, const char *path, const char *name, bool *has_alpha_out)
{
	struct archive_data *dfile = RE_get_aar_entry(aar, path, name, "");
	if (!dfile) {
		return 0;
	}
	struct cg *cg = cg_load_data(dfile);
	if (!cg) {
		WARNING("cg_load_data failed: %s", dfile->name);
		archive_free_data(dfile);
		return 0;
	}
	archive_free_data(dfile);

	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (has_alpha_out)
		*has_alpha_out = cg->metrics.has_alpha;
	cg_free(cg);
	return texture;
}

static GLuint *load_texture_list(struct archive *aar, const char *path, const char *name, int *nr_textures_out, bool *has_alpha_out)
{
	// Load the base texture (e.g. face.png)
	GLuint tex = load_texture(aar, path, name, has_alpha_out);
	if (!tex)
		return NULL;
	GLuint *textures = xmalloc(10 * sizeof(GLuint));
	int nr_textures = 0;
	textures[nr_textures++] = tex;

	// Try to load additional textures (e.g. face[1].png, face[2].png, etc.)
	const char *ext = strrchr(name, '.');
	if (ext) {
		for (;;) {
			char next_name[100];
			snprintf(next_name, sizeof(next_name), "%.*s[%d]%s", (int)(ext - name), name, nr_textures, ext);
			tex = load_texture(aar, path, next_name, NULL);
			if (!tex)
				break;
			textures[nr_textures++] = tex;
			if (nr_textures % 10 == 0) {
				textures = xrealloc(textures, (nr_textures + 10) * sizeof(GLuint));
			}
		}
	}
	*nr_textures_out = nr_textures;
	return textures;
}

static bool init_material(struct material *material, const struct pol_material *m, struct amt *amt, struct archive *aar, const char *path)
{
	material->flags = m->flags;
	if (!m->textures[COLOR_MAP]) {
		WARNING("No color texture");
		return false;
	}
	bool has_alpha;
	material->color_maps = load_texture_list(aar, path, m->textures[COLOR_MAP], &material->nr_color_maps, &has_alpha);
	if (!material->color_maps)
		return false;

	if (m->textures[SPECULAR_MAP])
		material->specular_map = load_texture(aar, path, m->textures[SPECULAR_MAP], NULL);
	if (m->textures[ALPHA_MAP]) {
		if (has_alpha && !strcmp(m->textures[COLOR_MAP], m->textures[ALPHA_MAP])) {
			// Do nothing; the alpha channel of the color map is used.
		} else {
			material->alpha_map = load_texture(aar, path, m->textures[ALPHA_MAP], NULL);
		}
	}
	if (m->textures[LIGHT_MAP])
		material->light_map = load_texture(aar, path, m->textures[LIGHT_MAP], NULL);
	if (m->textures[NORMAL_MAP])
		material->normal_map = load_texture(aar, path, m->textures[NORMAL_MAP], NULL);

	material->is_transparent = (has_alpha || material->alpha_map) && is_transparent_material(m);
	material->shadow_darkness = 1.0f;

	struct amt_material *amt_m = amt ? amt_find_material(amt, m->name) : NULL;
	if (amt_m) {
		material->specular_strength = amt_m->fields[AMT_SPECULAR_STRENGTH];
		material->specular_shininess = amt_m->fields[AMT_SPECULAR_SHININESS];
		if (amt->version >= 4)
			material->shadow_darkness = amt_m->fields[AMT_SHADOW_DARKNESS];
		if (amt->version >= 5) {
			material->rim_exponent = amt_m->fields[AMT_RIM_EXPONENT];
			material->rim_color[0] = amt_m->fields[AMT_RIM_R];
			material->rim_color[1] = amt_m->fields[AMT_RIM_G];
			material->rim_color[2] = amt_m->fields[AMT_RIM_B];
			// Very small rim_exponent value should not be used for rim
			// lighting. (e.g. meizi.amt in TT3)
			if (material->rim_exponent < FP16_MIN)
				material->rim_exponent = 0.0f;
		}
	}

	return true;
}

static void destroy_material(struct material *material)
{
	if (material->color_maps) {
		glDeleteTextures(material->nr_color_maps, material->color_maps);
		free(material->color_maps);
	}
	if (material->specular_map)
		glDeleteTextures(1, &material->specular_map);
	if (material->alpha_map)
		glDeleteTextures(1, &material->alpha_map);
	if (material->light_map)
		glDeleteTextures(1, &material->light_map);
	if (material->normal_map)
		glDeleteTextures(1, &material->normal_map);
}

static int cmp_by_bone_weight(const void *lhs, const void *rhs)
{
	float l = ((struct pol_bone_weight *)lhs)->weight;
	float r = ((struct pol_bone_weight *)rhs)->weight;
	return (l < r) - (l > r);  // descending order.
}

static void sort_and_normalize_bone_weights(struct pol_vertex *v)
{
	qsort(v->weights, v->nr_weights, sizeof(struct pol_bone_weight), cmp_by_bone_weight);
	float total = 0.0;
	for (uint32_t i = 0; i < v->nr_weights && i < NR_WEIGHTS; i++) {
		total += v->weights[i].weight;
	}
	for (uint32_t i = 0; i < v->nr_weights && i < NR_WEIGHTS; i++) {
		v->weights[i].weight /= total;
	}
}

static void calc_tangent(struct pol_mesh *m, struct pol_triangle *t, vec4 tangent[3])
{
	vec3 v1, v2;
	glm_vec3_sub(m->vertices[t->vert_index[1]].pos, m->vertices[t->vert_index[0]].pos, v1);
	glm_vec3_sub(m->vertices[t->vert_index[2]].pos, m->vertices[t->vert_index[0]].pos, v2);

	vec2 w1, w2;
	glm_vec2_sub(m->uvs[t->uv_index[1]], m->uvs[t->uv_index[0]], w1);
	glm_vec2_sub(m->uvs[t->uv_index[2]], m->uvs[t->uv_index[0]], w2);

	float r = 1.0 / (w1[0] * w2[1] - w2[0] * w1[1]);
	if (!isfinite(r))  // degenerate uv triangle
		r = 1.0;  // ??

	vec3 sdir, tdir;
	glm_vec3_scale(v1, w2[1], sdir);
	glm_vec3_muladds(v2, -w1[1], sdir);
	glm_vec3_scale(sdir, r, sdir);

	glm_vec3_scale(v2, w1[0], tdir);
	glm_vec3_muladds(v1, -w2[0], tdir);
	glm_vec3_scale(tdir, r, tdir);

	for (int i = 0; i < 3; i++) {
		vec3 s;
		glm_vec3_copy(sdir, s);

		// Gram-Schmidt orthogonalize.
		glm_vec3_muladds(t->normals[i], -glm_vec3_dot(t->normals[i], s), s);
		glm_vec3_normalize(s);

		// Calculate handedness.
		vec3 c;
		glm_vec3_cross(t->normals[i], sdir, c);
		float w = (glm_vec3_dot(c, tdir) < 0.0) ? -1.0 : 1.0;

		glm_vec4(s, w, tangent[i]);
	}
}

static void *buf_alloc(uint8_t **ptr, int size)
{
	void *p = *ptr;
	*ptr += size;
	return p;
}

static void add_mesh(struct model *model, struct pol_mesh *m, uint32_t material_group_index, int material)
{
	bool has_light_map = m->light_uvs && model->materials[material].light_map;
	bool has_vertex_colors = m->nr_colors > 0 || m->nr_alphas > 0;
	bool has_normal_map = model->materials[material].normal_map != 0;
	bool has_bones = !!model->bone_map;

	GLsizei stride = sizeof(struct vertex_common);
	if (has_light_map)
		stride += sizeof(struct vertex_light_uv);
	if (has_vertex_colors)
		stride += sizeof(struct vertex_color);
	if (has_normal_map)
		stride += sizeof(struct vertex_tangent);
	if (has_bones)
		stride += sizeof(struct vertex_bones);

	void *buffer = xmalloc(m->nr_triangles * 3 * stride);
	uint8_t *ptr = buffer;

	int nr_vertices = 0;
	for (uint32_t i = 0; i < m->nr_triangles; i++) {
		struct pol_triangle *t = &m->triangles[i];
		if (t->material_group_index != material_group_index)
			continue;
		vec4 tangent[3];
		if (has_normal_map)
			calc_tangent(m, t, tangent);
		for (int j = 0; j < 3; j++) {
			struct pol_vertex *vert = &m->vertices[t->vert_index[j]];
			struct vertex_common *v_common = buf_alloc(&ptr, sizeof(struct vertex_common));
			glm_vec3_copy(vert->pos, v_common->pos);
			glm_vec3_copy(t->normals[j], v_common->normal);
			glm_vec2_copy(m->uvs[t->uv_index[j]], v_common->uv);
			if (has_light_map) {
				struct vertex_light_uv *v_light_uv = buf_alloc(&ptr, sizeof(struct vertex_light_uv));
				glm_vec2_copy(m->light_uvs[t->light_uv_index[j]], v_light_uv->uv);
			}
			if (has_vertex_colors) {
				struct vertex_color *v_color = buf_alloc(&ptr, sizeof(struct vertex_color));
				if (m->nr_colors > 0)
					glm_vec3_copy(m->colors[t->color_index[j]], v_color->color);
				else
					glm_vec3_one(v_color->color);
				v_color->color[3] = m->nr_alphas > 0 ? m->alphas[t->alpha_index[j]] : 1.f;
			}
			if (has_normal_map) {
				struct vertex_tangent *v_tangent = buf_alloc(&ptr, sizeof(struct vertex_tangent));
				glm_vec4_ucopy(tangent[j], v_tangent->tangent);
			}
			if (has_bones) {
				struct vertex_bones *v_bones = buf_alloc(&ptr, sizeof(struct vertex_bones));
				sort_and_normalize_bone_weights(vert);
				for (uint32_t k = 0; k < NR_WEIGHTS; k++) {
					if (k < vert->nr_weights) {
						struct bone *bone = ht_get_int(model->bone_map, vert->weights[k].bone, NULL);
						if (!bone)
							WARNING("%s: invalid bone id in vertex data", model->path);
						v_bones->bone_id[k] = bone ? bone->index : -1;
						v_bones->bone_weight[k] = vert->weights[k].weight;
					} else {
						v_bones->bone_id[k] = -1;
						v_bones->bone_weight[k] = 0.0;
					}
				}
			}
			nr_vertices++;
		}
	}
	assert(ptr == (uint8_t *)buffer + nr_vertices * stride);

	if (nr_vertices == 0) {
		free(buffer);
		return;
	}
	model->meshes = xrealloc_array(model->meshes, model->nr_meshes, model->nr_meshes + 1, sizeof(struct mesh));
	struct mesh *mesh = &model->meshes[model->nr_meshes++];
	mesh->name = xstrdup(m->name);
	mesh->flags = m->flags;
	mesh->material = material;
	if (re_plugin_version == RE_REIGN_PLUGIN)
		mesh->is_transparent = model->materials[material].is_transparent && is_transparent_mesh(m);
	else
		mesh->is_transparent = model->materials[material].is_transparent || is_transparent_mesh(m);
	if (mesh->is_transparent)
		model->has_transparent_mesh = true;
	mesh->outline_color[0] = m->edge_color.r / 255.f;
	mesh->outline_color[1] = m->edge_color.g / 255.f;
	mesh->outline_color[2] = m->edge_color.b / 255.f;
	mesh->outline_thickness = m->edge_size ? m->edge_size : DEFAULT_OUTLINE_THICKNESS;
	glm_vec2_copy(m->uv_scroll, mesh->uv_scroll);
	mesh->nr_vertices = nr_vertices;

	glGenVertexArrays(1, &mesh->vao);
	glBindVertexArray(mesh->vao);
	glGenBuffers(1, &mesh->attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, mesh->attr_buffer);

	const uint8_t *base = (const uint8_t *)0;
	glEnableVertexAttribArray(VATTR_POS);
	glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, stride, base + offsetof(struct vertex_common, pos));
	glEnableVertexAttribArray(VATTR_NORMAL);
	glVertexAttribPointer(VATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, stride, base + offsetof(struct vertex_common, normal));
	glEnableVertexAttribArray(VATTR_UV);
	glVertexAttribPointer(VATTR_UV, 2, GL_FLOAT, GL_FALSE, stride, base + offsetof(struct vertex_common, uv));
	base += sizeof(struct vertex_common);
	if (has_light_map) {
		glEnableVertexAttribArray(VATTR_LIGHT_UV);
		glVertexAttribPointer(VATTR_LIGHT_UV, 2, GL_FLOAT, GL_FALSE, stride, base + offsetof(struct vertex_light_uv, uv));
		base += sizeof(struct vertex_light_uv);
	} else {
		glDisableVertexAttribArray(VATTR_LIGHT_UV);
		glVertexAttrib2f(VATTR_LIGHT_UV, 0.0, 0.0);
	}
	if (has_vertex_colors) {
		glEnableVertexAttribArray(VATTR_COLOR);
		glVertexAttribPointer(VATTR_COLOR, 4, GL_FLOAT, GL_FALSE, stride, base + offsetof(struct vertex_color, color));
		base += sizeof(struct vertex_color);
	} else {
		glDisableVertexAttribArray(VATTR_COLOR);
		glVertexAttrib4f(VATTR_COLOR, 1.0, 1.0, 1.0, 1.0);
	}
	if (has_normal_map) {
		glEnableVertexAttribArray(VATTR_TANGENT);
		glVertexAttribPointer(VATTR_TANGENT, 4, GL_FLOAT, GL_FALSE, stride, base + offsetof(struct vertex_tangent, tangent));
		base += sizeof(struct vertex_tangent);
	} else {
		glDisableVertexAttribArray(VATTR_TANGENT);
		glVertexAttrib3f(VATTR_TANGENT, 1.0, 0.0, 0.0);
	}
	if (has_bones) {
		glEnableVertexAttribArray(VATTR_BONE_INDEX);
		glVertexAttribIPointer(VATTR_BONE_INDEX, NR_WEIGHTS, GL_INT, stride, base + offsetof(struct vertex_bones, bone_id));
		glEnableVertexAttribArray(VATTR_BONE_WEIGHT);
		glVertexAttribPointer(VATTR_BONE_WEIGHT, NR_WEIGHTS, GL_FLOAT, GL_FALSE, stride, base + offsetof(struct vertex_bones, bone_weight));
		base += sizeof(struct vertex_bones);
	} else {
		glDisableVertexAttribArray(VATTR_BONE_INDEX);
		glVertexAttribI4i(VATTR_BONE_INDEX, 0, 0, 0, 0);
		glDisableVertexAttribArray(VATTR_BONE_WEIGHT);
		glVertexAttrib4f(VATTR_BONE_WEIGHT, 0.0, 0.0, 0.0, 0.0);
	}
	assert((intptr_t)base == stride);

	glBufferData(GL_ARRAY_BUFFER, mesh->nr_vertices * stride, buffer, GL_STATIC_DRAW);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	free(buffer);
}

static void destroy_mesh(struct mesh *mesh)
{
	free(mesh->name);
	glDeleteVertexArrays(1, &mesh->vao);
	glDeleteBuffers(1, &mesh->attr_buffer);
	if (mesh->index_buffer)
		glDeleteBuffers(1, &mesh->index_buffer);
}

static struct bone *add_bone(struct model *model, struct pol *pol, struct pol_bone *pol_bone)
{
	struct bone *bone = ht_get_int(model->bone_map, pol_bone->id, NULL);
	if (bone)
		return bone;  // already added.

	struct bone *parent = NULL;
	if (pol_bone->parent >= 0) {
		// Parent bone must appear before its children in model->bones.
		struct pol_bone *pol_parent = pol_find_bone(pol, pol_bone->parent);
		if (!pol_parent)
			ERROR("Parent bone of \"%s\" is not found", pol_bone->name);
		parent = add_bone(model, pol, pol_parent);
	}

	bone = &model->bones[model->nr_bones];
	ht_put_int(model->bone_map, pol_bone->id, bone);
	bone->name = strdup(pol_bone->name);
	bone->index = model->nr_bones;
	bone->parent = parent ? parent->index : -1;

	glm_quat_mat4(pol_bone->rotq, bone->inverse_bind_matrix);
	glm_translate(bone->inverse_bind_matrix, pol_bone->pos);

	// Update bone_name_map. If the bone name is not unique in the POL, set the
	// map value to NULL so that ID matching will be used.
	struct ht_slot *slot = ht_put(model->bone_name_map, pol_bone->name, bone);
	if (slot->value != bone) {
		NOTICE("%s: non-unique bone %s", model->path, pol_bone->name);
		slot->value = NULL;
	}

	model->nr_bones++;
	return bone;
}

static void destroy_bone(struct bone *bone)
{
	free(bone->name);
}

struct model *model_load(struct archive *aar, const char *path)
{
	const char *basename = strrchr(path, '\\');
	basename = basename ? basename + 1 : path;

	// Load .POL file
	struct archive_data *pol_file = RE_get_aar_entry(aar, path, basename, ".POL");
	if (!pol_file) {
		WARNING("%s\\%s.POL: not found", path, basename);
		return NULL;
	}
	struct pol *pol = pol_parse(pol_file->data, pol_file->size);
	if (!pol) {
		WARNING("%s: parse error", pol_file->name);
		archive_free_data(pol_file);
		return NULL;
	}
	archive_free_data(pol_file);

	// Load .amt file, if any
	struct amt *amt = NULL;
	struct archive_data *amt_file = RE_get_aar_entry(aar, path, basename, ".amt");
	if (amt_file) {
		amt = amt_parse(amt_file->data, amt_file->size);
		if (!amt)
			WARNING("%s: parse error", amt_file->name);
		archive_free_data(amt_file);
	}

	struct model *model = xcalloc(1, sizeof(struct model));
	model->path = strdup(path);

	// Load .opr file, if any
	struct archive_data *opr_file = RE_get_aar_entry(aar, path, basename, ".opr");
	if (opr_file) {
		opr_load(opr_file->data, opr_file->size, pol);
		archive_free_data(opr_file);
	}

	// Bones
	if (pol->nr_bones > 0) {
		if (pol->nr_bones > MAX_BONES)
			ERROR("%s: Too many bones (%u)", model->path, pol->nr_bones);
		model->bone_map = ht_create(pol->nr_bones * 3 / 2);
		model->bone_name_map = ht_create(pol->nr_bones * 3 / 2);
		model->mot_cache = ht_create(16);
		model->bones = xcalloc(pol->nr_bones, sizeof(struct bone));
		for (uint32_t i = 0; i < pol->nr_bones; i++) {
			add_bone(model, pol, &pol->bones[i]);
		}
		if (model->nr_bones != (int)pol->nr_bones)
			ERROR("%s: Broken bone data", model->path);
	}

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
				      &pol->materials[i].m, amt, aar, path);
			continue;
		}
		for (uint32_t j = 0; j < pol->materials[i].nr_children; j++) {
			init_material(&model->materials[material_offsets[i] + j],
				      &pol->materials[i].children[j], amt, aar, path);
		}
	}

	// Meshes
	for (uint32_t i = 0; i < pol->nr_meshes; i++) {
		if (!pol->meshes[i])
			continue;
		if (!strcmp(pol->meshes[i]->name, "collision")) {
			if (model->collider)
				WARNING("multiple collision meshes");
			else
				model->collider = collider_create(pol->meshes[i]);
			continue;
		}
		struct pol_material_group *mg = &pol->materials[pol->meshes[i]->material];
		int m_off = material_offsets[pol->meshes[i]->material];
		if (mg->nr_children == 0) {
			add_mesh(model, pol->meshes[i], 0, m_off);
			continue;
		}
		for (uint32_t j = 0; j < mg->nr_children; j++) {
			add_mesh(model, pol->meshes[i], j, m_off + j);
		}
	}

	pol_compute_aabb(pol, model->aabb);

	free(material_offsets);
	if (amt)
		amt_free(amt);
	pol_free(pol);
	return model;
}

void model_free(struct model *model)
{
	if (model->collider)
		collider_free(model->collider);

	for (int i = 0; i < model->nr_meshes; i++)
		destroy_mesh(&model->meshes[i]);
	free(model->meshes);

	for (int i = 0; i < model->nr_materials; i++)
		destroy_material(&model->materials[i]);
	free(model->materials);

	for (int i = 0; i < model->nr_bones; i++)
		destroy_bone(&model->bones[i]);
	free(model->bones);
	if (model->bone_map)
		ht_free_int(model->bone_map);
	if (model->bone_name_map)
		ht_free(model->bone_name_map);
	if (model->mot_cache) {
		ht_foreach_value(model->mot_cache, (void(*)(void*))mot_free);
		ht_free(model->mot_cache);
	}

	free(model->path);
	free(model);
}

static void init_sphere_mesh(struct mesh *mesh)
{
	const int w_segments = 16;
	const int h_segments = 16;
	const int nr_vertices = (w_segments + 1) * (h_segments + 1);
	struct vertex_common *vertices = xcalloc(nr_vertices, sizeof(struct vertex_common));
	struct vertex_common *v = vertices;
	for (int y = 0; y <= h_segments; y++) {
		float theta = GLM_PIf * y / h_segments;
		for (int x = 0; x <= w_segments; x++, v++) {
			float phi = 2.0f * GLM_PIf * x / w_segments;
			v->pos[0] = -cosf(phi) * sinf(theta);
			v->pos[1] = cosf(theta);
			v->pos[2] = sinf(phi) * sinf(theta);
			glm_vec3_copy(v->pos, v->normal);
			v->uv[0] = v->uv[1] = 0.0f;
		}
	}
	assert(v == vertices + nr_vertices);

	const int nr_indices = 3 * 2 * w_segments * (h_segments - 1);
	GLushort *indices = xcalloc(nr_indices, sizeof(GLushort));
	GLushort *pi = indices;
	for (int y = 0; y < h_segments; y++) {
		for (int x = 0; x < w_segments; x++) {
			GLushort a = y * w_segments + x + 1;
			GLushort b = y * w_segments + x;
			GLushort c = (y + 1) * w_segments + x;
			GLushort d = (y + 1) * w_segments + x + 1;
			if (y > 0) {
				*pi++ = a;
				*pi++ = b;
				*pi++ = d;
			}
			if (y < h_segments - 1) {
				*pi++ = b;
				*pi++ = c;
				*pi++ = d;
			}
		}
	}
	assert(pi == indices + nr_indices);

	mesh->flags = MESH_NOLIGHTING;
	mesh->nr_vertices = nr_vertices;
	mesh->nr_indices = nr_indices;
	glGenVertexArrays(1, &mesh->vao);
	glBindVertexArray(mesh->vao);
	glGenBuffers(1, &mesh->attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, mesh->attr_buffer);
	glBufferData(GL_ARRAY_BUFFER, nr_vertices * sizeof(struct vertex_common), vertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(VATTR_POS);
	glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex_common), (void*)offsetof(struct vertex_common, pos));
	glEnableVertexAttribArray(VATTR_NORMAL);
	glVertexAttribPointer(VATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex_common), (void*)offsetof(struct vertex_common, normal));
	glEnableVertexAttribArray(VATTR_UV);
	glVertexAttribPointer(VATTR_UV, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex_common), (void*)offsetof(struct vertex_common, uv));

	glGenBuffers(1, &mesh->index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, nr_indices * sizeof(GLushort), indices, GL_STATIC_DRAW);
	glBindVertexArray(0);

	free(vertices);
	free(indices);
}

struct model *model_create_sphere(int r, int g, int b, int a)
{
	struct model *model = xcalloc(1, sizeof(struct model));

	model->nr_meshes = 1;
	model->meshes = xcalloc(1, sizeof(struct mesh));
	init_sphere_mesh(&model->meshes[0]);
	model->aabb[0][0] = -1.0f;
	model->aabb[0][1] = -1.0f;
	model->aabb[0][2] = -1.0f;
	model->aabb[1][0] = 1.0f;
	model->aabb[1][1] = 1.0f;
	model->aabb[1][2] = 1.0f;

	model->nr_materials = 1;
	model->materials = xcalloc(1, sizeof(struct material));
	struct material *material = &model->materials[0];
	material->is_transparent = true;
	material->color_maps = xmalloc(sizeof(GLuint));
	material->nr_color_maps = 1;
	glGenTextures(1, material->color_maps);
	glBindTexture(GL_TEXTURE_2D, material->color_maps[0]);
	uint8_t pixel[4] = {r, g, b, a};
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	glBindTexture(GL_TEXTURE_2D, 0);
	model->has_transparent_mesh = true;

	return model;
}

static int cmp_motions_by_bone_id(const void *lhs, const void *rhs)
{
	return (*(struct mot_bone **)lhs)->id - (*(struct mot_bone **)rhs)->id;
}

static struct mot *mot_load(const char *name, struct model *model, struct archive *aar)
{
	struct archive_data *mot_file = RE_get_aar_entry(aar, model->path, name, ".MOT");
	if (!mot_file) {
		WARNING("%s\\%s.MOT: not found", model->path, name);
		return NULL;
	}
	struct mot *mot = mot_parse(mot_file->data, mot_file->size, name);
	if (!mot) {
		WARNING("%s: parse error", mot_file->name);
		archive_free_data(mot_file);
		return NULL;
	}
	archive_free_data(mot_file);

	if (model->nr_bones != (int)mot->nr_bones)
		ERROR("%s: wrong number of bones. Expected %d but got %d", name, model->nr_bones, mot->nr_bones);

	// Reorder mot->motions so that motion for model->bones[i] can be
	// accessed by mot->motions[i].
	for (uint32_t i = 0; i < mot->nr_bones; i++) {
		// Match by name first, since some MOT have wrong bone IDs (e.g. maidsan_ahoge_*).
		struct bone *bone = ht_get(model->bone_name_map, mot->motions[i]->name, NULL);
		// If it is not found or is NULL (non-unique bone name), match by bone ID.
		if (!bone)
			bone = ht_get_int(model->bone_map, mot->motions[i]->id, NULL);
		if (!bone)
			ERROR("%s: invalid bone \"%s\" (%d)", name, mot->motions[i]->name, mot->motions[i]->id);
		mot->motions[i]->id = bone->index;
	}
	qsort(mot->motions, mot->nr_bones, sizeof(struct mot_bone *), cmp_motions_by_bone_id);

	// Load optional .txa file.
	struct archive_data *txa_file = RE_get_aar_entry(aar, model->path, name, ".txa");
	if (txa_file) {
		txa_load(txa_file->data, txa_file->size, mot);
		archive_free_data(txa_file);
	}

	return mot;
}

struct motion *motion_load(const char *name, struct RE_instance *instance, struct archive *aar)
{
	struct model *model = instance->model;
	if (!model || !model->mot_cache)
		return NULL;
	struct mot *mot = ht_get(model->mot_cache, name, NULL);
	if (!mot) {
		mot = mot_load(name, model, aar);
		if (!mot)
			return NULL;
		ht_put(model->mot_cache, name, mot);
	}
	struct motion *motion = xcalloc(1, sizeof(struct motion));
	motion->instance = instance;
	motion->mot = mot;
	return motion;
}

void motion_free(struct motion *motion)
{
	free(motion);
}
