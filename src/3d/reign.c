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
#include "system4/string.h"

#include "3d_internal.h"
#include "asset_manager.h"
#include "reign.h"
#include "sact.h"
#include "vm.h"
#include "xsystem4.h"

static struct RE_instance *create_instance(struct RE_plugin *plugin)
{
	struct RE_instance *instance = xcalloc(1, sizeof(struct RE_instance));
	instance->plugin = plugin;
	instance->scale[0] = 1.0;
	instance->scale[1] = 1.0;
	instance->scale[2] = 1.0;
	instance->alpha = 1.0;
	instance->draw_bump = true;
	instance->fps = 30.0;
	glm_mat4_identity(instance->local_transform);
	glm_mat3_identity(instance->normal_transform);
	return instance;
}

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
		model_free(instance->model);
		instance->model = NULL;
	}
	if (instance->bone_transforms) {
		free(instance->bone_transforms);
		instance->bone_transforms = NULL;
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
	switch (m->state) {
	case RE_MOTION_STATE_NOLOOP:
		if (m->current_frame > m->frame_end) {
			m->current_frame = m->frame_end;
			m->state = RE_MOTION_STATE_STOP;
		}
		break;
	case RE_MOTION_STATE_LOOP:
		if (m->current_frame > m->loop_frame_end)
			m->current_frame = m->loop_frame_begin + fmodf(m->current_frame - m->loop_frame_begin, m->loop_frame_end - m->loop_frame_begin);
		break;
	default:
		VM_ERROR("Invalid motion state %d", m->state);
	}
}

static void calc_motion_frame(struct motion *m, int bone, struct mot_frame *out)
{
	int cur_frame = m->current_frame;
	int next_frame = ceilf(m->current_frame);
	float t = m->current_frame - cur_frame;
	struct mot_frame *frames = m->mot->motions[bone]->frames;
	interpolate_motion_frame(&frames[cur_frame], &frames[next_frame], t, out);
}

static void animate(struct RE_instance *inst, int elapsed_ms)
{
	float delta_frame = inst->fps * elapsed_ms / 1000.0;
	update_motion(inst->motion, delta_frame);
	update_motion(inst->next_motion, delta_frame);

	if (inst->type != RE_ITYPE_SKINNED || !inst->motion)
		return;

	mat4 parent_transforms[MAX_BONES];
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

		glm_mat4_mul(bone_transform, inst->model->bones[i].inverse_bind_matrix, inst->bone_transforms[i]);
	}
}

struct RE_plugin *RE_plugin_new(void)
{
	char *aar_path = gamedir_path("Data/ReignData.red");
	int error = ARCHIVE_SUCCESS;
	struct archive *aar = (struct archive *)aar_open(aar_path, ARCHIVE_MMAP, &error);
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
	plugin->plugin.debug_print = RE_debug_print;
	plugin->nr_instances = 16;
	plugin->instances = xcalloc(plugin->nr_instances, sizeof(struct RE_instance *));
	plugin->aar = aar;
	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		RE_back_cg_init(&plugin->back_cg[i]);
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
	plugin->renderer = RE_renderer_new(sprite_get_texture(sp));
	plugin->sprite = sprite;
	sprite_bind_plugin(sp, &plugin->plugin);
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

bool RE_build_model(struct RE_plugin *plugin, int elapsed_ms)
{
	if (!plugin)
		return false;

	for (int i = 0; i < plugin->nr_instances; i++) {
		if (plugin->instances[i])
			animate(plugin->instances[i], elapsed_ms);
	}
	return true;
}

bool RE_set_projection(struct RE_plugin *plugin, float width, float height, float near, float far, float deg)
{
	if (!plugin)
		return false;
	glm_perspective(glm_rad(deg), width / height, near, far, plugin->proj_transform);
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
	default:
		ERROR("unknown instance type %d", type);
	}
	instance->type = type;
	return true;
}

bool RE_instance_load(struct RE_instance *instance, const char *name)
{
	if (!instance)
		return false;
	unload_instance(instance);
	instance->model = model_load(instance->plugin->aar, name, instance->plugin->renderer);
	if (instance->model && instance->model->nr_bones > 0)
		instance->bone_transforms = xcalloc(instance->model->nr_bones, sizeof(mat4));
	return !!instance->model;
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

	glm_mat4_mulv3(instance->bone_transforms[bone], offset, 1.0, out);
	glm_mat4_mulv3(instance->local_transform, out, 1.0, out);
	return true;
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
