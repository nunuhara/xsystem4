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

#define RE_MAX_INSTANCES 32

#include <cglm/types.h>

#include "plugin.h"

struct RE_options {
	int anti_aliasing;
	int wait_vsync;
};
extern struct RE_options RE_options;

struct RE_camera {
	vec3 pos;
	float pitch, roll, yaw;  // in degrees
};

struct RE_plugin {
	struct draw_plugin plugin;
	int sprite;
	struct RE_instance *instances[RE_MAX_INSTANCES];
	struct archive *aar;
	struct RE_renderer *renderer;
	struct RE_camera camera;
	mat4 proj_transform;

	// Settings.
	int render_mode;
	int shadow_mode;
	int bump_mode;
	int bloom_mode;
	int glare_mode;
	int fog_mode;
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
	float global_ambient_r;
	float global_ambient_g;
	float global_ambient_b;
	float post_effect_filter_y;
	float post_effect_filter_cb;
	float post_effect_filter_cr;
};

struct RE_instance {
	struct RE_plugin *plugin;
	struct model *model;
	int type;
	vec3 pos;
	float pitch, roll, yaw;  // in degrees
	vec3 vec;
	vec3 scale;
	bool draw;
	bool local_transform_needs_update;
	mat4 local_transform;
};

struct RE_plugin *RE_plugin_new(void);
void RE_plugin_free(struct RE_plugin *plugin);
bool RE_plugin_bind(struct RE_plugin *plugin, int sprite);
bool RE_plugin_unbind(struct RE_plugin *plugin);
bool RE_set_projection(struct RE_plugin *plugin, float width, float height, float near, float far, float deg);
int RE_create_instance(struct RE_plugin *plugin);
bool RE_release_instance(struct RE_plugin *plugin, int instance);

bool RE_instance_load(struct RE_instance *instance, const char *name);

void RE_render(struct sact_sprite *sp);
void RE_debug_print(struct RE_plugin *p);

#endif /* SYSTEM4_REIGN_H */
