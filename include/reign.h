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

#ifndef SYSTEM4_REIGN_H
#define SYSTEM4_REIGN_H

#define RE_NR_BACK_CGS 16
#define RE_NR_INSTANCE_TARGETS 2

#include <cglm/types.h>

#include "gfx/gfx.h"
#include "plugin.h"

typedef struct cJSON cJSON;
struct hash_table;

enum RE_plugin_version {
	RE_REIGN_PLUGIN,  // Toushin Toshi 3
	RE_TAPIR_PLUGIN,  // Rance Quest
};

enum RE_instance_type {
	RE_ITYPE_UNINITIALIZED     = 0,
	RE_ITYPE_STATIC            = 1,
	RE_ITYPE_SKINNED           = 2,
	RE_ITYPE_BILLBOARD         = 3,
	RE_ITYPE_DIRECTIONAL_LIGHT = 4,
	RE_ITYPE_SPECULAR_LIGHT    = 6,
	RE_ITYPE_PARTICLE_EFFECT   = 8,
	RE_ITYPE_PATH_LINE         = 9,
};

enum RE_motion_state {
	RE_MOTION_STATE_STOP   = 0,
	RE_MOTION_STATE_NOLOOP = 1,
	RE_MOTION_STATE_LOOP   = 2,
};

enum RE_fog_type {
	RE_FOG_NONE             = 0,
	RE_FOG_LINEAR           = 1,
	RE_FOG_LIGHT_SCATTERING = 2,
};

enum RE_draw_type {
	RE_DRAW_TYPE_NORMAL   = 0,
	RE_DRAW_TYPE_ADDITIVE = 1,
	RE_DRAW_TYPE_MAX = RE_DRAW_TYPE_ADDITIVE
};

enum RE_draw_options {
	RE_DRAW_OPTION_EDGE = 0,
	RE_DRAW_OPTION_MAX
};

enum RE_draw_edge_mode {
	RE_DRAW_EDGE_NONE = 0,
	RE_DRAW_EDGE_CHARACTERS_ONLY = 1,
	RE_DRAW_EDGE_ALL = 2,
};

struct RE_options {
	int anti_aliasing;
	int wait_vsync;
};
extern struct RE_options RE_options;

struct RE_camera {
	vec3 pos;
	float pitch, roll, yaw;  // in degrees
	float quake_pitch, quake_yaw;
};

struct RE_back_cg {
	struct texture texture;
	struct string *name;
	int no;
	float blend_rate;
	float x;
	float y;
	float mag;
	bool show;
};

struct RE_plugin {
	struct draw_plugin plugin;
	int sprite;
	int nr_instances;
	struct RE_instance **instances;
	struct archive *aar;
	struct hash_table *model_cache;
	struct hash_table *pae_cache;
	struct RE_renderer *renderer;
	struct RE_camera camera;
	mat4 proj_transform;

	struct RE_back_cg back_cg[RE_NR_BACK_CGS];

	// Settings.
	int render_mode;
	int shadow_mode;
	vec3 shadow_map_light_dir;
	float shadow_bias;
	int bump_mode;
	int bloom_mode;
	int glare_mode;
	int fog_mode;
	enum RE_fog_type fog_type;
	float fog_near;
	float fog_far;
	vec3 fog_color;
	float ls_beta_r;
	float ls_beta_m;
	float ls_g;
	float ls_distance;
	vec3 ls_light_dir;
	vec3 ls_light_color;
	vec3 ls_sun_color;
	int specular_mode;
	int light_map_mode;
	int soft_fog_edge_mode;
	int ssao_mode;
	int vertex_blend_mode;
	int shader_precision_mode;
	int texture_filter_mode;
	bool use_power2_texture;
	int shadow_map_resolution_level;
	int texture_resolution_level;
	vec3 global_ambient;
	float post_effect_filter_y;
	float post_effect_filter_cb;
	float post_effect_filter_cr;
	int draw_options[RE_DRAW_OPTION_MAX];

	// TapirEngine
	bool suspended;
};

struct RE_instance {
	struct RE_plugin *plugin;
	struct model *model;
	struct particle_effect *effect;
	struct motion *motion;
	struct motion *next_motion;
	struct height_detector *height_detector;

	enum RE_instance_type type;
	int target[RE_NR_INSTANCE_TARGETS];
	vec3 pos;
	float pitch, roll, yaw;  // in degrees
	vec3 scale;
	float alpha;
	bool draw;
	bool draw_shadow;
	bool make_shadow;
	float shadow_volume_bone_radius;
	bool draw_bump;
	float fps;
	bool motion_blend;
	float motion_blend_rate;
	vec3 ambient;
	vec3 column_pos;
	float column_height;
	float column_radius;
	float column_angle;  // around the Y axis, in degrees

	// Billboards
	enum RE_draw_type draw_type;

	// Lights
	vec3 vec;
	vec3 diffuse;
	vec3 globe_diffuse;

	// Private
	bool local_transform_needs_update;
	mat4 local_transform;
	mat3 normal_transform;
	mat3x4 *bone_transforms;  // row-major, model->nr_bones elements
	GLuint bone_transforms_ubo;
	vec4 bounding_sphere;
	struct RE_instance *shadow_volume_instance;
	float z_from_camera;
	int texture_animation_index;
};

struct RE_plugin *RE_plugin_new(enum RE_plugin_version version);
void RE_plugin_free(struct RE_plugin *plugin);
bool RE_plugin_bind(struct RE_plugin *plugin, int sprite);
bool RE_plugin_unbind(struct RE_plugin *plugin);
bool RE_plugin_suspend(struct RE_plugin *plugin);
bool RE_plugin_resume(struct RE_plugin *plugin);
bool RE_build_model(struct RE_plugin *plugin, int elapsed_ms);
bool RE_set_viewport(struct RE_plugin *plugin, int x, int y, int width, int height);
bool RE_set_projection(struct RE_plugin *plugin, float width, float height, float near, float far, float deg);
int RE_create_instance(struct RE_plugin *plugin);
bool RE_release_instance(struct RE_plugin *plugin, int instance);

bool RE_instance_set_type(struct RE_instance *instance, int type);
bool RE_instance_load(struct RE_instance *instance, const char *name);
bool RE_instance_load_motion(struct RE_instance *instance, const char *name);
bool RE_instance_load_next_motion(struct RE_instance *instance, const char *name);
bool RE_instance_free_next_motion(struct RE_instance *instance);
bool RE_instance_set_mesh_show(struct RE_instance *instance, const char *mesh_name, bool show);
bool RE_instance_set_vertex_pos(struct RE_instance *instance, int index, float x, float y, float z);
int RE_instance_get_bone_index(struct RE_instance *instance, const char *name);
bool RE_instance_trans_local_pos_to_world_pos_by_bone(struct RE_instance *instance, int bone, vec3 offset, vec3 out);
float RE_instance_calc_height(struct RE_instance *instance, float x, float z);
float RE_instance_calc_2d_detection_height(struct RE_instance *instance, float x, float z);
bool RE_instance_calc_2d_detection(struct RE_instance *instance, float x0, float y0, float z0, float x1, float y1, float z1, float *x2, float *y2, float *z2, float radius);
bool RE_instance_set_debug_draw_shadow_volume(struct RE_instance *instance, bool draw);
bool RE_instance_find_path(struct RE_instance *instance, vec3 start, vec3 goal);
const vec3 *RE_instance_get_path_line(struct RE_instance *instance, int *nr_path_points);
bool RE_instance_optimize_path_line(struct RE_instance *instance);
bool RE_instance_calc_path_finder_intersect_eye_vec(struct RE_instance *instance, int mouse_x, int mouse_y, vec3 out);

int RE_motion_get_state(struct motion *motion);
bool RE_motion_set_state(struct motion *motion, int state);
float RE_motion_get_frame(struct motion *motion);
bool RE_motion_set_frame(struct motion *motion, float frame);
bool RE_motion_set_frame_range(struct motion *motion, float begin, float end);
bool RE_motion_set_loop_frame_range(struct motion *motion, float begin, float end);

int RE_effect_get_num_object(struct particle_effect *effect);
bool RE_effect_get_camera_quake_flag(struct particle_effect *effect);
bool RE_effect_set_camera_quake_flag(struct particle_effect *effect, bool enable);
struct particle_object *RE_get_effect_object(struct particle_effect *effect, unsigned object);
int RE_particle_get_type(struct particle_object *po);
int RE_particle_get_move_type(struct particle_object *po);
int RE_particle_get_up_vec_type(struct particle_object *po);
float RE_particle_get_move_curve(struct particle_object *po);
void RE_particle_get_frame(struct particle_object *po, int *begin_frame, int *end_frame);
int RE_particle_get_stop_frame(struct particle_object *po);
const char *RE_particle_get_name(struct particle_object *po);
int RE_particle_get_num_pos(struct particle_object *po);
int RE_particle_get_num_pos_unit(struct particle_object *po, int pos);
int RE_particle_get_pos_unit_type(struct particle_object *po, int pos, int unit);
int RE_particle_get_pos_unit_index(struct particle_object *po, int pos, int unit);
int RE_particle_get_num_texture(struct particle_object *po);
const char *RE_particle_get_texture(struct particle_object *po, int texture);
bool RE_particle_get_size(struct particle_object *po, float *begin_size, float *end_size);
bool RE_particle_get_size2(struct particle_object *po, int frame, float *size);
bool RE_particle_get_size_x(struct particle_object *po, int frame, float *size);
bool RE_particle_get_size_y(struct particle_object *po, int frame, float *size);
bool RE_particle_get_size_type(struct particle_object *po, int frame, int *type);
bool RE_particle_get_size_x_type(struct particle_object *po, int frame, int *type);
bool RE_particle_get_size_y_type(struct particle_object *po, int frame, int *type);
int RE_particle_get_num_size2(struct particle_object *po);
int RE_particle_get_num_size_x(struct particle_object *po);
int RE_particle_get_num_size_y(struct particle_object *po);
int RE_particle_get_num_size_type(struct particle_object *po);
int RE_particle_get_num_size_x_type(struct particle_object *po);
int RE_particle_get_num_size_y_type(struct particle_object *po);
int RE_particle_get_blend_type(struct particle_object *po);
const char *RE_particle_get_polygon_name(struct particle_object *po);
int RE_particle_get_num_particles(struct particle_object *po);
int RE_particle_get_alpha_fadein_time(struct particle_object *po);
int RE_particle_get_alpha_fadeout_time(struct particle_object *po);
int RE_particle_get_texture_anime_time(struct particle_object *po);
float RE_particle_get_alpha_fadein_frame(struct particle_object *po);
float RE_particle_get_alpha_fadeout_frame(struct particle_object *po);
float RE_particle_get_texture_anime_frame(struct particle_object *po);
int RE_particle_get_frame_ref_type(struct particle_object *po);
int RE_particle_get_frame_ref_param(struct particle_object *po);
void RE_particle_get_x_rotation_angle(struct particle_object *po, float *begin_angle, float *end_angle);
void RE_particle_get_y_rotation_angle(struct particle_object *po, float *begin_angle, float *end_angle);
void RE_particle_get_z_rotation_angle(struct particle_object *po, float *begin_angle, float *end_angle);
void RE_particle_get_x_revolution_angle(struct particle_object *po, float *begin_angle, float *end_angle);
void RE_particle_get_x_revolution_distance(struct particle_object *po, float *begin_distance, float *end_distance);
void RE_particle_get_y_revolution_angle(struct particle_object *po, float *begin_angle, float *end_angle);
void RE_particle_get_y_revolution_distance(struct particle_object *po, float *begin_distance, float *end_distance);
void RE_particle_get_z_revolution_angle(struct particle_object *po, float *begin_angle, float *end_angle);
void RE_particle_get_z_revolution_distance(struct particle_object *po, float *begin_distance, float *end_distance);
bool RE_particle_get_curve_length(struct particle_object *po, vec3 out);
int RE_particle_get_child_frame(struct particle_object *po);
float RE_particle_get_child_length(struct particle_object *po);
float RE_particle_get_child_begin_slope(struct particle_object *po);
float RE_particle_get_child_end_slope(struct particle_object *po);
float RE_particle_get_child_create_begin_frame(struct particle_object *po);
float RE_particle_get_child_create_end_frame(struct particle_object *po);
int RE_particle_get_child_move_dir_type(struct particle_object *po);
int RE_particle_get_dir_type(struct particle_object *po);
int RE_particle_get_num_damage(struct particle_object *po);
int RE_particle_get_damage(struct particle_object *po, int frame);
float RE_particle_get_offset_x(struct particle_object *po);
float RE_particle_get_offset_y(struct particle_object *po);
bool RE_effect_get_frame_range(struct RE_instance *instance, int *begin_frame, int *end_frame);

bool RE_back_cg_set(struct RE_back_cg *bcg, int no);
bool RE_back_cg_set_name(struct RE_back_cg *bcg, struct string *name, struct archive *aar);

void RE_render(struct sact_sprite *sp);
cJSON *RE_to_json(struct sact_sprite *sp, bool verbose);

#endif /* SYSTEM4_REIGN_H */
