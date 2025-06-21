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
#include "reign.h"

#define NR_DIR_LIGHTS 3
#define MAX_BONES 308  // Maximum in Rance Quest

typedef struct vec3_range {
	vec3 begin;
	vec3 end;
} vec3_range;

struct model {
	char *path;
	int nr_meshes;
	struct mesh *meshes;
	int nr_materials;
	struct material *materials;
	int nr_bones;
	struct bone *bones;
	struct hash_table *bone_map;  // bone id in POL/MOT -> struct bone *
	struct hash_table *bone_name_map;  // bone name -> (struct bone * | NULL)
	struct hash_table *mot_cache;  // name -> struct mot *
	struct collider *collider;
	vec3 aabb[2];  // axis-aligned bounding box
	bool has_transparent_mesh;
};

struct mesh {
	char *name;
	uint32_t flags;
	bool is_transparent;
	bool hidden;
	GLuint vao;
	GLuint attr_buffer;
	GLuint index_buffer;
	int nr_vertices;
	int nr_indices;
	int material;
	vec3 outline_color;
	float outline_thickness;
	vec2 uv_scroll;
};

struct material {
	uint32_t flags;
	bool is_transparent;
	int nr_color_maps;
	GLuint *color_maps;
	GLuint specular_map;
	GLuint alpha_map;
	GLuint light_map;
	GLuint normal_map;
	float specular_strength;
	float specular_shininess;
	float shadow_darkness;
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
	int state;
	float current_frame;
	float frame_begin, frame_end;
	float loop_frame_begin, loop_frame_end;
	int texture_index;
};

// Attribute variable locations
enum RE_attribute_location {
	VATTR_POS = 0,
	VATTR_NORMAL,
	VATTR_UV,
	VATTR_BONE_INDEX,
	VATTR_BONE_WEIGHT,
	VATTR_LIGHT_UV,
	VATTR_COLOR,
	VATTR_TANGENT,
};

struct shadow_renderer {
	GLuint program;
	GLuint fbo;
	GLuint texture;

	// Uniform variable locations
	GLint local_transform;
	GLint view_transform;
	GLint has_bones;
};

struct outline_renderer {
	GLuint program;

	// Uniform variable locations
	GLint local_transform;
	GLint view_transform;
	GLint proj_transform;
	GLint normal_transform;
	GLint has_bones;
	GLint outline_color;
	GLint outline_thickness;
};

struct RE_renderer {
	int viewport_width;
	int viewport_height;
	GLuint program;
	GLuint depth_buffer;
	struct shadow_renderer shadow;
	struct outline_renderer outline;

	// Uniform variable locations
	GLint view_transform;
	GLint texture;
	GLint local_transform;
	GLint proj_transform;
	GLint normal_transform;
	GLint alpha_mod;
	GLint has_bones;
	GLint global_ambient;
	GLint instance_ambient;
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
	GLint diffuse_type;
	GLint light_texture;
	GLint use_normal_map;
	GLint normal_texture;
	GLint shadow_darkness;
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
	GLint alpha_mode;
	GLint alpha_texture;
	GLint uv_scroll;

	GLuint billboard_vao;
	GLuint billboard_attr_buffer;
	struct hash_table *billboard_textures;  // cg_no -> struct billboard_texture*

	uint32_t last_frame_timestamp;
};

struct billboard_texture {
	GLuint texture;
	bool has_alpha;
};

// reign.c

extern enum RE_plugin_version re_plugin_version;

void RE_instance_update_local_transform(struct RE_instance *inst);

// model.c

struct model *model_load(struct archive *aar, const char *path);
void model_free(struct model *model);
struct model *model_create_sphere(int r, int g, int b, int a);

struct motion *motion_load(const char *name, struct RE_instance *instance, struct archive *aar);
void motion_free(struct motion *motion);

struct archive_data *RE_get_aar_entry(struct archive *aar, const char *dir, const char *name, const char *ext);

// renderer.c

struct RE_renderer *RE_renderer_new(void);
void RE_renderer_free(struct RE_renderer *r);
void RE_renderer_set_viewport_size(struct RE_renderer *r, int width, int height);
bool RE_renderer_load_billboard_texture(struct RE_renderer *r, int cg_no);
struct height_detector *RE_renderer_create_height_detector(struct RE_renderer *r, struct model *model);
void RE_renderer_free_height_detector(struct height_detector *hd);
bool RE_renderer_detect_height(struct height_detector *hd, float x, float z, float *y_out);
void RE_calc_view_matrix(struct RE_camera *camera, vec3 up, mat4 out);

// particle.c

enum particle_type {
	PARTICLE_TYPE_BILLBOARD = 0,
	PARTICLE_TYPE_POLYGON_OBJECT = 1,
	PARTICLE_TYPE_SWORD_BLUR = 2,
	PARTICLE_TYPE_CAMERA_QUAKE = 3,
};

enum particle_move_type {
	PARTICLE_MOVE_FIXED = 0,
	PARTICLE_MOVE_LINEAR = 1,
	PARTICLE_MOVE_EMITTER = 2,
};

enum particle_up_vector_type {
	PARTICLE_UP_VECTOR_Y_AXIS = 0,
	PARTICLE_UP_VECTOR_OBJECT_MOVE = 1,
	PARTICLE_UP_VECTOR_INSTANCE_MOVE = 2,
	PARTICLE_UP_VECTOR_TYPE_MAX = PARTICLE_UP_VECTOR_INSTANCE_MOVE
};

enum particle_blend_type {
	PARTICLE_BLEND_NORMAL = 0,
	PARTICLE_BLEND_ADDITIVE = 1,
};

enum particle_frame_ref_type {
	PARTICLE_FRAME_REF_EFFECT = 0,
	PARTICLE_FRAME_REF_TARGET = 1,
};

enum particle_child_move_dir_type {
	PARTICLE_CHILD_MOVE_DIR_NORMAL = 0,
	PARTICLE_CHILD_MOVE_DIR_REVERSE = 1,
	PARTICLE_CHILD_MOVE_DIR_TYPE_MAX = PARTICLE_CHILD_MOVE_DIR_REVERSE
};

enum particle_dir_type {
	PARTICLE_DIR_TYPE_NORMAL = 0,
	PARTICLE_DIR_TYPE_XY_PLANE = 1,
	PARTICLE_DIR_TYPE_MAX = PARTICLE_DIR_TYPE_XY_PLANE
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
			int index;
			vec3 offset;
		} target;
		struct {
			int target_index;
			char *name;
			vec3 offset;
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

struct pae {
	char *path;
	struct hash_table *textures; // name -> struct billboard_texture*
	int nr_objects;
	struct pae_object *objects;
};

struct pae_object {
	char *name;
	enum particle_type type;
	enum particle_move_type move_type;
	enum particle_up_vector_type up_vector_type;
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
	vec3_range rotation;
	vec3_range revolution_angle;
	vec3_range revolution_distance;
	vec3 curve_length;
	int child_frame;
	float child_length;
	float child_begin_slope;
	float child_end_slope;
	float child_create_begin_frame;
	float child_create_end_frame;
	enum particle_child_move_dir_type child_move_dir_type;
	enum particle_dir_type dir_type;
	int nr_damages;
	int *damages;
	float offset_x;
	float offset_y;

	struct model *model;
};

struct particle_effect {
	struct pae *pae;
	struct particle_object *objects;  // pae->nr_objects elements
	bool camera_quake_enabled;
};

struct particle_object {
	struct pae_object *pae_obj;
	struct particle_instance *instances;  // pae_obj->nr_particles elements
	vec3_range pos;
	vec3 move_vec, move_ortho;
};

struct particle_instance {
	float begin_frame;
	float end_frame;
	vec3_range random_offset;
	float roll_angle;
	float pitch_angle;
};

struct pae *pae_load(struct archive *aar, const char *path);
void pae_free(struct pae *pae);
void pae_calc_frame_range(struct pae *pae, struct motion *motion);
float pae_object_calc_alpha(struct pae_object *po, struct particle_instance *pi, float frame);

struct particle_effect *particle_effect_create(struct pae *pae);
void particle_effect_free(struct particle_effect *effect);
void particle_effect_update(struct RE_instance *inst);
void particle_object_calc_local_transform(struct RE_instance *inst, struct particle_object *po, struct particle_instance *pi, float frame, mat4 dest);

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
	ALPHA_MAP = 6,
	LIGHT_MAP = 7,
	NORMAL_MAP = 8,
	MAX_TEXTURE_TYPE
};

enum material_flags {
	MATERIAL_SPRITE = 1 << 0,
	MATERIAL_ALPHA  = 1 << 1,
};

struct pol_material {
	char *name;
	uint32_t flags;
	char *textures[MAX_TEXTURE_TYPE];
};

struct pol_material_group {
	struct pol_material m;
	uint32_t nr_children;
	struct pol_material *children;
};

enum mesh_flags {
	MESH_NOLIGHTING          = 1 << 0,
	MESH_NOMAKESHADOW        = 1 << 1,
	MESH_ENVMAP              = 1 << 2,
	MESH_BOTH                = 1 << 3,
	MESH_SPRITE              = 1 << 4,
	MESH_BLEND_ADDITIVE      = 1 << 5,
	MESH_NO_EDGE             = 1 << 6,
	MESH_NO_HEIGHT_DETECTION = 1 << 7,
	MESH_ALPHA               = 1 << 8,
	MESH_HAS_LIGHT_UV        = 1 << 9,
};

struct pol_mesh {
	char *name;
	uint32_t flags;
	uint32_t material;
	uint32_t nr_vertices;
	struct pol_vertex *vertices;
	uint32_t nr_uvs;
	vec2 *uvs;
	uint32_t nr_light_uvs;
	vec2 *light_uvs;
	uint32_t nr_colors;
	vec3 *colors;
	uint32_t nr_alphas;
	float *alphas;
	uint32_t nr_triangles;
	struct pol_triangle *triangles;
	// Parameters in .opr file.
	Color edge_color;
	float edge_size;
	vec2 uv_scroll;
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
	uint32_t color_index[3];
	uint32_t alpha_index[3];
	vec3 normals[3];
	uint32_t material_group_index;
};

struct pol_bone {
	char *name;
	uint32_t id;
	int32_t parent;
	vec3 pos;
	versor rotq;
};

struct mot {
	char *name;
	uint32_t nr_frames;
	uint32_t nr_bones;
	uint32_t nr_texture_indices;
	uint32_t *texture_indices;  // texture index for each frame
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
	// amt v4+
	AMT_SHADOW_DARKNESS = 6,
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

struct mot *mot_parse(uint8_t *data, size_t size, const char *name);
void mot_free(struct mot *mot);

struct amt *amt_parse(uint8_t *data, size_t size);
void amt_free(struct amt *amt);
struct amt_material *amt_find_material(struct amt *amt, const char *name);

void opr_load(uint8_t *data, size_t size, struct pol *pol);
void txa_load(uint8_t *data, size_t size, struct mot *mot);

// collision.c

struct collider_triangle;
struct collider_edge;

struct collider {
	struct collider_triangle *triangles;
	uint32_t nr_triangles;
	struct collider_edge *edges;  // boundary edges
	uint32_t nr_edges;
	vec3 *path_points;
	uint32_t nr_path_points;
};

struct collider *collider_create(struct pol_mesh *mesh);
void collider_free(struct collider *collider);
bool collider_height(struct collider *collider, vec2 xz, float *h_out);
bool check_collision(struct collider *collider, vec2 p0, vec2 p1, float radius, vec2 out);
bool collider_raycast(struct collider *collider, vec3 origin, vec3 direction, vec3 out);
bool collider_find_path(struct collider *collider, vec3 start, vec3 goal, mat4 vp_transform);
bool collider_optimize_path(struct collider *collider);

#endif /* SYSTEM4_3D_3D_INTERNAL_H */
