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

static void print_motion(const char *name, struct motion *m, int indent)
{
	indent_printf(indent, "%s = {name=\"%s\", state=%s, frame=%f},\n",
	              name, m->name, motion_state_name(m->state), m->current_frame);
}

static void print_instance(struct RE_instance *inst, int index, int indent)
{
	if (!inst)
		return;
	indent_printf(indent, "instance[%d] = {\n", index);
	indent++;

	indent_printf(indent, "type = %s,\n", instance_type_name(inst->type));
	indent_printf(indent, "draw = %d,\n", inst->draw);
	indent_printf(indent, "pos = {x=%f, y=%f, z=%f},\n", inst->pos[0], inst->pos[1], inst->pos[2]);
	indent_printf(indent, "vec = {x=%f, y=%f, z=%f},\n", inst->vec[0], inst->vec[1], inst->vec[2]);
	indent_printf(indent, "scale = {x=%f, y=%f, z=%f},\n", inst->scale[0], inst->scale[1], inst->scale[2]);
	if (inst->model)
		indent_printf(indent, "path = \"%s\",\n", inst->model->path);
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
	              p->camera.pos[0], p->camera.pos[1], p->camera.pos[2],
	              p->camera.pitch, p->camera.roll, p->camera.yaw);

	for (int i = 0; i < RE_MAX_INSTANCES; i++)
		print_instance(p->instances[i], i, indent);

	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		print_back_cg(&p->back_cg[i], i, indent);
}
