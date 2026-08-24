/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include <cglm/cglm.h>

#include "system4.h"
#include "system4/string.h"

#include "3d_internal.h"
#include "reign.h"
#include "../hll/iarray.h"

static void write_vec3(struct iarray_writer *w, vec3 v)
{
	for (int i = 0; i < 3; i++)
		iarray_write_float(w, v[i]);
}

static void read_vec3(struct iarray_reader *r, vec3 v)
{
	for (int i = 0; i < 3; i++)
		v[i] = iarray_read_float(r);
}

static void write_cstring(struct iarray_writer *w, const char *s)
{
	struct string *tmp = cstr_to_string(s);
	iarray_write_string(w, tmp);
	free_string(tmp);
}

static void save_motion(struct iarray_writer *w, struct motion *m)
{
	iarray_write(w, !!m);
	if (!m)
		return;
	write_cstring(w, m->mot ? m->mot->name : "");
	iarray_write(w, m->state);
	iarray_write_float(w, m->current_frame);
	iarray_write_float(w, m->frame_begin);
	iarray_write_float(w, m->frame_end);
	iarray_write_float(w, m->loop_frame_begin);
	iarray_write_float(w, m->loop_frame_end);
}

static void load_motion(struct iarray_reader *r, struct RE_instance *inst, bool next)
{
	if (!iarray_read(r))
		return;
	struct string *name = iarray_read_string(r);
	if (name->size > 0) {
		if (next)
			RE_instance_load_next_motion(inst, name->text);
		else
			RE_instance_load_motion(inst, name->text);
	}
	free_string(name);

	int state = iarray_read(r);
	float current_frame = iarray_read_float(r);
	float frame_begin = iarray_read_float(r);
	float frame_end = iarray_read_float(r);
	float loop_frame_begin = iarray_read_float(r);
	float loop_frame_end = iarray_read_float(r);

	struct motion *m = next ? inst->next_motion : inst->motion;
	if (!m)
		return;
	m->state = state;
	m->current_frame = current_frame;
	m->frame_begin = frame_begin;
	m->frame_end = frame_end;
	m->loop_frame_begin = loop_frame_begin;
	m->loop_frame_end = loop_frame_end;
}

static void save_instance(struct iarray_writer *w, struct RE_instance *inst)
{
	iarray_write(w, inst->type);
	iarray_write_string_or_null(w, inst->name);
	for (int i = 0; i < RE_NR_INSTANCE_TARGETS; i++)
		iarray_write(w, inst->target[i]);
	write_vec3(w, inst->pos);
	iarray_write_float(w, inst->pitch);
	iarray_write_float(w, inst->roll);
	iarray_write_float(w, inst->yaw);
	write_vec3(w, inst->scale);
	iarray_write_float(w, inst->alpha);
	iarray_write_float(w, inst->grayscale_rate);
	iarray_write(w, inst->draw);
	iarray_write(w, inst->draw_edge);
	iarray_write(w, inst->draw_shadow);
	iarray_write(w, inst->make_shadow);
	iarray_write(w, inst->draw_bump);
	iarray_write_float(w, inst->shadow_volume_bone_radius);
	iarray_write_float(w, inst->fps);
	iarray_write(w, inst->use_mag_speed);
	iarray_write(w, inst->motion_blend);
	iarray_write_float(w, inst->motion_blend_rate);
	write_vec3(w, inst->diffuse);
	write_vec3(w, inst->ambient);
	write_vec3(w, inst->column_pos);
	iarray_write_float(w, inst->column_height);
	iarray_write_float(w, inst->column_radius);
	iarray_write_float(w, inst->column_angle);

	iarray_write(w, inst->draw_type);
	for (int i = 0; i < 4; i++)
		write_vec3(w, inst->vertex_pos[i]);
	for (int i = 0; i < 4; i++) {
		iarray_write_float(w, inst->vertex_uv[i][0]);
		iarray_write_float(w, inst->vertex_uv[i][1]);
	}

	write_vec3(w, inst->vec);
	write_vec3(w, inst->globe_diffuse);
	iarray_write(w, !!inst->light_params);
	if (inst->light_params) {
		iarray_write(w, RE_NR_LIGHT_PARAMS);
		for (int i = 0; i < RE_NR_LIGHT_PARAMS; i++)
			iarray_write_float(w, inst->light_params[i]);
	}

	save_motion(w, inst->motion);
	save_motion(w, inst->next_motion);
}

static void load_instance(struct iarray_reader *r, struct RE_instance *inst)
{
	int type = iarray_read(r);
	if (type != RE_ITYPE_UNINITIALIZED)
		RE_instance_set_type(inst, type);
	struct string *name = iarray_read_string_or_null(r);
	if (name) {
		RE_instance_load(inst, name->text);
		free_string(name);
	}
	for (int i = 0; i < RE_NR_INSTANCE_TARGETS; i++)
		inst->target[i] = iarray_read(r);
	read_vec3(r, inst->pos);
	inst->pitch = iarray_read_float(r);
	inst->roll = iarray_read_float(r);
	inst->yaw = iarray_read_float(r);
	read_vec3(r, inst->scale);
	inst->alpha = iarray_read_float(r);
	inst->grayscale_rate = iarray_read_float(r);
	inst->draw = iarray_read(r);
	inst->draw_edge = iarray_read(r);
	inst->draw_shadow = iarray_read(r);
	inst->make_shadow = iarray_read(r);
	inst->draw_bump = iarray_read(r);
	inst->shadow_volume_bone_radius = iarray_read_float(r);
	inst->fps = iarray_read_float(r);
	inst->use_mag_speed = iarray_read(r);
	inst->motion_blend = iarray_read(r);
	inst->motion_blend_rate = iarray_read_float(r);
	read_vec3(r, inst->diffuse);
	read_vec3(r, inst->ambient);
	read_vec3(r, inst->column_pos);
	inst->column_height = iarray_read_float(r);
	inst->column_radius = iarray_read_float(r);
	inst->column_angle = iarray_read_float(r);
	inst->local_transform_needs_update = true;

	inst->draw_type = iarray_read(r);
	for (int i = 0; i < 4; i++)
		read_vec3(r, inst->vertex_pos[i]);
	for (int i = 0; i < 4; i++) {
		inst->vertex_uv[i][0] = iarray_read_float(r);
		inst->vertex_uv[i][1] = iarray_read_float(r);
	}

	read_vec3(r, inst->vec);
	read_vec3(r, inst->globe_diffuse);
	if (iarray_read(r)) {
		int nr_params = iarray_read(r);
		for (int i = 0; i < nr_params; i++) {
			float param = iarray_read_float(r);
			if (inst->light_params && i < RE_NR_LIGHT_PARAMS)
				inst->light_params[i] = param;
		}
	}

	load_motion(r, inst, false);
	load_motion(r, inst, true);
}

static void save_back_cg(struct iarray_writer *w, struct RE_back_cg *bcg)
{
	iarray_write_string_or_null(w, bcg->name);
	iarray_write(w, bcg->no);
	iarray_write_float(w, bcg->blend_rate);
	iarray_write_float(w, bcg->x);
	iarray_write_float(w, bcg->y);
	iarray_write_float(w, bcg->mag);
	iarray_write(w, bcg->show);
}

static void load_back_cg(struct iarray_reader *r, struct RE_back_cg *bcg, struct archive *aar)
{
	struct string *name = iarray_read_string_or_null(r);
	int no = iarray_read(r);
	float blend_rate = iarray_read_float(r);
	float x = iarray_read_float(r);
	float y = iarray_read_float(r);
	float mag = iarray_read_float(r);
	bool show = iarray_read(r);
	if (bcg) {
		if (name)
			RE_back_cg_set_name(bcg, name, aar);
		else if (no)
			RE_back_cg_set(bcg, no);
		bcg->blend_rate = blend_rate;
		bcg->x = x;
		bcg->y = y;
		bcg->mag = mag;
		bcg->show = show;
	}
	if (name)
		free_string(name);
}

void RE_plugin_serialize(struct RE_plugin *plugin, struct iarray_writer *w)
{
	write_vec3(w, plugin->camera.pos);
	iarray_write_float(w, plugin->camera.pitch);
	iarray_write_float(w, plugin->camera.roll);
	iarray_write_float(w, plugin->camera.yaw);
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++)
			iarray_write_float(w, plugin->proj_transform[i][j]);
	}
	iarray_write(w, plugin->viewport_x);
	iarray_write(w, plugin->viewport_y);
	iarray_write(w, plugin->viewport_width);
	iarray_write(w, plugin->viewport_height);

	iarray_write(w, RE_NR_BACK_CGS);
	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		save_back_cg(w, &plugin->back_cg[i]);

	iarray_write(w, plugin->render_mode);
	iarray_write(w, plugin->shadow_mode);
	write_vec3(w, plugin->shadow_map_light_dir);
	iarray_write_float(w, plugin->shadow_bias);
	iarray_write(w, plugin->bump_mode);
	iarray_write(w, plugin->bloom_mode);
	iarray_write(w, plugin->glare_mode);
	iarray_write(w, plugin->fog_mode);
	iarray_write(w, plugin->fog_type);
	iarray_write_float(w, plugin->fog_near);
	iarray_write_float(w, plugin->fog_far);
	write_vec3(w, plugin->fog_color);
	iarray_write_float(w, plugin->ls_beta_r);
	iarray_write_float(w, plugin->ls_beta_m);
	iarray_write_float(w, plugin->ls_g);
	iarray_write_float(w, plugin->ls_distance);
	write_vec3(w, plugin->ls_light_dir);
	write_vec3(w, plugin->ls_light_color);
	write_vec3(w, plugin->ls_sun_color);
	iarray_write(w, plugin->specular_mode);
	iarray_write(w, plugin->light_map_mode);
	iarray_write(w, plugin->soft_fog_edge_mode);
	iarray_write(w, plugin->ssao_mode);
	iarray_write(w, plugin->vertex_blend_mode);
	iarray_write(w, plugin->shader_precision_mode);
	iarray_write(w, plugin->texture_filter_mode);
	iarray_write(w, plugin->use_power2_texture);
	iarray_write(w, plugin->shadow_map_resolution_level);
	iarray_write(w, plugin->texture_resolution_level);
	write_vec3(w, plugin->global_ambient);
	iarray_write_float(w, plugin->post_effect_filter_y);
	iarray_write_float(w, plugin->post_effect_filter_cb);
	iarray_write_float(w, plugin->post_effect_filter_cr);
	iarray_write(w, RE_DRAW_OPTION_MAX);
	for (int i = 0; i < RE_DRAW_OPTION_MAX; i++)
		iarray_write(w, plugin->draw_options[i]);
	iarray_write(w, plugin->suspended);

	iarray_write(w, plugin->mag_speed);
	iarray_write(w, RE_NR_LIGHT_PARAMS);
	for (int i = 0; i < RE_NR_LIGHT_PARAMS; i++)
		iarray_write_float(w, plugin->light_params[i]);
	iarray_write_float(w, plugin->edge_length);
	iarray_write_float(w, plugin->edge_reduction_rate);
	write_vec3(w, plugin->edge_color);

	iarray_write(w, plugin->nr_instances);
	for (int i = 0; i < plugin->nr_instances; i++) {
		iarray_write(w, !!plugin->instances[i]);
		if (plugin->instances[i])
			save_instance(w, plugin->instances[i]);
	}
}

void RE_plugin_deserialize(struct RE_plugin *plugin, struct iarray_reader *r, int version)
{
	read_vec3(r, plugin->camera.pos);
	plugin->camera.pitch = iarray_read_float(r);
	plugin->camera.roll = iarray_read_float(r);
	plugin->camera.yaw = iarray_read_float(r);
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++)
			plugin->proj_transform[i][j] = iarray_read_float(r);
	}
	int viewport_x = iarray_read(r);
	int viewport_y = iarray_read(r);
	int viewport_width = iarray_read(r);
	int viewport_height = iarray_read(r);
	if (viewport_width > 0 && viewport_height > 0)
		RE_set_viewport(plugin, viewport_x, viewport_y, viewport_width, viewport_height);

	int nr_back_cgs = iarray_read(r);
	for (int i = 0; i < nr_back_cgs; i++)
		load_back_cg(r, i < RE_NR_BACK_CGS ? &plugin->back_cg[i] : NULL, plugin->aar);

	plugin->render_mode = iarray_read(r);
	plugin->shadow_mode = iarray_read(r);
	read_vec3(r, plugin->shadow_map_light_dir);
	plugin->shadow_bias = iarray_read_float(r);
	plugin->bump_mode = iarray_read(r);
	plugin->bloom_mode = iarray_read(r);
	plugin->glare_mode = iarray_read(r);
	plugin->fog_mode = iarray_read(r);
	plugin->fog_type = iarray_read(r);
	plugin->fog_near = iarray_read_float(r);
	plugin->fog_far = iarray_read_float(r);
	read_vec3(r, plugin->fog_color);
	plugin->ls_beta_r = iarray_read_float(r);
	plugin->ls_beta_m = iarray_read_float(r);
	plugin->ls_g = iarray_read_float(r);
	plugin->ls_distance = iarray_read_float(r);
	read_vec3(r, plugin->ls_light_dir);
	read_vec3(r, plugin->ls_light_color);
	read_vec3(r, plugin->ls_sun_color);
	plugin->specular_mode = iarray_read(r);
	plugin->light_map_mode = iarray_read(r);
	plugin->soft_fog_edge_mode = iarray_read(r);
	plugin->ssao_mode = iarray_read(r);
	plugin->vertex_blend_mode = iarray_read(r);
	plugin->shader_precision_mode = iarray_read(r);
	plugin->texture_filter_mode = iarray_read(r);
	plugin->use_power2_texture = iarray_read(r);
	plugin->shadow_map_resolution_level = iarray_read(r);
	plugin->texture_resolution_level = iarray_read(r);
	read_vec3(r, plugin->global_ambient);
	plugin->post_effect_filter_y = iarray_read_float(r);
	plugin->post_effect_filter_cb = iarray_read_float(r);
	plugin->post_effect_filter_cr = iarray_read_float(r);
	int nr_draw_options = iarray_read(r);
	for (int i = 0; i < nr_draw_options; i++) {
		int option = iarray_read(r);
		if (i < RE_DRAW_OPTION_MAX)
			plugin->draw_options[i] = option;
	}
	plugin->suspended = iarray_read(r);

	plugin->mag_speed = iarray_read(r);
	int nr_light_params = iarray_read(r);
	for (int i = 0; i < nr_light_params; i++) {
		float param = iarray_read_float(r);
		if (i < RE_NR_LIGHT_PARAMS)
			plugin->light_params[i] = param;
	}
	plugin->edge_length = iarray_read_float(r);
	plugin->edge_reduction_rate = iarray_read_float(r);
	read_vec3(r, plugin->edge_color);

	int nr_instances = iarray_read(r);
	for (int i = 0; i < nr_instances; i++) {
		if (!iarray_read(r))
			continue;
		load_instance(r, RE_create_instance_at(plugin, i));
	}
}
