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
	struct bone *bones;  // ordered so that parents precede their children
	struct bone **bones_by_pol_index;  // POL bone array index -> struct bone *
	struct hash_table *bone_map;  // bone id in POL -> struct bone *
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
	vec3 specular_color;
	float specular_power;
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
	GLuint blend_texture;
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
	VATTR_BLEND_WEIGHT,
	VATTR_BLEND_UV,
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
	GLint diffuse_mod;
	struct {
		GLint dir;
		GLint diffuse;
		GLint globe_diffuse;
		GLint ambient;
	} dir_lights[NR_DIR_LIGHTS];
	GLint specular_light_dir;
	GLint specular_color;
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
	GLint blend_tex;
	GLint use_blend_texture;

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

// s3de.c

// .3de effect system. The 's' prefix is just to avoid an identifier starting
// with a digit; it stands for SealEngine.

enum s3de_object_type {
	S3DE_OBJ_BILLBOARD,
	S3DE_OBJ_POLYGON,
	S3DE_OBJ_CAMERA,
};

enum s3de_move_type {
	S3DE_MOVE_LINEAR,
	S3DE_MOVE_BEZIER,
};

enum s3de_spawn_position_type {
	S3DE_SPAWN_NORMAL,
	S3DE_SPAWN_SPHERE,
	S3DE_SPAWN_HORIZONTAL,
};

enum s3de_blend_type {
	S3DE_BLEND_NORMAL,
	S3DE_BLEND_ADDITIVE,
};

enum s3de_emitter_link_type {
	S3DE_LINK_NONE              = 0,  // frozen at spawn-time world pos
	S3DE_LINK_FOLLOW            = 1,  // particle follows current emitter pos
	S3DE_LINK_ROTATE_AT_SPAWN   = 2,  // emitter rotation around spawn pos
	S3DE_LINK_ROTATE_AT_CURRENT = 3,  // emitter rotation around current pos
	S3DE_LINK_MAX = S3DE_LINK_ROTATE_AT_CURRENT
};

enum s3de_direction_type {
	S3DE_DIR_RANDOM,
	S3DE_DIR_SPECIFIED,
	S3DE_DIR_EMITTER,
	S3DE_DIR_EMITTER_REVERSE,
	S3DE_DIR_EMITTER_COORD,
	S3DE_DIR_ARBITRARY_PLANE,
	S3DE_DIR_SPAWN,
};

enum s3de_posture_type {
	S3DE_POSE_NONE,
	S3DE_POSE_CAMERA,
	S3DE_POSE_MOVE_DIR,
	S3DE_POSE_MOVE_AND_FLY,
	S3DE_POSE_FLY_AND_EMITTER_MOVE,
};

enum s3de_interp_type {
	S3DE_INTERP_NONE,
	S3DE_INTERP_LINEAR,
	S3DE_INTERP_SLERP,
};

struct s3de_range {
	float base;
	float random;
};

struct s3de_scalar_entry {
	int frame;
	float value;
	enum s3de_interp_type interp;
};

struct s3de_vec3_entry {
	int frame;
	vec3 v;
	enum s3de_interp_type interp;
};

struct s3de_position_entry {
	int frame;
	vec3 pos;
	enum s3de_interp_type interp;
	float spawn_radius;
	float spawn_angle_deg;
};

struct s3de_object {
	char *name;
	enum s3de_object_type type;
	enum s3de_move_type movement_type;
	enum s3de_spawn_position_type spawn_position_type;
	float spawn_distance;
	int particle_count;

	char *texture;

	enum s3de_blend_type blend_type;
	char *polygon_name;
	enum s3de_emitter_link_type emitter_link_type;
	bool soft_fog_edge;

	enum s3de_posture_type posture_type;
	enum s3de_direction_type direction_type;
	vec3 direction;
	float direction_angle;

	struct s3de_range start_size, end_size;
	struct s3de_range start_x_size, end_x_size;
	struct s3de_range start_y_size, end_y_size;
	struct s3de_range offset_x, offset_y, offset_z;
	struct s3de_range speed, acceleration;
	struct s3de_range movement_distance, movement_curve;

	bool free_fall;
	float mass, air_resistance;

	struct s3de_range start_x_rotation, end_x_rotation;
	struct s3de_range start_y_rotation, end_y_rotation;
	struct s3de_range start_z_rotation, end_z_rotation;

	float x_revolution_angle[2], y_revolution_angle[2], z_revolution_angle[2];
	float x_revolution_distance[2], y_revolution_distance[2], z_revolution_distance[2];

	int child_frame;
	float texture_anime_frame;

	struct s3de_range alpha_fade_in_frame, alpha_fade_out_frame;

	int *damages;
	int nr_damages;

	struct s3de_position_entry *position_list;
	int nr_position_entries;
	struct s3de_scalar_entry *size_list;
	int nr_size_entries;
	struct s3de_scalar_entry *x_size_list;
	int nr_x_size_entries;
	struct s3de_scalar_entry *y_size_list;
	int nr_y_size_entries;
	struct s3de_scalar_entry *alpha_list;
	int nr_alpha_entries;
	struct s3de_vec3_entry *rotation_list;
	int nr_rotation_entries;
	struct s3de_vec3_entry *multiply_color_list;
	int nr_multiply_color_entries;
	struct s3de_vec3_entry *additive_color_list;
	int nr_additive_color_entries;

	struct model *model;
};

struct s3de {
	char *path;
	int loop_start_frame;
	int loop_end_frame;
	struct s3de_object *objects;
	int nr_objects;
	struct hash_table *textures; // name -> struct billboard_texture*
};

struct s3de *s3de_load(struct archive *aar, const char *path);
void s3de_free(struct s3de *s);

struct s3de_particle {
	float begin_frame, end_frame;
	vec3 world_pos;       // spawn-time world position
	vec3 spawn_offset;    // offset from emitter pos
	vec3 direction;
	vec3 spawn_dir;       // emitter motion direction at spawn frame
	float speed, accel;
	float movement_distance, movement_curve;
	float start_size_scale, end_size_scale;
	float start_x_scale, end_x_scale;
	float start_y_scale, end_y_scale;
	float offset_x, offset_y;  // per-particle 2D quad pivot shift
	vec3 start_rotation_deg, end_rotation_deg;
	float fade_in_frames, fade_out_frames;
};

// Emitter-level values interpolated once per frame by s3de_effect_update,
// then read (and combined with per-particle values) during rendering.
struct s3de_object_state {
	vec3 emitter_pos;
	float emitter_size, emitter_x_size, emitter_y_size;
	float emitter_alpha;
	vec3 emitter_rotation_deg;
	vec3 multiply_color;
	vec3 additive_color;
	struct s3de_particle *particles;  // length = obj->particle_count
};

struct s3de_effect {
	struct s3de *s3de;
	struct s3de_object_state *objects;  // length = s3de->nr_objects
	int wav_channel;       // -1 if no sound
	bool sound_started;
	float last_frame;
};

struct s3de_effect *s3de_effect_create(struct s3de *s, struct archive *aar);
void s3de_effect_free(struct s3de_effect *eff);
void s3de_effect_update(struct RE_instance *inst);
void s3de_calc_frame_range(struct s3de *s, struct motion *motion);
bool s3de_particle_alpha(struct s3de_object_state *st, struct s3de_particle *p,
	float frame, float *alpha_out);
bool s3de_billboard_world_transform(struct RE_instance *inst,
	struct s3de_object *obj, struct s3de_object_state *st,
	struct s3de_particle *p, float frame, mat3 camera_rot,
	mat4 out);
bool s3de_mesh_world_transform(struct RE_instance *inst,
	struct s3de_object *obj, struct s3de_object_state *st,
	struct s3de_particle *p, float frame, mat3 camera_rot, vec3 camera_pos,
	mat4 out);

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
	struct pol_material_group *children;
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
	MESH_NO_ZWRITE           = 1 << 10,
	MESH_HEIGHT_DETECTION    = 1 << 11,
	MESH_HAS_SPECULAR_COLOR  = 1 << 12,
	MESH_HAS_SPECULAR_POWER  = 1 << 13,
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
	uint32_t nr_blend_uvs;
	vec2 *blend_uvs;
	uint32_t nr_blend_weights;
	float *blend_weights;
	uint32_t nr_triangles;
	struct pol_triangle *triangles;
	// Parameters in .opr file.
	Color edge_color;
	float edge_size;
	vec2 uv_scroll;
	vec3 specular_color;
	float specular_power;
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
	uint32_t blend_uv_index[3];
	uint32_t blend_weight_index[3];
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
	struct mpr *mpr;            // optional
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

// mpr.c

struct mpr_float_key { int frame; float v; };
struct mpr_vec3_key { int frame; vec3 v; };
struct mpr_int_key { int frame; int v; };

struct mpr_track_set {
	int nr_mul_alpha;
	struct mpr_float_key *mul_alpha;
	int nr_mul_diffuse;
	struct mpr_vec3_key *mul_diffuse;
	int nr_add_ambient;
	struct mpr_vec3_key *add_ambient;
	int nr_texture_anime;
	struct mpr_int_key *texture_anime;  // mesh-scope only
};

struct mpr {
	int nr_meshes;                       // == model->nr_meshes
	struct mpr_track_set **mesh_tracks;  // sparse: indexed by mesh index, NULL if absent
	struct mpr_track_set object;
	bool has_mesh_alpha;                 // any mesh_tracks[i]->nr_mul_alpha > 0
};

// Per-draw material modulation.
struct mpr_modulation {
	float alpha;
	vec3 ambient;
	vec3 diffuse;
};

struct mpr *mpr_load(uint8_t *data, size_t size, struct model *model);
void mpr_free(struct mpr *mpr);
void mpr_evaluate_object(const struct mpr *mpr, float frame,
		struct RE_instance *inst, struct mpr_modulation *out);
void mpr_evaluate_mesh(const struct mpr_track_set *mt, float frame,
		const struct mpr_modulation *obj, struct mpr_modulation *out);
void mpr_build_mat_tex_index(const struct model *model, const struct RE_instance *inst,
		const struct mpr *mpr, float frame, int *mat_tex_index);

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
struct collider *collider_create_raycast(struct pol_mesh **meshes, int nr_meshes);
void collider_free(struct collider *collider);
bool collider_height(struct collider *collider, vec2 xz, float *h_out);
bool check_collision(struct collider *collider, vec2 p0, vec2 p1, float radius, vec2 out);
bool collider_raycast(struct collider *collider, vec3 origin, vec3 direction, vec3 out);
bool collider_find_path(struct collider *collider, vec3 start, vec3 goal, mat4 vp_transform);
bool collider_optimize_path(struct collider *collider);

#endif /* SYSTEM4_3D_3D_INTERNAL_H */
