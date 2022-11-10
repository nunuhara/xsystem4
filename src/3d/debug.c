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

static void print_motion(const char *name, struct motion *m, int indent)
{
	if (m->instance->type == RE_ITYPE_BILLBOARD) {
		indent_printf(indent, "%s = {state=%s, frame=%f, range=(%d,%d), loop_range=(%d,%d)},\n",
		              name, motion_state_name(m->state), m->current_frame,
		              (int)m->frame_begin, (int)m->frame_end, (int)m->loop_frame_begin, (int)m->loop_frame_end);
	} else {
		indent_printf(indent, "%s = {name=\"%s\", state=%s, frame=%f},\n",
		              name, m->name, motion_state_name(m->state), m->current_frame);
	}
}

static void print_instance(struct RE_instance *inst, int index, int indent)
{
	if (!inst)
		return;
	indent_printf(indent, "instance[%d] = {\n", index);
	indent++;

	indent_printf(indent, "type = %s,\n", instance_type_name(inst->type));
	if (inst->type == RE_ITYPE_DIRECTIONAL_LIGHT || inst->type == RE_ITYPE_SPECULAR_LIGHT) {
		indent_printf(indent, "vec = {x=%f, y=%f, z=%f},\n", SPREAD_VEC3(inst->vec));
		indent_printf(indent, "diffuse = {r=%f, g=%f, b=%f},\n", SPREAD_VEC3(inst->diffuse));
		indent_printf(indent, "globe_diffuse = {r=%f, g=%f, b=%f},\n", SPREAD_VEC3(inst->globe_diffuse));
	} else {
		indent_printf(indent, "draw = %d,\n", inst->draw);
		indent_printf(indent, "pos = {x=%f, y=%f, z=%f},\n", SPREAD_VEC3(inst->pos));
		indent_printf(indent, "scale = {x=%f, y=%f, z=%f},\n", SPREAD_VEC3(inst->scale));
		indent_printf(indent, "ambient = {r=%f, g=%f, b=%f},\n", SPREAD_VEC3(inst->ambient));
	}
	if (inst->model)
		indent_printf(indent, "path = \"%s\",\n", inst->model->path);
	if (inst->motion) {
		indent_printf(indent, "fps = %f,\n", inst->fps);
		print_motion("motion", inst->motion, indent);
	}
	if (inst->next_motion)
		print_motion("next_motion", inst->next_motion, indent);
	if (inst->motion_blend)
		indent_printf(indent, "motion_blend_rate = %f,\n", inst->motion_blend_rate);
	if (inst->effect)
		indent_printf(indent, "path = \"%s\",\n", inst->effect->path);
	indent_printf(indent, "is_transparent = %d,\n", inst->is_transparent);

	indent--;
	indent_printf(indent, "},\n");
}

static void print_back_cg(struct RE_back_cg *bcg, int index, int indent)
{
	if (!bcg->name && !bcg->no)
		return;
	indent_printf(indent, "backCG[%d] = {\n", index);
	indent++;

	if (bcg->no)
		indent_printf(indent, "no = %d,\n", bcg->no);
	if (bcg->name)
		indent_printf(indent, "name = \"%s\",\n", bcg->name ? bcg->name->text : "");
	indent_printf(indent, "pos = {x=%f, y=%f},\n", bcg->x, bcg->y);
	indent_printf(indent, "blend_rate = %f,\n", bcg->blend_rate);
	indent_printf(indent, "mag = %f,\n", bcg->mag);
	indent_printf(indent, "show = %d,\n", bcg->show);

	indent--;
	indent_printf(indent, "},\n");
}

void RE_debug_print(struct sact_sprite *sp, int indent)
{
	struct RE_plugin *p = (struct RE_plugin *)sp->plugin;

	indent_printf(indent, "name = \"%s\",\n", p->plugin.name);
	indent_printf(indent, "camera = {x=%f, y=%f, z=%f, pitch=%f, roll=%f, yaw=%f},\n",
	              SPREAD_VEC3(p->camera.pos), p->camera.pitch, p->camera.roll, p->camera.yaw);
	indent_printf(indent, "shadow_map_light_dir = {x=%f, y=%f, z=%f},\n", SPREAD_VEC3(p->shadow_map_light_dir));
	indent_printf(indent, "shadow_bias = %f,\n", p->shadow_bias);

	indent_printf(indent, "fog_type = %s,\n", fog_type_name(p->fog_type));
	switch (p->fog_type) {
	case RE_FOG_NONE:
		break;
	case RE_FOG_LINEAR:
		indent_printf(indent, "fog_near = %f, fog_far = %f,\n", p->fog_near, p->fog_far);
		indent_printf(indent, "fog_color = {r=%f, g=%f, b=%f},\n", SPREAD_VEC3(p->fog_color));
		break;
	case RE_FOG_LIGHT_SCATTERING:
		indent_printf(indent, "ls_beta_r = %f, ls_beta_m = %f,\n", p->ls_beta_r, p->ls_beta_m);
		indent_printf(indent, "ls_g = %f,\n", p->ls_g);
		indent_printf(indent, "ls_distance = %f,\n", p->ls_distance);
		indent_printf(indent, "ls_light_dir = {x=%f, y=%f, z=%f},\n", SPREAD_VEC3(p->ls_light_dir));
		indent_printf(indent, "ls_light_color = {r=%f, g=%f, b=%f},\n", SPREAD_VEC3(p->ls_light_color));
		indent_printf(indent, "ls_sun_color = {r=%f, g=%f, b=%f},\n", SPREAD_VEC3(p->ls_sun_color));
		break;
	}

	for (int i = 0; i < p->nr_instances; i++)
		print_instance(p->instances[i], i, indent);

	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		print_back_cg(&p->back_cg[i], i, indent);
}
