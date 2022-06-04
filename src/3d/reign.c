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
	glm_mat4_identity(instance->local_transform);
	return instance;
}

static void free_instance(struct RE_instance *instance)
{
	if (instance->model)
		model_free(instance->model);
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
	if (instance->model)
		model_free(instance->model);
	instance->model = model_load(instance->plugin->aar, name, instance->plugin->renderer);
	return !!instance->model;
}

void RE_debug_print(struct RE_plugin *p)
{
	printf("camera = { x: %f, y: %f, z: %f, pitch: %f, roll: %f, yaw: %f }",
	       p->camera.pos[0], p->camera.pos[1], p->camera.pos[2],
	       p->camera.pitch, p->camera.roll, p->camera.yaw);

	for (int i = 0; i < RE_MAX_INSTANCES; i++) {
		struct RE_instance *inst = p->instances[i];
		if (!inst || !inst->model)
			continue;
		printf("[%d] = {", i);
		printf("  name: \"%s\",", inst->model->name);
		printf("  type: %d,", inst->type);
		printf("  draw: %d,", inst->draw);
		printf("  pos: { x: %f, y: %f, z: %f },", inst->pos[0], inst->pos[1], inst->pos[2]);
		printf("  vec: { x: %f, y: %f, z: %f },", inst->vec[0], inst->vec[1], inst->vec[2]);
		printf("  scale: { x: %f, y: %f, z: %f },", inst->scale[0], inst->scale[1], inst->scale[2]);
		printf("}");
	}
}
