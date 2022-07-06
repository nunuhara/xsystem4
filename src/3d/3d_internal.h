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

#ifndef SYSTEM4_3D_3D_INTERNAL_H
#define SYSTEM4_3D_3D_INTERNAL_H

#include <cglm/types.h>

#include "system4/archive.h"

#include "gfx/gl.h"
#include "gfx/gfx.h"

#define NR_DIR_LIGHTS 3
#define MAX_BONES 211  // Maximum in TT3

struct model {
	char *path;
	int nr_meshes;
	struct mesh *meshes;
	int nr_materials;
	struct material *materials;
	int nr_bones;
	struct bone *bones;
	struct hash_table *bone_map;  // bone id in POL/MOT -> struct bone *
};

struct mesh {
	GLuint vao;
	GLuint attr_buffer;
	int nr_vertices;
	int material;
};

struct material {
	GLuint color_map;
	GLuint specular_map;
	GLuint normal_map;
	float specular_strength;
	float specular_shininess;
	float rim_exponent;
	vec3 rim_color;
};

struct bone {
	char *name;
	int index;   // index on model->bones
	int parent;  // index on model->bones, or -1
	mat4 inverse_bind_matrix;
};

struct motion {
	struct RE_instance *instance;
	struct mot *mot;
	char *name;
	int state;
	float current_frame;
	float frame_begin, frame_end;
	float loop_frame_begin, loop_frame_end;
};

struct RE_renderer {
	struct shader shader;
	GLuint depth_buffer;

	// Uniform variable locations
	GLint local_transform;
	GLint proj_transform;
	GLint normal_transform;
	GLint alpha_mod;
	GLint has_bones;
	GLint bone_matrices;
	GLint ambient;
	struct {
		GLint dir;
		GLint diffuse;
		GLint globe_diffuse;
		GLint ambient;
	} dir_lights[NR_DIR_LIGHTS];
	GLint specular_light_dir;
	GLint specular_strength;
	GLint specular_shininess;
	GLint use_specular_map;
	GLint specular_texture;
	GLint rim_exponent;
	GLint rim_color;
	GLint view_pos;
	GLint use_normal_map;
	GLint normal_texture;

	// Attribute variable locations
	GLint vertex_normal;
	GLint vertex_uv;
	GLint vertex_tangent;
	GLint vertex_bone_index;
	GLint vertex_bone_weight;

	mat4 bone_transforms[MAX_BONES];
	GLuint billboard_vao;
	GLuint billboard_attr_buffer;
	struct hash_table *billboard_textures;  // cg_no -> struct billboard_texture*
	uint32_t last_frame_timestamp;
};

struct billboard_texture {
	GLuint texture;
};

// model.c

struct model *model_load(struct archive *aar, const char *path, struct RE_renderer *renderer);
void model_free(struct model *model);

struct motion *motion_load(const char *name, struct RE_instance *instance, struct archive *aar);
void motion_free(struct motion *motion);

// renderer.c

struct RE_renderer *RE_renderer_new(struct texture *texture);
void RE_renderer_free(struct RE_renderer *r);
bool RE_renderer_load_billboard_texture(struct RE_renderer *r, int cg_no);

// parser.c

struct pol {
	int32_t version;
	uint32_t nr_materials;
	struct pol_material_group *materials;
	uint32_t nr_meshes;
	struct pol_mesh **meshes;
	uint32_t nr_bones;
	struct pol_bone *bones;
};

enum pol_texture_type {
	COLOR_MAP = 1,
	SPECULAR_MAP = 4,
	NORMAL_MAP = 8,
	MAX_TEXTURE_TYPE
};

struct pol_material {
	char *name;
	char *textures[MAX_TEXTURE_TYPE];
};

struct pol_material_group {
	struct pol_material m;
	uint32_t nr_children;
	struct pol_material *children;
};

struct pol_mesh {
	char *name;
	uint32_t material;
	uint32_t nr_vertices;
	struct pol_vertex *vertices;
	uint32_t nr_uvs;
	vec2 *uvs;
	uint32_t nr_triangles;
	struct pol_triangle *triangles;
};

struct pol_vertex {
	vec3 pos;
	uint32_t nr_weights;
	struct pol_bone_weight *weights;
};

struct pol_bone_weight {
	uint32_t bone;
	float weight;
};

struct pol_triangle {
	uint32_t vert_index[3];
	uint32_t uv_index[3];
	vec3 normals[3];
	uint32_t material;  // index in the material group
};

struct pol_bone {
	char *name;
	uint32_t id;
	int32_t parent;
	vec3 pos;
	versor rotq;
};

struct mot {
	uint32_t nr_frames;
	uint32_t nr_bones;
	struct mot_bone *motions[];
};

struct mot_frame {
	vec3 pos;
	versor rotq;
};

struct mot_bone {
	char *name;
	uint32_t id;
	uint32_t parent;
	struct mot_frame frames[];
};

struct amt {
	int version;
	int nr_materials;
	struct amt_material *materials[];
};

struct amt_material {
	char *name;
	float fields[];
};

enum amt_field_index {
	AMT_SPECULAR_STRENGTH = 0,
	AMT_SPECULAR_SHININESS = 2,
	// amt v5+
	AMT_RIM_EXPONENT = 7,
	AMT_RIM_R = 8,
	AMT_RIM_G = 9,
	AMT_RIM_B = 10,
};

struct pol *pol_parse(uint8_t *data, size_t size);
void pol_free(struct pol *pol);
struct pol_bone *pol_find_bone(struct pol *pol, uint32_t id);

struct mot *mot_parse(uint8_t *data, size_t size);
void mot_free(struct mot *mot);

struct amt *amt_parse(uint8_t *data, size_t size);
void amt_free(struct amt *amt);
struct amt_material *amt_find_material(struct amt *amt, const char *name);

#endif /* SYSTEM4_3D_3D_INTERNAL_H */
