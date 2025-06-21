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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "3d_internal.h"
#include "asset_manager.h"
#include "reign.h"
#include "sact.h"
#include "vm.h"
#include "xsystem4.h"

enum RE_plugin_version re_plugin_version;

static struct RE_instance *create_instance(struct RE_plugin *plugin)
{
	struct RE_instance *instance = xcalloc(1, sizeof(struct RE_instance));
	instance->plugin = plugin;
	for (int i = 0; i < RE_NR_INSTANCE_TARGETS; i++)
		instance->target[i] = -1;
	instance->scale[0] = 1.0f;
	instance->scale[1] = 1.0f;
	instance->scale[2] = 1.0f;
	instance->alpha = 1.0f;
	instance->shadow_volume_bone_radius = 0.1f;
	instance->draw_bump = true;
	instance->fps = 30.0f;
	instance->column_height = 1.0f;
	instance->column_radius = 1.0f;
	glm_mat4_identity(instance->local_transform);
	glm_mat3_identity(instance->normal_transform);
	return instance;
}

static void free_instance(struct RE_instance *instance);

static void unload_instance(struct RE_instance *instance)
{
	if (instance->motion) {
		motion_free(instance->motion);
		instance->motion = NULL;
	}
	if (instance->next_motion) {
		motion_free(instance->next_motion);
		instance->next_motion = NULL;
	}
	if (instance->model) {
		// No need to free, model is owned by plugin->model_cache.
		instance->model = NULL;
	}
	if (instance->effect) {
		particle_effect_free(instance->effect);
		instance->effect = NULL;
	}
	if (instance->bone_transforms) {
		glDeleteBuffers(1, &instance->bone_transforms_ubo);
		free(instance->bone_transforms);
		instance->bone_transforms = NULL;
	}
	if (instance->height_detector) {
		RE_renderer_free_height_detector(instance->height_detector);
		instance->height_detector = NULL;
	}
	if (instance->shadow_volume_instance) {
		free_instance(instance->shadow_volume_instance);
		instance->shadow_volume_instance = NULL;
	}
}

static void free_instance(struct RE_instance *instance)
{
	unload_instance(instance);
	free(instance);
}

static void RE_back_cg_init(struct RE_back_cg *bcg)
{
	bcg->blend_rate = 1.0;
	bcg->mag = 1.0;
}

static void RE_back_cg_destroy(struct RE_back_cg *bcg)
{
	gfx_delete_texture(&bcg->texture);
	if (bcg->name)
		free_string(bcg->name);
}

static void interpolate_motion_frame(struct mot_frame *m1, struct mot_frame *m2, float t, struct mot_frame *out)
{
	glm_vec3_lerp(m1->pos, m2->pos, t, out->pos);
	glm_quat_nlerp(m1->rotq, m2->rotq, t, out->rotq);
}

static void update_motion(struct motion *m, float delta_frame)
{
	if (!m || m->state == RE_MOTION_STATE_STOP)
		return;
	m->current_frame += delta_frame;
	if (m->current_frame < m->frame_begin)
		m->current_frame = m->frame_begin;
	switch (m->state) {
	case RE_MOTION_STATE_NOLOOP:
		if (m->current_frame > m->frame_end) {
			m->current_frame = m->frame_end;
			m->state = RE_MOTION_STATE_STOP;
		}
		break;
	case RE_MOTION_STATE_LOOP:
		if (m->loop_frame_begin == m->loop_frame_end)
			m->current_frame = m->loop_frame_begin;
		else if (m->current_frame > m->loop_frame_end)
			m->current_frame = m->loop_frame_begin + fmodf(m->current_frame - m->loop_frame_begin, m->loop_frame_end - m->loop_frame_begin);
		break;
	default:
		VM_ERROR("Invalid motion state %d", m->state);
	}

	uint32_t anim_frame = m->current_frame - m->frame_begin;
	m->texture_index = (m->mot && anim_frame < m->mot->nr_texture_indices)
		? m->mot->texture_indices[anim_frame] : 0;
}

static void calc_motion_frame(struct motion *m, int bone, struct mot_frame *out)
{
	uint32_t cur_frame = m->current_frame;
	uint32_t next_frame = ceilf(m->current_frame);
	if (next_frame >= m->mot->nr_frames) {
		*out = m->mot->motions[bone]->frames[m->mot->nr_frames - 1];
		return;
	}
	float t = m->current_frame - cur_frame;
	struct mot_frame *frames = m->mot->motions[bone]->frames;
	interpolate_motion_frame(&frames[cur_frame], &frames[next_frame], t, out);
}

static void update_bones(struct RE_instance *inst)
{
	if (!inst->motion)
		return;

	mat4 parent_transforms[MAX_BONES];
	vec3 aabb[2];
	glm_aabb_invalidate(aabb);

	// Update inst->bone_transforms.
	for (int i = 0; i < inst->model->nr_bones; i++) {
		struct mot_frame mf;
		calc_motion_frame(inst->motion, i, &mf);
		if (inst->motion_blend && inst->next_motion) {
			struct mot_frame next_mf;
			calc_motion_frame(inst->next_motion, i, &next_mf);
			interpolate_motion_frame(&mf, &next_mf, inst->motion_blend_rate, &mf);
		}
		mat4 bone_transform;
		glm_translate_make(bone_transform, mf.pos);
		glm_quat_rotate(bone_transform, mf.rotq, bone_transform);

		if (inst->model->bones[i].parent >= 0) {
			assert(inst->model->bones[i].parent < i);
			glm_mat4_mul(parent_transforms[inst->model->bones[i].parent], bone_transform, bone_transform);
		}
		glm_mat4_copy(bone_transform, parent_transforms[i]);

		glm_vec3_minv(aabb[0], bone_transform[3], aabb[0]);
		glm_vec3_maxv(aabb[1], bone_transform[3], aabb[1]);

		glm_mat4_mul(bone_transform, inst->model->bones[i].inverse_bind_matrix, bone_transform);
		glm_mat4_transpose(bone_transform);  // column major -> row major
		glm_mat3x4_copy(bone_transform, inst->bone_transforms[i]);
	}
	glBindBuffer(GL_UNIFORM_BUFFER, inst->bone_transforms_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, inst->model->nr_bones * sizeof(mat3x4), inst->bone_transforms);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	// Update inst->bounding_sphere.
	vec3 center;
	glm_aabb_center(aabb, center);
	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	glm_mat4_mulv3(inst->local_transform, center, 1.0f, inst->bounding_sphere);
	inst->bounding_sphere[3] = glm_aabb_radius(aabb) * inst->scale[0] + inst->shadow_volume_bone_radius;

	struct RE_instance *shvol = inst->shadow_volume_instance;
	if (shvol) {
		glm_vec3_copy(inst->bounding_sphere, shvol->pos);
		glm_vec3_broadcast(inst->bounding_sphere[3], shvol->scale);
		RE_instance_update_local_transform(shvol);
	}
}

struct RE_plugin *RE_plugin_new(enum RE_plugin_version version)
{
	re_plugin_version = version;

	char *aar_path = gamedir_path("Data/ReignData.red");
	int error = ARCHIVE_SUCCESS;
	struct archive *aar = (struct archive *)aar_open(aar_path, MMAP_IF_64BIT, &error);
	if (error == ARCHIVE_FILE_ERROR) {
		WARNING("aar_open(\"%s\"): %s", display_utf0(aar_path), strerror(errno));
	} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
		WARNING("aar_open(\"%s\"): invalid AAR archive", display_utf0(aar_path));
	}
	free(aar_path);
	if (!aar)
		return NULL;

	struct RE_plugin *plugin = xcalloc(1, sizeof(struct RE_plugin));
	plugin->plugin.name = "ReignEngine";
	plugin->plugin.update = RE_render;
	plugin->plugin.to_json = RE_to_json;
	plugin->nr_instances = 16;
	plugin->instances = xcalloc(plugin->nr_instances, sizeof(struct RE_instance *));
	plugin->aar = aar;
	plugin->model_cache = ht_create(32);
	plugin->pae_cache = ht_create(32);
	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		RE_back_cg_init(&plugin->back_cg[i]);
	plugin->fog_type = RE_FOG_NONE;
	plugin->fog_near = 1.0;
	plugin->fog_far = 10.0;
	glm_vec3_one(plugin->fog_color);
	return plugin;
}

void RE_plugin_free(struct RE_plugin *plugin)
{
	for (int i = 0; i < plugin->nr_instances; i++) {
		if (plugin->instances[i])
			free_instance(plugin->instances[i]);
	}
	free(plugin->instances);
	archive_free(plugin->aar);
	ht_foreach_value(plugin->model_cache, (void(*)(void*))model_free);
	ht_free(plugin->model_cache);
	ht_foreach_value(plugin->pae_cache, (void(*)(void*))pae_free);
	ht_free(plugin->pae_cache);
	if (plugin->renderer)
		RE_renderer_free(plugin->renderer);
	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		RE_back_cg_destroy(&plugin->back_cg[i]);
	free(plugin);
}

bool RE_plugin_bind(struct RE_plugin *plugin, int sprite)
{
	struct sact_sprite *sp = sact_try_get_sprite(sprite);
	if (!sp)
		return false;
	plugin->renderer = RE_renderer_new();
	plugin->sprite = sprite;
	sprite_bind_plugin(sp, &plugin->plugin);
	struct texture *texture = sprite_get_texture(sp);
	RE_renderer_set_viewport_size(plugin->renderer, texture->w, texture->h);
	return true;
}

bool RE_plugin_unbind(struct RE_plugin *plugin)
{
	if (!plugin || !plugin->renderer)
		return false;
	struct sact_sprite *sp = sact_try_get_sprite(plugin->sprite);
	if (sp)
		sprite_bind_plugin(sp, NULL);
	RE_renderer_free(plugin->renderer);
	plugin->renderer = NULL;
	plugin->sprite = 0;
	return true;
}

bool RE_plugin_suspend(struct RE_plugin *plugin)
{
	if (!plugin)
		return false;
	plugin->suspended = true;
	struct sact_sprite *sp = sact_try_get_sprite(plugin->sprite);
	if (sp)
		sprite_set_show(sp, false);
	return true;
}

bool RE_plugin_resume(struct RE_plugin *plugin)
{
	if (!plugin)
		return false;
	plugin->suspended = false;
	struct sact_sprite *sp = sact_try_get_sprite(plugin->sprite);
	if (sp)
		sprite_set_show(sp, true);
	return true;
}

bool RE_build_model(struct RE_plugin *plugin, int elapsed_ms)
{
	if (!plugin)
		return false;

	plugin->camera.quake_pitch = plugin->camera.quake_yaw = 0.0;

	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst)
			continue;

		float delta_frame = inst->fps * elapsed_ms / 1000.0;
		update_motion(inst->motion, delta_frame);
		update_motion(inst->next_motion, delta_frame);

		if (inst->type == RE_ITYPE_SKINNED)
			update_bones(inst);
		inst->texture_animation_index = inst->motion ? inst->motion->texture_index : 0;
	}

	// Update particle effects in a second pass, because it uses the results of
	// update_bones().
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (inst && inst->type == RE_ITYPE_PARTICLE_EFFECT)
			particle_effect_update(inst);
	}
	return true;
}

bool RE_set_viewport(struct RE_plugin *plugin, int x, int y, int width, int height)
{
	if (!plugin)
		return false;
	struct sact_sprite *sp = sact_try_get_sprite(plugin->sprite);
	if (!sp || sp->plugin != &plugin->plugin) {
		WARNING("No sprite bound to plugin");
		return false;
	}
	sprite_init_color(sp, width, height, 0, 0, 0, 255);
	sprite_set_pos(sp, x, y);
	RE_renderer_set_viewport_size(plugin->renderer, width, height);
	return true;
}

bool RE_set_projection(struct RE_plugin *plugin, float width, float height, float near, float far, float deg)
{
	if (!plugin)
		return false;

	float aspect = width / height;
	glm_perspective(glm_rad(deg), aspect, near, far, plugin->proj_transform);

	// Adjust the X and Y scaling factor, because ReignEngine calculates the
	// projection matrix based on the horizontal FOV while glm_perspective takes
	// the vertical FOV.
	plugin->proj_transform[0][0] *= aspect;
	plugin->proj_transform[1][1] *= aspect;
	return true;
}

int RE_create_instance(struct RE_plugin *plugin)
{
	if (!plugin)
		return -1;
	for (int i = 0; i < plugin->nr_instances; i++) {
		if (!plugin->instances[i]) {
			plugin->instances[i] = create_instance(plugin);
			return i;
		}
	}
	int old_size = plugin->nr_instances;
	plugin->nr_instances *= 2;
	plugin->instances = xrealloc_array(plugin->instances, old_size, plugin->nr_instances, sizeof(struct RE_instance *));
	plugin->instances[old_size] = create_instance(plugin);
	return old_size;
}

bool RE_release_instance(struct RE_plugin *plugin, int instance)
{
	if (!plugin || !plugin->instances[instance])
		return false;
	free_instance(plugin->instances[instance]);
	plugin->instances[instance] = NULL;
	return true;
}

bool RE_instance_set_type(struct RE_instance *instance, int type)
{
	if (!instance)
		return false;
	if (instance->type != RE_ITYPE_UNINITIALIZED)
		ERROR("instance type cannot be changed");
	switch (type) {
	case RE_ITYPE_STATIC:
		break;
	case RE_ITYPE_SKINNED:
		break;
	case RE_ITYPE_BILLBOARD:
		assert(!instance->motion);
		instance->motion = xcalloc(1, sizeof(struct motion));
		instance->motion->instance = instance;
		break;
	case RE_ITYPE_DIRECTIONAL_LIGHT:
		break;
	case RE_ITYPE_SPECULAR_LIGHT:
		break;
	case RE_ITYPE_PARTICLE_EFFECT:
		break;
	case RE_ITYPE_PATH_LINE:
		break;
	default:
		VM_ERROR("unknown instance type %d", type);
	}
	instance->type = type;
	return true;
}

bool RE_instance_load(struct RE_instance *instance, const char *name)
{
	if (!instance)
		return false;
	unload_instance(instance);
	if (name[0] == '\0')
		return false;

	struct pae *pae;
	switch (instance->type) {
	case RE_ITYPE_STATIC:
	case RE_ITYPE_SKINNED:
		instance->model = ht_get(instance->plugin->model_cache, name, NULL);
		if (!instance->model) {
			instance->model = model_load(instance->plugin->aar, name);
			if (instance->model)
				ht_put(instance->plugin->model_cache, name, instance->model);
		}
		if (instance->model && instance->model->nr_bones > 0) {
			instance->bone_transforms = xcalloc(instance->model->nr_bones, sizeof(mat3x4));
			glGenBuffers(1, &instance->bone_transforms_ubo);
			glBindBuffer(GL_UNIFORM_BUFFER, instance->bone_transforms_ubo);
			glBufferData(GL_UNIFORM_BUFFER, MAX_BONES * sizeof(mat3x4), NULL, GL_DYNAMIC_DRAW);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}
		return !!instance->model;
	case RE_ITYPE_PARTICLE_EFFECT:
		pae = ht_get(instance->plugin->pae_cache, name, NULL);
		if (!pae) {
			pae = pae_load(instance->plugin->aar, name);
			if (!pae)
				return false;
			ht_put(instance->plugin->pae_cache, name, pae);
		}
		instance->effect = particle_effect_create(pae);
		if (!instance->motion) {
			instance->motion = xcalloc(1, sizeof(struct motion));
			instance->motion->instance = instance;
			pae_calc_frame_range(instance->effect->pae, instance->motion);
		}
		return true;
	default:
		WARNING("Invalid instance type %d", instance->type);
		return false;
	}
}

bool RE_instance_load_motion(struct RE_instance *instance, const char *name)
{
	if (!instance)
		return false;
	if (instance->motion)
		motion_free(instance->motion);
	instance->motion = motion_load(name, instance, instance->plugin->aar);
	return !!instance->motion;
}

bool RE_instance_load_next_motion(struct RE_instance *instance, const char *name)
{
	if (!instance)
		return false;
	if (instance->next_motion)
		motion_free(instance->next_motion);
	instance->next_motion = motion_load(name, instance, instance->plugin->aar);
	return !!instance->next_motion;
}

bool RE_instance_free_next_motion(struct RE_instance *instance)
{
	if (!instance || !instance->next_motion)
		return false;
	motion_free(instance->next_motion);
	instance->next_motion = NULL;
	return true;
}

bool RE_instance_set_mesh_show(struct RE_instance *instance, const char *mesh_name, bool show)
{
	if (!instance || !instance->model)
		return false;
	for (int i = 0; i < instance->model->nr_meshes; i++) {
		if (!strcmp(instance->model->meshes[i].name, mesh_name)) {
			instance->model->meshes[i].hidden = !show;
			return true;
		}
	}
	return false;
}

bool RE_instance_set_vertex_pos(struct RE_instance *instance, int index, float x, float y, float z)
{
	if (!instance)
		return false;
	if (instance->type != RE_ITYPE_BILLBOARD)
		ERROR("not implemented");
	// Hack: This works because SetInstanceVertexPos is only used to scale
	// billboards, and SetInstanceScale is not used for billboards.
	if (index == 1) {
		instance->scale[0] = x;
		instance->scale[1] = y / 2.0;
	}
	return false;
}

int RE_instance_get_bone_index(struct RE_instance *instance, const char *name)
{
	if (!instance || !instance->model)
		return -1;
	for (int i = 0; i < instance->model->nr_bones; i++) {
		if (!strcmp(instance->model->bones[i].name, name))
			return i;
	}
	return -1;
}

bool RE_instance_trans_local_pos_to_world_pos_by_bone(struct RE_instance *instance, int bone, vec3 offset, vec3 out)
{
	if (!instance || !instance->bone_transforms || (unsigned)bone >= (unsigned)instance->model->nr_bones)
		return false;

	if (instance->local_transform_needs_update)
		RE_instance_update_local_transform(instance);

	mat4 bone_transform = GLM_MAT4_IDENTITY_INIT;
	glm_mat3x4_copy(instance->bone_transforms[bone], bone_transform);
	glm_mat4_transpose(bone_transform);  // row major -> column major
	glm_mat4_mulv3(bone_transform, offset, 1.0, out);
	glm_mat4_mulv3(instance->local_transform, out, 1.0, out);
	return true;
}

float RE_instance_calc_height(struct RE_instance *instance, float x, float z)
{
	if (!instance || !instance->model)
		return 0.0;
	if (!instance->height_detector) {
		instance->height_detector = RE_renderer_create_height_detector(instance->plugin->renderer, instance->model);
	}

	float y;
	if (RE_renderer_detect_height(instance->height_detector, x, z, &y))
		return y;

	// Return the average of 4 neighbors.
	int cnt = 0;
	float total = 0.0f;
	if (RE_renderer_detect_height(instance->height_detector, x - 0.5f, z, &y)) { cnt++; total += y; }
	if (RE_renderer_detect_height(instance->height_detector, x + 0.5f, z, &y)) { cnt++; total += y; }
	if (RE_renderer_detect_height(instance->height_detector, x, z - 0.5f, &y)) { cnt++; total += y; }
	if (RE_renderer_detect_height(instance->height_detector, x, z + 0.5f, &y)) { cnt++; total += y; }
	return cnt ? total / cnt : 0.0f;
}

float RE_instance_calc_2d_detection_height(struct RE_instance *instance, float x, float z)
{
	if (!instance || !instance->model->collider)
		return false;
	vec2 xz = { x, -z };
	float h;
	if (collider_height(instance->model->collider, xz, &h))
		return h;
	return 0.f;
}

bool RE_instance_calc_2d_detection(struct RE_instance *instance, float x0, float y0, float z0, float x1, float y1, float z1, float *x2, float *y2, float *z2, float radius)
{
	if (!instance || !instance->model->collider)
		return false;
	vec2 p0 = { x0, -z0 };
	vec2 p1 = { x1, -z1 };
	vec2 p2;
	if (!check_collision(instance->model->collider, p0, p1, radius, p2))
		return false;
	*x2 = p2[0];
	*z2 = -p2[1];
	return collider_height(instance->model->collider, p2, y2);
}

bool RE_instance_set_debug_draw_shadow_volume(struct RE_instance *inst, bool draw)
{
	if (!inst)
		return false;
	if (draw && !inst->shadow_volume_instance) {
		inst->shadow_volume_instance = create_instance(inst->plugin);
		inst->shadow_volume_instance->model = model_create_sphere(255, 0, 0, 64);
	}
	if (inst->shadow_volume_instance)
		inst->shadow_volume_instance->draw = draw;
	return true;
}

bool RE_instance_find_path(struct RE_instance *inst, vec3 start, vec3 goal)
{
	if (!inst || !inst->model->collider)
		return false;
	mat4 vp_transform;
	RE_calc_view_matrix(&inst->plugin->camera, GLM_YUP, vp_transform);
	glm_mat4_mul(inst->plugin->proj_transform, vp_transform, vp_transform);
	return collider_find_path(inst->model->collider, start, goal, vp_transform);
}

const vec3 *RE_instance_get_path_line(struct RE_instance *inst, int *nr_path_points)
{
	if (!inst || !inst->model->collider)
		return NULL;
	*nr_path_points = inst->model->collider->nr_path_points;
	return inst->model->collider->path_points;
}

bool RE_instance_optimize_path_line(struct RE_instance *inst)
{
	if (!inst || !inst->model->collider)
		return false;
	return collider_optimize_path(inst->model->collider);
}

bool RE_instance_calc_path_finder_intersect_eye_vec(struct RE_instance *inst, int mouse_x, int mouse_y, vec3 out)
{
	if (!inst || !inst->model->collider)
		return false;

	float ndc_x = 2.f * mouse_x / inst->plugin->renderer->viewport_width - 1.f;
	float ndc_y = 1.f - 2.f * mouse_y / inst->plugin->renderer->viewport_height;
	vec4 near = { ndc_x, ndc_y, -1.f, 1.f };
	vec4 far  = { ndc_x, ndc_y,  1.f, 1.f };

	mat4 inv_vp;
	RE_calc_view_matrix(&inst->plugin->camera, GLM_YUP, inv_vp);
	glm_mat4_mul(inst->plugin->proj_transform, inv_vp, inv_vp);
	glm_mat4_inv(inv_vp, inv_vp);

	glm_mat4_mulv(inv_vp, near, near);
	glm_mat4_mulv(inv_vp, far, far);
	glm_vec3_divs(near, near[3], near);
	glm_vec3_divs(far, far[3], far);

	vec3 origin, direction;
	glm_vec3_copy(near, origin);
	glm_vec3_sub(far, near, direction);
	glm_vec3_normalize(direction);

	return collider_raycast(inst->model->collider, origin, direction, out);
}

void RE_instance_update_local_transform(struct RE_instance *inst)
{
	vec3 euler = {
		glm_rad(inst->pitch),
		glm_rad(inst->yaw),
		glm_rad(inst->roll)
	};
	mat4 rot;
	glm_euler(euler, rot);

	glm_translate_make(inst->local_transform, inst->pos);
	glm_mat4_mul(inst->local_transform, rot, inst->local_transform);
	glm_scale(inst->local_transform, inst->scale);

	mat3 normal;
	glm_mat4_pick3(inst->local_transform, normal);
	glm_mat3_inv(normal, normal);
	glm_mat3_transpose_to(normal, inst->normal_transform);

	inst->local_transform_needs_update = false;
}

int RE_motion_get_state(struct motion *motion)
{
	return motion ? motion->state : RE_MOTION_STATE_STOP;
}

bool RE_motion_set_state(struct motion *motion, int state)
{
	if (!motion)
		return false;
	motion->state = state;
	return true;
}

float RE_motion_get_frame(struct motion *motion)
{
	return motion ? motion->current_frame : 0.0;
}

bool RE_motion_set_frame(struct motion *motion, float frame)
{
	if (!motion)
		return false;
	motion->current_frame = frame;
	return true;
}

bool RE_motion_set_frame_range(struct motion *motion, float begin, float end)
{
	if (!motion)
		return false;
	motion->frame_begin = begin;
	motion->frame_end = end;
	if (motion->instance->type == RE_ITYPE_BILLBOARD) {
		for (int i = begin; i < end; i++) {
			if (!RE_renderer_load_billboard_texture(motion->instance->plugin->renderer, i))
				return false;
		}
	}
	return true;
}

bool RE_motion_set_loop_frame_range(struct motion *motion, float begin, float end)
{
	if (!motion)
		return false;
	motion->loop_frame_begin = begin;
	motion->loop_frame_end = end;
	if (motion->instance->type == RE_ITYPE_BILLBOARD) {
		for (int i = begin; i < end; i++) {
			if (!RE_renderer_load_billboard_texture(motion->instance->plugin->renderer, i))
				return false;
		}
	}
	return true;
}

int RE_effect_get_num_object(struct particle_effect *effect)
{
	return effect ? effect->pae->nr_objects : 0;
}

bool RE_effect_get_camera_quake_flag(struct particle_effect *effect)
{
	return effect && effect->camera_quake_enabled;
}

bool RE_effect_set_camera_quake_flag(struct particle_effect *effect, bool enable)
{
	if (!effect)
		return false;
	effect->camera_quake_enabled = enable;
	return true;
}

struct particle_object *RE_get_effect_object(struct particle_effect *effect, unsigned object)
{
	if (!effect || object >= (unsigned)effect->pae->nr_objects)
		return NULL;
	return &effect->objects[object];
}

int RE_particle_get_type(struct particle_object *po)
{
	return po ? po->pae_obj->type : 0;
}

int RE_particle_get_move_type(struct particle_object *po)
{
	return po ? po->pae_obj->move_type : 0;
}

int RE_particle_get_up_vec_type(struct particle_object *po)
{
	return po ? po->pae_obj->up_vector_type : 0;
}

float RE_particle_get_move_curve(struct particle_object *po)
{
	return po ? po->pae_obj->move_curve : 0.0;
}

void RE_particle_get_frame(struct particle_object *po, int *begin_frame, int *end_frame)
{
	if (!po)
		return;
	*begin_frame = po->pae_obj->begin_frame;
	*end_frame = po->pae_obj->end_frame;
}

int RE_particle_get_stop_frame(struct particle_object *po)
{
	return po ? po->pae_obj->stop_frame : 0;
}

const char *RE_particle_get_name(struct particle_object *po)
{
	return po ? po->pae_obj->name : NULL;
}

int RE_particle_get_num_pos(struct particle_object *po)
{
	return po ? NR_PARTICLE_POSITIONS : 0;
}

int RE_particle_get_num_pos_unit(struct particle_object *po, int pos)
{
	return po ? NR_PARTICLE_POSITION_UNITS : 0;
}

static struct particle_position_unit *get_position_unit(struct particle_object *po, int pos, int unit)
{
	if (!po || pos >= NR_PARTICLE_POSITIONS || !po->pae_obj->position[pos] || unit >= NR_PARTICLE_POSITION_UNITS)
		return NULL;
	return &po->pae_obj->position[pos]->units[unit];
}

int RE_particle_get_pos_unit_type(struct particle_object *po, int pos, int unit)
{
	struct particle_position_unit *u = get_position_unit(po, pos, unit);
	return u ? u->type : 0;
}

int RE_particle_get_pos_unit_index(struct particle_object *po, int pos, int unit)
{
	struct particle_position_unit *u = get_position_unit(po, pos, unit);
	if (!u)
		return 0;
	switch (u->type) {
	case PARTICLE_POS_UNIT_BONE:
		return u->bone.target_index;
	case PARTICLE_POS_UNIT_TARGET:
		return u->target.index;
	default:
		return 0;
	}
}

int RE_particle_get_num_texture(struct particle_object *po)
{
	return po ? po->pae_obj->nr_textures : 0;
}

const char *RE_particle_get_texture(struct particle_object *po, int texture)
{
	if (!po || texture >= po->pae_obj->nr_textures)
		return NULL;
	return po->pae_obj->textures[texture];
}

bool RE_particle_get_size(struct particle_object *po, float *begin_size, float *end_size)
{
	if (!po)
		return false;
	*begin_size = po->pae_obj->size[0];
	*end_size = po->pae_obj->size[1];
	return true;
}

bool RE_particle_get_size2(struct particle_object *po, int frame, float *size)
{
	if (!po || frame >= po->pae_obj->nr_sizes2)
		return false;
	*size = po->pae_obj->sizes2[frame];
	return true;
}

bool RE_particle_get_size_x(struct particle_object *po, int frame, float *size)
{
	if (!po || frame >= po->pae_obj->nr_sizes_x)
		return false;
	*size = po->pae_obj->sizes_x[frame];
	return true;
}

bool RE_particle_get_size_y(struct particle_object *po, int frame, float *size)
{
	if (!po || frame >= po->pae_obj->nr_sizes_y)
		return false;
	*size = po->pae_obj->sizes_y[frame];
	return true;
}

bool RE_particle_get_size_type(struct particle_object *po, int frame, int *type)
{
	if (!po || frame >= po->pae_obj->nr_size_types)
		return false;
	*type = po->pae_obj->size_types[frame];
	return true;
}

bool RE_particle_get_size_x_type(struct particle_object *po, int frame, int *type)
{
	if (!po || frame >= po->pae_obj->nr_size_x_types)
		return false;
	*type = po->pae_obj->size_x_types[frame];
	return true;
}

bool RE_particle_get_size_y_type(struct particle_object *po, int frame, int *type)
{
	if (!po || frame >= po->pae_obj->nr_size_y_types)
		return false;
	*type = po->pae_obj->size_y_types[frame];
	return true;
}

int RE_particle_get_num_size2(struct particle_object *po)
{
	return po ? po->pae_obj->nr_sizes2 : 0;
}

int RE_particle_get_num_size_x(struct particle_object *po)
{
	return po ? po->pae_obj->nr_sizes_x : 0;
}

int RE_particle_get_num_size_y(struct particle_object *po)
{
	return po ? po->pae_obj->nr_sizes_y : 0;
}

int RE_particle_get_num_size_type(struct particle_object *po)
{
	return po ? po->pae_obj->nr_size_types : 0;
}

int RE_particle_get_num_size_x_type(struct particle_object *po)
{
	return po ? po->pae_obj->nr_size_x_types : 0;
}

int RE_particle_get_num_size_y_type(struct particle_object *po)
{
	return po ? po->pae_obj->nr_size_y_types : 0;
}

int RE_particle_get_blend_type(struct particle_object *po)
{
	return po ? po->pae_obj->blend_type : 0;
}

const char *RE_particle_get_polygon_name(struct particle_object *po)
{
	return po ? po->pae_obj->polygon_name : NULL;
}

int RE_particle_get_num_particles(struct particle_object *po)
{
	return po ? po->pae_obj->nr_particles : 0;
}

int RE_particle_get_alpha_fadein_time(struct particle_object *po)
{
	return po ? po->pae_obj->alpha_fadein_time : 0;
}

int RE_particle_get_alpha_fadeout_time(struct particle_object *po)
{
	return po ? po->pae_obj->alpha_fadeout_time : 0;
}

int RE_particle_get_texture_anime_time(struct particle_object *po)
{
	return po ? po->pae_obj->texture_anime_time : 0;
}

float RE_particle_get_alpha_fadein_frame(struct particle_object *po)
{
	return po ? po->pae_obj->alpha_fadein_frame : 0.0;
}

float RE_particle_get_alpha_fadeout_frame(struct particle_object *po)
{
	return po ? po->pae_obj->alpha_fadeout_frame : 0.0;
}

float RE_particle_get_texture_anime_frame(struct particle_object *po)
{
	return po ? po->pae_obj->texture_anime_frame : 0.0;
}

int RE_particle_get_frame_ref_type(struct particle_object *po)
{
	return po ? po->pae_obj->frame_ref_type : 0;
}

int RE_particle_get_frame_ref_param(struct particle_object *po)
{
	return po ? po->pae_obj->frame_ref_param : 0;
}

void RE_particle_get_x_rotation_angle(struct particle_object *po, float *begin_angle, float *end_angle)
{
	if (!po)
		return;
	*begin_angle = po->pae_obj->rotation.begin[0];
	*end_angle = po->pae_obj->rotation.end[0];
}

void RE_particle_get_y_rotation_angle(struct particle_object *po, float *begin_angle, float *end_angle)
{
	if (!po)
		return;
	*begin_angle = po->pae_obj->rotation.begin[1];
	*end_angle = po->pae_obj->rotation.end[1];
}

void RE_particle_get_z_rotation_angle(struct particle_object *po, float *begin_angle, float *end_angle)
{
	if (!po)
		return;
	*begin_angle = po->pae_obj->rotation.begin[2];
	*end_angle = po->pae_obj->rotation.end[2];
}

void RE_particle_get_x_revolution_angle(struct particle_object *po, float *begin_angle, float *end_angle)
{
	if (!po)
		return;
	*begin_angle = po->pae_obj->revolution_angle.begin[0];
	*end_angle = po->pae_obj->revolution_angle.end[0];
}

void RE_particle_get_x_revolution_distance(struct particle_object *po, float *begin_distance, float *end_distance)
{
	if (!po)
		return;
	*begin_distance = po->pae_obj->revolution_distance.begin[0];
	*end_distance = po->pae_obj->revolution_distance.end[0];
}

void RE_particle_get_y_revolution_angle(struct particle_object *po, float *begin_angle, float *end_angle)
{
	if (!po)
		return;
	*begin_angle = po->pae_obj->revolution_angle.begin[1];
	*end_angle = po->pae_obj->revolution_angle.end[1];
}

void RE_particle_get_y_revolution_distance(struct particle_object *po, float *begin_distance, float *end_distance)
{
	if (!po)
		return;
	*begin_distance = po->pae_obj->revolution_distance.begin[1];
	*end_distance = po->pae_obj->revolution_distance.end[1];
}

void RE_particle_get_z_revolution_angle(struct particle_object *po, float *begin_angle, float *end_angle)
{
	if (!po)
		return;
	*begin_angle = po->pae_obj->revolution_angle.begin[2];
	*end_angle = po->pae_obj->revolution_angle.end[2];
}

void RE_particle_get_z_revolution_distance(struct particle_object *po, float *begin_distance, float *end_distance)
{
	if (!po)
		return;
	*begin_distance = po->pae_obj->revolution_distance.begin[2];
	*end_distance = po->pae_obj->revolution_distance.end[2];
}

bool RE_particle_get_curve_length(struct particle_object *po, vec3 out)
{
	if (!po)
		return false;
	glm_vec3_copy(po->pae_obj->curve_length, out);
	return true;
}

int RE_particle_get_child_frame(struct particle_object *po)
{
	return po ? po->pae_obj->child_frame : 0;
}

float RE_particle_get_child_length(struct particle_object *po)
{
	return po ? po->pae_obj->child_length : 0.0;
}

float RE_particle_get_child_begin_slope(struct particle_object *po)
{
	return po ? po->pae_obj->child_begin_slope : 0.0;
}

float RE_particle_get_child_end_slope(struct particle_object *po)
{
	return po ? po->pae_obj->child_end_slope : 0.0;
}

float RE_particle_get_child_create_begin_frame(struct particle_object *po)
{
	return po ? po->pae_obj->child_create_begin_frame : 0.0;
}

float RE_particle_get_child_create_end_frame(struct particle_object *po)
{
	return po ? po->pae_obj->child_create_end_frame : 0.0;
}

int RE_particle_get_child_move_dir_type(struct particle_object *po)
{
	return po ? po->pae_obj->child_move_dir_type : 0;
}

int RE_particle_get_dir_type(struct particle_object *po)
{
	return po ? po->pae_obj->dir_type : 0;
}

int RE_particle_get_num_damage(struct particle_object *po)
{
	return po ? po->pae_obj->nr_damages : 0;
}

int RE_particle_get_damage(struct particle_object *po, int frame)
{
	if (!po || frame >= po->pae_obj->nr_damages)
		return 0;
	return po->pae_obj->damages[frame];
}

float RE_particle_get_offset_x(struct particle_object *po)
{
	return po ? po->pae_obj->offset_x : 0.0;
}

float RE_particle_get_offset_y(struct particle_object *po)
{
	return po ? po->pae_obj->offset_y : 0.0;
}

bool RE_effect_get_frame_range(struct RE_instance *instance, int *begin_frame, int *end_frame)
{
	if (!instance || !instance->effect || !instance->motion)
		return false;
	*begin_frame = instance->motion->frame_begin;
	*end_frame = instance->motion->frame_end;
	return true;
}

bool RE_back_cg_set(struct RE_back_cg *bcg, int no)
{
	if (!bcg)
		return false;
	if (bcg->name) {
		free_string(bcg->name);
		bcg->name = NULL;
	}
	bcg->no = no;
	gfx_delete_texture(&bcg->texture);

	if (!no)  // unload only.
		return true;

	struct cg *cg = asset_cg_load(no);
	if (!cg) {
		WARNING("cannot load back CG: %d", no);
		return false;
	}
	gfx_init_texture_with_cg(&bcg->texture, cg);
	cg_free(cg);
	return true;
}

bool RE_back_cg_set_name(struct RE_back_cg *bcg, struct string *name, struct archive *aar)
{
	if (!bcg)
		return false;
	if (bcg->name)
		free_string(bcg->name);
	bcg->name = string_ref(name);
	bcg->no = 0;
	gfx_delete_texture(&bcg->texture);

	if (name->size == 0)  // unload only.
		return true;

	char *cg_path = xmalloc(name->size + 5);
	sprintf(cg_path, "%s.bmp", name->text);
	struct archive_data *dfile = archive_get_by_name(aar, cg_path);
	free(cg_path);
	if (!dfile) {
		WARNING("cannot load back CG %s", name->text);
		return false;
	}
	struct cg *cg = cg_load_data(dfile);
	archive_free_data(dfile);
	if (!cg) {
		WARNING("cg_load_data failed: %s", name->text);
		return false;
	}
	gfx_init_texture_with_cg(&bcg->texture, cg);
	cg_free(cg);
	return true;
}
