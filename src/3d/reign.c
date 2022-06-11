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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/aar.h"

#include "3d_internal.h"
#include "reign.h"
#include "sact.h"
#include "xsystem4.h"

static struct RE_instance *create_instance(struct RE_plugin *plugin)
{
	struct RE_instance *instance = xcalloc(1, sizeof(struct RE_instance));
	instance->plugin = plugin;
	instance->scale[0] = 1.0;
	instance->scale[1] = 1.0;
	instance->scale[2] = 1.0;
	instance->fps = 30.0;
	glm_mat4_identity(instance->local_transform);
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
	plugin->aar = aar;
	return plugin;
}

void RE_plugin_free(struct RE_plugin *plugin)
{
	for (int i = 0; i < RE_MAX_INSTANCES; i++) {
		if (plugin->instances[i])
			free_instance(plugin->instances[i]);
	}
	archive_free(plugin->aar);
	if (plugin->renderer)
		RE_renderer_free(plugin->renderer);
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
	for (int i = 0; i < RE_MAX_INSTANCES; i++) {
		if (!plugin->instances[i]) {
			plugin->instances[i] = create_instance(plugin);
			return i;
		}
	}
	WARNING("too many instances");
	return -1;
}

bool RE_release_instance(struct RE_plugin *plugin, int instance)
{
	if (!plugin || !plugin->instances[instance])
		return false;
	free_instance(plugin->instances[instance]);
	plugin->instances[instance] = NULL;
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
	if (!instance || !instance->model)
		return false;
	instance->motion = motion_load(name, instance->model, instance->plugin->aar);
	return !!instance->motion;
}

bool RE_instance_load_next_motion(struct RE_instance *instance, const char *name)
{
	if (!instance || !instance->model)
		return false;
	instance->next_motion = motion_load(name, instance->model, instance->plugin->aar);
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
	return true;
}

bool RE_motion_set_loop_frame_range(struct motion *motion, float begin, float end)
{
	if (!motion)
		return false;
	motion->loop_frame_begin = begin;
	motion->loop_frame_end = end;
	return true;
}

static void print_motion(const char *name, struct motion *m, int indent)
{
	const char *state;
	switch (m->state) {
	case RE_MOTION_STATE_STOP: state = "STOP"; break;
	case RE_MOTION_STATE_NOLOOP: state = "NOLOOP"; break;
	case RE_MOTION_STATE_LOOP: state = "LOOP"; break;
	default: state = "?"; break;
	}
	indent_printf(indent, "%s = {name=\"%s\", state=%s, frame=%f},\n",
	              name, m->name, state, m->current_frame);
}

void RE_debug_print(struct RE_plugin *p)
{
	int indent = 0;
	indent_printf(indent, "camera = {x=%f, y=%f, z=%f, pitch=%f, roll=%f, yaw=%f},\n",
	              p->camera.pos[0], p->camera.pos[1], p->camera.pos[2],
	              p->camera.pitch, p->camera.roll, p->camera.yaw);

	for (int i = 0; i < RE_MAX_INSTANCES; i++) {
		struct RE_instance *inst = p->instances[i];
		if (!inst || !inst->model)
			continue;
		indent_printf(indent, "instance[%d] = {\n", i);
		indent++;

		indent_printf(indent, "path = \"%s\",\n", inst->model->path);
		indent_printf(indent, "type = %d,\n", inst->type);
		indent_printf(indent, "draw = %d,\n", inst->draw);
		indent_printf(indent, "pos = {x=%f, y=%f, z=%f},\n", inst->pos[0], inst->pos[1], inst->pos[2]);
		indent_printf(indent, "vec = {x=%f, y=%f, z=%f},\n", inst->vec[0], inst->vec[1], inst->vec[2]);
		indent_printf(indent, "scale = {x=%f, y=%f, z=%f},\n", inst->scale[0], inst->scale[1], inst->scale[2]);
		if (inst->motion)
			print_motion("motion", inst->motion, indent);
		if (inst->next_motion)
			print_motion("next_motion", inst->next_motion, indent);
		indent_printf(indent, "fps = %f,\n", inst->fps);
		if (inst->motion_blend)
			indent_printf(indent, "motion_blend_rate = %f,\n", inst->motion_blend_rate);

		indent--;
		indent_printf(indent, "},\n");
	}
}
