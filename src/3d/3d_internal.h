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
	vec3 aabb[2];  // axis-aligned bounding box
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
	GLuint light_map;
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

// Attribute variable locations
enum RE_attribute_location {
	VATTR_POS = 0,
	VATTR_NORMAL,
	VATTR_UV,
	VATTR_BONE_INDEX,
	VATTR_BONE_WEIGHT,
	VATTR_LIGHT_UV,
	VATTR_TANGENT,
};

struct shadow_renderer {
	GLuint program;
	GLuint fbo;
	GLuint texture;

	// Uniform variable locations
	GLint world_transform;
	GLint view_transform;
	GLint has_bones;
	GLint bone_matrices;
};

struct RE_renderer {
	GLuint program;
	GLuint depth_buffer;
	struct shadow_renderer shadow;

	// Uniform variable locations
	GLint world_transform;
	GLint view_transform;
	GLint texture;
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
	GLint camera_pos;
	GLint use_light_map;
	GLint light_texture;
	GLint use_normal_map;
	GLint normal_texture;
	GLint use_shadow_map;
	GLint shadow_transform;
	GLint shadow_texture;
	GLint shadow_bias;
	GLint fog_type;
	GLint fog_near;
	GLint fog_far;
	GLint fog_color;
	GLint ls_params;
	GLint ls_light_dir;
	GLint ls_light_color;
	GLint ls_sun_color;

	GLuint billboard_vao;
	GLuint billboard_attr_buffer;
	struct hash_table *billboard_textures;  // cg_no -> struct billboard_texture*
};

struct billboard_texture {
	GLuint texture;
};

// reign.c

void RE_instance_update_local_transform(struct RE_instance *inst);

// model.c

struct model *model_load(struct archive *aar, const char *path);
void model_free(struct model *model);

struct motion *motion_load(const char *name, struct RE_instance *instance, struct archive *aar);
void motion_free(struct motion *motion);

struct archive_data *RE_get_aar_entry(struct archive *aar, const char *dir, const char *name, const char *ext);

// renderer.c

struct RE_renderer *RE_renderer_new(struct texture *texture);
void RE_renderer_free(struct RE_renderer *r);
bool RE_renderer_load_billboard_texture(struct RE_renderer *r, int cg_no);
struct height_detector *RE_renderer_create_height_detector(struct RE_renderer *r, struct model *model);
void RE_renderer_free_height_detector(struct height_detector *hd);
float RE_renderer_detect_height(struct height_detector *hd, float x, float z);

// particle.c

enum particle_type {
	PARTICLE_TYPE_BILLBOARD = 0,
	PARTICLE_TYPE_POLYGON_OBJECT = 1,
	PARTICLE_TYPE_SWORD_BLUR = 2,
	PARTICLE_TYPE_CAMERA_VIBRATION = 3,
};

enum particle_move_type {
	PARTICLE_MOVE_FIXED = 0,
	PARTICLE_MOVE_LINEAR = 1,
	PARTICLE_MOVE_EMITTER = 2,
};

enum particle_blend_type {
	PARTICLE_BLEND_NORMAL = 0,
	PARTICLE_BLEND_ADDITIVE = 1,
};

enum particle_frame_ref_type {
	PARTICLE_FRAME_REF_EFFECT = 0,
	PARTICLE_FRAME_REF_TARGET = 1,
};

struct particle_position_unit {
	enum {
		PARTICLE_POS_UNIT_NONE = 0,
		PARTICLE_POS_UNIT_TARGET = 1,
		PARTICLE_POS_UNIT_BONE = 2,
		PARTICLE_POS_UNIT_RAND = 4,
		PARTICLE_POS_UNIT_RAND_POSITIVE_Y = 5,
		PARTICLE_POS_UNIT_ABSOLUTE = 6,
	} type;
	union {
		struct {
			int n;
			vec3 pos;
		} target;
		struct {
			int n;
			// char *name;
			vec3 pos;
		} bone;
		struct {
			float f;
		} rand;
		vec3 absolute;
	};
};

#define NR_PARTICLE_POSITIONS 2
#define NR_PARTICLE_POSITION_UNITS 3
struct particle_position {
	struct particle_position_unit units[NR_PARTICLE_POSITION_UNITS];
};

struct particle_object {
	char *name;
	enum particle_type type;
	enum particle_move_type move_type;
	int up_vector_type;
	float move_curve;
	struct particle_position *position[NR_PARTICLE_POSITIONS];
	float size[2];
	int nr_sizes2;
	float *sizes2;
	int nr_sizes_x;
	float *sizes_x;
	int nr_sizes_y;
	float *sizes_y;
	int nr_size_types;
	int *size_types;
	int nr_size_x_types;
	int *size_x_types;
	int nr_size_y_types;
	int *size_y_types;
	int nr_textures;
	char **textures;
	enum particle_blend_type blend_type;
	float texture_anime_frame;
	int texture_anime_time;
	char *polygon_name;
	int begin_frame;
	int end_frame;
	int stop_frame;
	enum particle_frame_ref_type frame_ref_type;
	int frame_ref_param;
	int nr_particles;
	float alpha_fadein_frame;
	int alpha_fadein_time;
	float alpha_fadeout_frame;
	int alpha_fadeout_time;
	vec3 rotation[2];
	vec3 revolution_angle[2];
	vec3 revolution_distance[2];
	vec3 curve_length;
	int child_frame;
	float child_length;
	float child_begin_slope;
	float child_end_slope;
	float child_create_begin_frame;
	float child_create_end_frame;
	int child_move_dir_type;
	int dir_type;
	int nr_damages;
	int *damages;
	float offset_x;
	float offset_y;
};

struct particle_effect {
	char *path;
	int nr_objects;
	struct particle_object *objects;
};

struct particle_effect *particle_effect_load(struct archive *aar, const char *path);
void particle_effect_free(struct particle_effect *effect);

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
	LIGHT_MAP = 7,
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
	uint32_t nr_light_uvs;
	vec2 *light_uvs;
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
	uint32_t light_uv_index[3];
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
void pol_compute_aabb(struct pol *model, vec3 dest[2]);
struct pol_bone *pol_find_bone(struct pol *pol, uint32_t id);

struct mot *mot_parse(uint8_t *data, size_t size);
void mot_free(struct mot *mot);

struct amt *amt_parse(uint8_t *data, size_t size);
void amt_free(struct amt *amt);
struct amt_material *amt_find_material(struct amt *amt, const char *name);

#endif /* SYSTEM4_3D_3D_INTERNAL_H */
