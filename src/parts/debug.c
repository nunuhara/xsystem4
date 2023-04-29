/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "xsystem4.h"
#include "parts_internal.h"

static void parts_cg_print(struct parts_cg *cg, int indent)
{
	indent_message(indent, "cg.no = %d,\n", cg->no);
}

static void parts_text_print(struct parts_text *text, int indent)
{
	indent_message(indent, "text.lines = {\n");
	for (unsigned i = 0; i < text->nr_lines; i++) {
		struct string *s = parts_text_line_get(&text->lines[i]);
		indent_message(indent+1, "contents = \"%s\",\n", display_sjis0(s->text));
		indent_message(indent+1, "width = %u,\n", text->lines[i].width);
		indent_message(indent+1, "height = %u,\n", text->lines[i].height);
		free_string(s);
	}
	indent_message(indent, "},\n");
	indent_message(indent, "text.line_space = %u,\n", text->line_space);
	indent_message(indent, "text.cursor = ");
	gfx_print_point(&text->cursor);
	sys_message(",\n");
	indent_message(indent, "text.ts = ");
	gfx_print_text_style(&text->ts, indent);
	sys_message("\n");
}

static void parts_animation_print(struct parts_animation *anim, int indent)
{
	indent_message(indent, "anim.start_no = %u,\n", anim->start_no);
	indent_message(indent, "anim.frame_time = %u,\n", anim->frame_time);
	indent_message(indent, "anim.elapsed = %u,\n", anim->elapsed);
	indent_message(indent, "anim.current_frame = %u,\n", anim->current_frame);
	indent_message(indent, "anim.frames = {\n");
	for (unsigned i = 0; i < anim->nr_frames; i++) {
		indent_message(indent+1, "[%u] = ", i);
		gfx_print_texture(&anim->frames[i], indent+1);
		indent_message(indent+1, ",\n");
	}
	indent_message(indent, "}\n");
}

static void parts_numeral_print(struct parts_numeral *num, int indent)
{
	indent_message(indent, "num.have_num = %s,\n", num->have_num ? "true" : "false");
	indent_message(indent, "num.num = %d,\n", num->num);
	indent_message(indent, "num.space = %d,\n", num->space);
	indent_message(indent, "num.show_comma = %d,\n", num->show_comma);
	indent_message(indent, "num.length = %d,\n", num->length);
	indent_message(indent, "num.cg_no = %d,\n", num->cg_no);
	indent_message(indent, "num.cg = {\n");
	for (int i = 0; i < 12; i++) {
		indent_message(indent+1, "[%d] = ", i);
		gfx_print_texture(&num->cg[i], indent+1);
		indent_message(indent+1, ",\n");
	}
	indent_message(indent, "}\n");
}

static void parts_gauge_print(struct parts_gauge *gauge, int indent)
{
	indent_message(indent, "gauge.cg = ");
	gfx_print_texture(&gauge->cg, indent);
}

static void parts_construction_process_print(struct parts_construction_process *cproc, int indent)
{
	static const char *type_names[PARTS_NR_CP_TYPES] = {
		[PARTS_CP_CREATE] = "PARTS_CP_CREATE",
		[PARTS_CP_CREATE_PIXEL_ONLY] = "PARTS_CP_CREATE_PIXEL_ONLY",
		[PARTS_CP_CG] = "PARTS_CP_CG",
		[PARTS_CP_FILL] = "PARTS_CP_FILL",
		[PARTS_CP_FILL_ALPHA_COLOR] = "PARTS_CP_FILL_ALPHA_COLOR",
		[PARTS_CP_FILL_AMAP] = "PARTS_CP_FILL_AMAP",
		[PARTS_CP_DRAW_CUT_CG] = "PARTS_CP_DRAW_CUT_CG",
		[PARTS_CP_COPY_CUT_CG] = "PARTS_CP_COPY_CUT_CG",
		[PARTS_CP_DRAW_TEXT] = "PARTS_CP_DRAW_TEXT",
		[PARTS_CP_COPY_TEXT] = "PARTS_CP_COPY_TEXT",
	};

	indent_message(indent, "cproc.ops = {\n");
	indent++;

	int i = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		indent_message(indent, "[%d] = {\n", i++);
		indent++;

		const char *type = "INVALID_CONSTRUCTION_PROCERSS_TYPE";
		if (op->type >= 0 && op->type < PARTS_NR_CP_TYPES)
			type = type_names[op->type];
		indent_message(indent, "type = %s,\n", type);

		switch (op->type) {
		case PARTS_CP_CREATE:
		case PARTS_CP_CREATE_PIXEL_ONLY:
			indent_message(indent, "create = {w=%d,h=%d}\n", op->create.w, op->create.h);
			break;
		case PARTS_CP_CG:
			indent_message(indent, "cg.no = %d\n", op->cg.no);
			break;
		case PARTS_CP_FILL:
		case PARTS_CP_FILL_ALPHA_COLOR:
		case PARTS_CP_FILL_AMAP:
			indent_message(indent, "fill.rect = {x=%d,y=%d,w=%d,h=%d},\n",
					op->fill.x, op->fill.y, op->fill.w, op->fill.h);
			indent_message(indent, "fill.color = (%d,%d,%d,%d)\n",
					op->fill.r, op->fill.g, op->fill.b, op->fill.a);
			break;
		case PARTS_CP_DRAW_CUT_CG:
		case PARTS_CP_COPY_CUT_CG:
			indent_message(indent, "cut_cg.cg_no = %d,\n", op->cut_cg.cg_no);
			indent_message(indent, "cut_cg.dst = {x=%d,y=%d,w=%d,h=%d},\n",
					op->cut_cg.dx, op->cut_cg.dy, op->cut_cg.dw, op->cut_cg.dh);
			indent_message(indent, "cut_cg.src = {x=%d,y=%d,w=%d,h=%d},\n",
					op->cut_cg.sx, op->cut_cg.sy, op->cut_cg.sw, op->cut_cg.sh);
			indent_message(indent, "cut_cg.interp_type = %d\n", op->cut_cg.interp_type);
			break;
		case PARTS_CP_DRAW_TEXT:
		case PARTS_CP_COPY_TEXT:
			indent_message(indent, "text.text = \"%s\",\n", display_sjis0(op->text.text->text));
			indent_message(indent, "text.pos = {x=%d,y=%d},\n", op->text.x, op->text.y);
			indent_message(indent, "text.line_space = %d,\n", op->text.line_space);
			indent_message(indent, "text.style = ");
			gfx_print_text_style(&op->text.style, indent);
			sys_message("\n");
			break;
		}
		indent--;
		indent_message(indent, "},\n");
	}

	indent--;
	indent_message(indent, "}\n");
}

static void parts_print_state(struct parts_state *state, int indent)
{
	if (state->type == PARTS_UNINITIALIZED) {
		sys_message("UNINITIALIZED");
		return;
	}
	sys_message("{\n");
	indent++;

	struct parts_common *com = &state->common;

	indent_message(indent, "dims = {w=%d,h=%d},\n", state->common.w, state->common.h);
	indent_message(indent, "origin_offset = "); gfx_print_point(&com->origin_offset); sys_message(",\n");
	indent_message(indent, "hitbox = "); gfx_print_rectangle(&com->hitbox); sys_message(",\n");
	indent_message(indent, "surface_area = "); gfx_print_rectangle(&com->surface_area); sys_message(",\n");
	switch (state->type) {
	case PARTS_UNINITIALIZED:
		break;
	case PARTS_CG:
		parts_cg_print(&state->cg, indent);
		break;
	case PARTS_TEXT:
		parts_text_print(&state->text, indent);
		break;
	case PARTS_ANIMATION:
		parts_animation_print(&state->anim, indent);
		break;
	case PARTS_NUMERAL:
		parts_numeral_print(&state->num, indent);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		parts_gauge_print(&state->gauge, indent);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		parts_construction_process_print(&state->cproc, indent);
		break;
	}

	indent--;
	indent_message(indent, "}");
}

static void parts_print_motion_param(union parts_motion_param param, enum parts_motion_type type)
{
	switch (type) {
	case PARTS_MOTION_POS:
		sys_message("{x=%d,y=%d}", param.x, param.y);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		sys_message("{w=%d,h=%d}", param.x, param.y);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		sys_message("%d", param.i);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		sys_message("%f", param.f);
		break;
	}
}

static void parts_print_motion(struct parts_motion *motion, int indent)
{
	static const char *type_names[PARTS_NR_MOTION_TYPES] = {
		[PARTS_MOTION_POS] = "PARTS_MOTION_POS",
		[PARTS_MOTION_ALPHA] = "PARTS_MOTION_ALPHA",
		[PARTS_MOTION_CG] = "PARTS_MOTION_CG",
		[PARTS_MOTION_HGAUGE_RATE] = "PARTS_MOTION_HGAUGE_RATE",
		[PARTS_MOTION_VGAUGE_RATE] = "PARTS_MOTION_VGAUGE_RATE",
		[PARTS_MOTION_NUMERAL_NUMBER] = "PARTS_MOTION_NUMERAL_NUMBER",
		[PARTS_MOTION_MAG_X] = "PARTS_MOTION_MAG_X",
		[PARTS_MOTION_MAG_Y] = "PARTS_MOTION_MAG_Y",
		[PARTS_MOTION_ROTATE_X] = "PARTS_MOTION_ROTATE_X",
		[PARTS_MOTION_ROTATE_Y] = "PARTS_MOTION_ROTATE_Y",
		[PARTS_MOTION_ROTATE_Z] = "PARTS_MOTION_ROTATE_Z",
		[PARTS_MOTION_VIBRATION_SIZE] = "PARTS_MOTION_VIBRATION_SIZE"
	};

	const char *type = "INVALID_MOTION_TYPE";
	if (motion->type >= 0 && motion->type < PARTS_NR_MOTION_TYPES)
		type = type_names[motion->type];

	sys_message("{\n");
	indent++;

	indent_message(indent, "type = %s,\n", type);
	indent_message(indent, "begin = ");
	parts_print_motion_param(motion->begin, motion->type);
	sys_message(",\n");
	indent_message(indent, "end = ");
	parts_print_motion_param(motion->end, motion->type);
	sys_message(",\n");
	indent_message(indent, "begin_time = %d,\n", motion->begin_time);
	indent_message(indent, "end_time = %d,\n", motion->end_time);

	indent--;
	indent_message(indent, "}");
}

static void parts_print_params(struct parts_params *global, struct parts_params *local, int indent)
{
	sys_message("{\n");
	indent++;

	indent_message(indent, "z = %d (%d),\n", global->z, local->z);
	indent_message(indent, "pos = ");
	gfx_print_point(&global->pos);
	sys_message(" (");
	gfx_print_point(&local->pos);
	sys_message("),\n");
	indent_message(indent, "show = %s (%s),\n", global->show ? "true" : "false",
			local->show ? "true" : "false");
	indent_message(indent, "alpha = %u (%u),\n", (unsigned)global->alpha, (unsigned)local->alpha);
	indent_message(indent, "scale = {x=%f,y=%f} ({x=%f,y=%f}),\n", global->scale.x, global->scale.y,
			local->scale.x, local->scale.y);
	indent_message(indent, "rotation = {x=%f,y=%f,z=%f} ({x=%f,y=%f,z=%f}),\n",
			global->rotation.x, global->rotation.y, global->rotation.z,
			local->rotation.x, local->rotation.y, local->rotation.z);
	indent_message(indent, "add_color = ");
	gfx_print_color(&global->add_color);
	sys_message(" (");
	gfx_print_color(&local->add_color);
	sys_message("),\n");
	indent_message(indent, "multiply_color = ");
	gfx_print_color(&global->multiply_color);
	sys_message(" (");
	gfx_print_color(&local->multiply_color);
	sys_message("),\n");

	indent--;
	indent_message(indent, "}");
}

static void _parts_print(struct parts *parts, int indent)
{
	static const char *state_names[PARTS_NR_STATES] = {
		[PARTS_STATE_DEFAULT] = "PARTS_STATE_DEFAULT",
		[PARTS_STATE_HOVERED] = "PARTS_STATE_HOVERED",
		[PARTS_STATE_CLICKED] = "PARTS_STATE_CLICKED"
	};
	const char *state = "INVALID_PARTS_STATE";
	if (parts->state >= 0 && parts->state < PARTS_NR_STATES)
		state = state_names[parts->state];

	indent_message(indent, "parts %d = {\n", parts->no);
	indent++;

	// print states
	indent_message(indent, "state = %s,\n", state);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		if (parts->states[i].type == PARTS_UNINITIALIZED && i != parts->state)
			continue;
		indent_message(indent, "state[%s] = ", state_names[i]);
		parts_print_state(&parts->states[i], indent);
		sys_message(",\n");
	}

	// print params
	indent_message(indent, "local = ");
	parts_print_params(&parts->global, &parts->local, indent);
	sys_message(",\n");

	// print misc. data
	if (parts->delegate_index >= 0)
		indent_message(indent, "delegate_index = %d,\n", parts->delegate_index);
	indent_message(indent, "sprite_deform = %d,\n", parts->sprite_deform);
	indent_message(indent, "clickable = %s,\n", parts->clickable ? "true" : "false");
	if (parts->on_cursor_sound >= 0)
		indent_message(indent, "on_cursor_sound = %d,\n", parts->on_cursor_sound);
	if (parts->on_click_sound >= 0)
		indent_message(indent, "on_click_sound = %d,\n", parts->on_click_sound);
	indent_message(indent, "origin_mode = %d,\n", parts->origin_mode);
	indent_message(indent, "parent = %d,\n", parts->parent ? parts->parent->no : -1);
	if (parts->linked_to >= 0)
		indent_message(indent, "linked_to = %d,\n", parts->linked_to);
	if (parts->linked_from >= 0)
		indent_message(indent, "linked_from = %d,\n", parts->linked_from);
	indent_message(indent, "draw_filter = %d,\n", parts->draw_filter);

	// print motion data
	if (TAILQ_EMPTY(&parts->motion)) {
		indent_message(indent, "motion = {},\n");
	} else {
		indent_message(indent, "motion = {\n");
		int i = 0;
		struct parts_motion *motion;
		TAILQ_FOREACH(motion, &parts->motion, entry) {
			indent_message(indent+1, "[%d] = ", i++);
			parts_print_motion(motion, indent+1);
			sys_message(",\n");
		}
		indent_message(indent, "},\n");
	}

	// print children
	if (TAILQ_EMPTY(&parts->children)) {

	} else {
		indent_message(indent, "children = {\n");
		struct parts *child;
		PARTS_FOREACH_CHILD(child, parts) {
			_parts_print(child, indent+1);
			sys_message(",\n");
		}
		indent_message(indent, "},\n");
	}

	indent--;
	indent_message(indent, "}");
}

void parts_print(struct parts *parts)
{
	_parts_print(parts, 0);
	putchar('\n');
}

void parts_engine_print(void)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!parts->parent)
			parts_print(parts);
	}
}

#ifdef DEBUGGER_ENABLED

#include "debugger.h"

static struct parts *dbg_get_parts(const char *no)
{
	struct parts *parts = parts_try_get(atoi(no));
	if (!parts)
		DBG_ERROR("No parts object with ID '%s'", no);
	return parts;
}

static void parts_cmd_parts(unsigned nr_args, char **args)
{
	struct parts *parts = dbg_get_parts(args[0]);
	if (!parts)
		return;

	parts_print(parts);
}

static void parts_list_print(struct parts *parts, int indent)
{
	indent_message(indent, parts->local.show ? "+ " : "- ");
	sys_message("parts %d ", parts->no);
	struct parts_state *state = &parts->states[parts->state];
	switch (state->type) {
	case PARTS_UNINITIALIZED:
		sys_message("(uninitialized)");
		break;
	case PARTS_CG:
		if (state->cg.name)
			sys_message("(cg %s)", display_sjis0(state->cg.name->text));
		else
			sys_message("(cg %d)", state->cg.no);
		break;
	case PARTS_TEXT:
		sys_message("(text)"); // TODO? store actual text and print it here
		break;
	case PARTS_ANIMATION:
		sys_message("(animation %d+%d)", state->anim.start_no, state->anim.nr_frames);
		break;
	case PARTS_NUMERAL:
		sys_message("(numeral %d)", state->num.cg_no);
		break;
	case PARTS_HGAUGE:
		sys_message("(hgauge)"); // TODO? store rate and cg and print them here
		break;
	case PARTS_VGAUGE:
		sys_message("(vgauge)");
		break;
	case PARTS_CONSTRUCTION_PROCESS: {
		sys_message("(construction process:");
		struct parts_cp_op *op;
		TAILQ_FOREACH(op, &state->cproc.ops, entry) {
			switch (op->type) {
			case PARTS_CP_CREATE:
				sys_message(" create");
				break;
			case PARTS_CP_CREATE_PIXEL_ONLY:
				sys_message(" create-pixel-only");
				break;
			case PARTS_CP_CG:
				sys_message(" cg");
				break;
			case PARTS_CP_FILL:
				sys_message(" fill");
				break;
			case PARTS_CP_FILL_ALPHA_COLOR:
				sys_message(" fill-alpha-color");
				break;
			case PARTS_CP_FILL_AMAP:
				sys_message(" fill-amap");
				break;
			case PARTS_CP_DRAW_CUT_CG:
				sys_message(" draw-cut-cg");
				break;
			case PARTS_CP_COPY_CUT_CG:
				sys_message(" copy-cut-cg");
				break;
			case PARTS_CP_DRAW_TEXT:
				sys_message(" draw-text");
				break;
			case PARTS_CP_COPY_TEXT:
				sys_message(" copy-text");
				break;
			}
		}
		sys_message(")");
		break;
	}
	}

	sys_message(" @ z=%d (%d)\n", parts->global.z, parts->local.z);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_list_print(child, indent + 1);
	}
}

static void parts_cmd_parts_list(unsigned nr_args, char **args)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!parts->parent)
			parts_list_print(parts, 0);
	}
}

static void parts_cmd_parts_save(unsigned nr_args, char **args)
{
	struct parts *parts = dbg_get_parts(args[0]);
	if (!parts)
		return;

	if (!parts->states[parts->state].common.texture.handle) {
		DBG_ERROR("parts texture not initialized");
		return;
	}

	// TODO: infer format from extension
	gfx_save_texture(&parts->states[parts->state].common.texture, args[1], ALCG_PNG);
}

static void parts_cmd_parts_render(unsigned nr_args, char **args)
{
	struct parts *parts = dbg_get_parts(args[0]);
	if (!parts)
		return;

	if (!parts->states[parts->state].common.texture.handle) {
		DBG_ERROR("parts texture not initialized");
		return;
	}

	// create output texture
	Texture t;
	gfx_init_texture_rgba(&t, config.view_width, config.view_height, (SDL_Color){0,0,0,0});

	// render parts to texture
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &t, 0, 0, t.w, t.h);
	parts_render(parts);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);

	// write texture to disk
	// TODO: infer format from extension
	gfx_save_texture(&t, args[1], ALCG_PNG);
	gfx_delete_texture(&t);
}

void parts_debug_init(void)
{
	struct dbg_cmd cmds[] = {
		{ "show", NULL, "<parts-no>", "Display parts object", 1, 1, parts_cmd_parts },
		{ "list", NULL, NULL, "Display parts list", 0, 0, parts_cmd_parts_list },
		{ "save", NULL, "<parts-no> <file-name>", "Save parts texture to an image file",
			2, 2, parts_cmd_parts_save },
		{ "render", NULL, "<parts-no> <file-name>", "Render parts object to an image file",
			2, 2, parts_cmd_parts_render },
	};

	dbg_cmd_add_module("parts", sizeof(cmds)/sizeof(*cmds), cmds);
}

#else

void parts_debug_init(void) {}

#endif // DEBUGGER ENABLED
