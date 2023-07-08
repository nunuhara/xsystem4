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

#include "system4/string.h"

#include "3d_internal.h"
#include "cJSON.h"
#include "json.h"
#include "reign.h"
#include "sprite.h"
#include "xsystem4.h"

#define SPREAD_VEC3(v) (v)[0], (v)[1], (v)[2]

static const char *instance_type_name(enum RE_instance_type type)
{
	switch (type) {
	case RE_ITYPE_UNINITIALIZED:     return "UNINITIALIZED";
	case RE_ITYPE_STATIC:            return "STATIC";
	case RE_ITYPE_SKINNED:           return "SKINNED";
	case RE_ITYPE_BILLBOARD:         return "BILLBOARD";
	case RE_ITYPE_DIRECTIONAL_LIGHT: return "DIRECTIONAL_LIGHT";
	case RE_ITYPE_SPECULAR_LIGHT:    return "SPECULAR_LIGHT";
	case RE_ITYPE_PARTICLE_EFFECT:   return "PARTICLE_EFFECT";
	case RE_ITYPE_PATH_LINE:         return "PATH_LINE";
	}
	return "UNKNOWN";
}

static const char *motion_state_name(enum RE_motion_state state)
{
	switch (state) {
	case RE_MOTION_STATE_STOP:   return "STOP";
	case RE_MOTION_STATE_NOLOOP: return "NOLOOP";
	case RE_MOTION_STATE_LOOP:   return "LOOP";
	}
	return "UNKNOWN";
}

static const char *fog_type_name(enum RE_fog_type type)
{
	switch (type) {
	case RE_FOG_NONE:             return "NONE";
	case RE_FOG_LINEAR:           return "LINEAR";
	case RE_FOG_LIGHT_SCATTERING: return "LIGHT_SCATTERING";
	}
	return "UNKNOWN";
}

static cJSON *motion_to_json(struct motion *m)
{
	if (m->instance->type == RE_ITYPE_BILLBOARD) {
		cJSON *obj = cJSON_CreateObject();
		cJSON_AddStringToObject(obj, "state", motion_state_name(m->state));
		cJSON_AddNumberToObject(obj, "frame", m->current_frame);
		cJSON_AddItemToObjectCS(obj, "range", num2_to_json(m->frame_begin, m->frame_end));
		cJSON_AddItemToObjectCS(obj, "loop_range", num2_to_json(m->loop_frame_begin, m->loop_frame_end));
		return obj;
	}
	if (m->mot) {
		cJSON *obj = cJSON_CreateObject();
		cJSON_AddStringToObject(obj, "name", m->mot->name);
		cJSON_AddStringToObject(obj, "state", motion_state_name(m->state));
		cJSON_AddNumberToObject(obj, "frame", m->current_frame);
		return obj;
	}
	return cJSON_CreateNull();
}

static cJSON *instance_to_json(struct RE_instance *inst, int index, bool verbose)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "index", index);
	cJSON_AddStringToObject(obj, "type", instance_type_name(inst->type));
	if (inst->type == RE_ITYPE_DIRECTIONAL_LIGHT || inst->type == RE_ITYPE_SPECULAR_LIGHT) {
		cJSON_AddItemToObjectCS(obj, "vec", vec3_point_to_json(inst->vec, verbose));
		cJSON_AddItemToObjectCS(obj, "diffuse", vec3_color_to_json(inst->diffuse, verbose));
		cJSON_AddItemToObjectCS(obj, "globe_diffuse", vec3_color_to_json(inst->globe_diffuse, verbose));
	} else {
		cJSON_AddNumberToObject(obj, "draw", inst->draw);
		cJSON_AddItemToObjectCS(obj, "pos", vec3_point_to_json(inst->pos, verbose));
		cJSON_AddItemToObjectCS(obj, "scale", vec3_point_to_json(inst->scale, verbose));
		cJSON_AddItemToObjectCS(obj, "ambient", vec3_color_to_json(inst->ambient, verbose));
	}
	if (inst->model) {
		cJSON *tmp;
		cJSON_AddStringToObject(obj, "path", inst->model->path);
		cJSON_AddItemToObjectCS(obj, "aabb", tmp = cJSON_CreateObject());
		cJSON_AddItemToObjectCS(tmp, "min", vec3_point_to_json(inst->model->aabb[0], verbose));
		cJSON_AddItemToObjectCS(tmp, "max", vec3_point_to_json(inst->model->aabb[1], verbose));
	}
	if (inst->motion) {
		cJSON_AddNumberToObject(obj, "fps", inst->fps);
		cJSON_AddItemToObjectCS(obj, "motion", motion_to_json(inst->motion));
	}
	if (inst->next_motion) {
		cJSON_AddItemToObjectCS(obj, "next_motion", motion_to_json(inst->next_motion));
	}
	if (inst->motion_blend) {
		cJSON_AddNumberToObject(obj, "motion_blend_rate", inst->motion_blend_rate);
	}
	if (inst->effect) {
		cJSON_AddStringToObject(obj, "path", inst->effect->pae->path);
	}

	return obj;
}

static cJSON *back_cg_to_json(struct RE_back_cg *bcg, int index, bool verbose)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "index", index);
	if (bcg->no) {
		cJSON_AddNumberToObject(obj, "no", bcg->no);
	}
	if (bcg->name) {
		cJSON_AddStringToObject(obj, "name", bcg->name->text);
	}
	if (!verbose) {
		cJSON_AddItemToObjectCS(obj, "pos", num2_to_json(bcg->x, bcg->y));
	} else {
		cJSON *pos;
		cJSON_AddItemToObjectCS(obj, "pos", pos = cJSON_CreateObject());
		cJSON_AddNumberToObject(pos, "x", bcg->x);
		cJSON_AddNumberToObject(pos, "y", bcg->y);
	}
	cJSON_AddNumberToObject(obj, "blend_rate", bcg->blend_rate);
	cJSON_AddNumberToObject(obj, "mag", bcg->mag);
	cJSON_AddNumberToObject(obj, "show", bcg->show);
	return obj;
}

cJSON *RE_to_json(struct sact_sprite *sp, bool verbose)
{
	struct RE_plugin *p = (struct RE_plugin*)sp->plugin;
	cJSON *obj, *cam, *a;

	obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", p->plugin.name);
	cJSON_AddItemToObjectCS(obj, "camera", cam = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(cam, "pos", vec3_point_to_json(p->camera.pos, verbose));
	cJSON_AddNumberToObject(cam, "pitch", p->camera.pitch);
	cJSON_AddNumberToObject(cam, "roll", p->camera.roll);
	cJSON_AddNumberToObject(cam, "yaw", p->camera.yaw);
	cJSON_AddItemToObjectCS(obj, "shadow_map_light_dir",
			vec3_point_to_json(p->shadow_map_light_dir, verbose));
	cJSON_AddNumberToObject(obj, "shadow_bias", p->shadow_bias);
	cJSON_AddStringToObject(obj, "fog_type", fog_type_name(p->fog_type));
	switch (p->fog_type) {
	case RE_FOG_NONE:
		break;
	case RE_FOG_LINEAR:
		cJSON_AddNumberToObject(obj, "fog_near", p->fog_near);
		cJSON_AddNumberToObject(obj, "fog_far", p->fog_far);
		break;
	case RE_FOG_LIGHT_SCATTERING:
		cJSON_AddNumberToObject(obj, "ls_beta_r", p->ls_beta_r);
		cJSON_AddNumberToObject(obj, "ls_beta_m", p->ls_beta_m);
		cJSON_AddNumberToObject(obj, "ls_g", p->ls_g);
		cJSON_AddNumberToObject(obj, "ls_distance", p->ls_distance);
		cJSON_AddItemToObjectCS(obj, "ls_light_dir",
				vec3_point_to_json(p->ls_light_dir, verbose));
		cJSON_AddItemToObjectCS(obj, "ls_light_color",
				vec3_color_to_json(p->ls_light_color, verbose));
		cJSON_AddItemToObjectCS(obj, "ls_sun_color",
				vec3_color_to_json(p->ls_sun_color, verbose));
		break;
	}

	cJSON_AddItemToObjectCS(obj, "instances", a = cJSON_CreateArray());
	for (int i = 0; i < p->nr_instances; i++) {
		if (!p->instances[i])
			continue;
		cJSON_AddItemToArray(a, instance_to_json(p->instances[i], i, verbose));
		
	}

	cJSON_AddItemToObjectCS(obj, "back_cgs", a = cJSON_CreateArray());
	for (int i = 0; i < RE_NR_BACK_CGS; i++) {
		struct RE_back_cg *bcg = &p->back_cg[i];
		if (!bcg->name && !bcg->no)
			continue;
		cJSON_AddItemToArray(a, back_cg_to_json(bcg, i, verbose));
	}

	return obj;
}
