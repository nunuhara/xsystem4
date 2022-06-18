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

static void print_instance(struct RE_instance *inst, int index, int indent)
{
	if (!inst || !inst->model)
		return;
	indent_printf(indent, "instance[%d] = {\n", index);
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
