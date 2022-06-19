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
	indent_printf(indent, "cg.no = %d,\n", cg->no);
}

static void parts_text_print(struct parts_text *text, int indent)
{
	indent_printf(indent, "text.lines = {");
	for (unsigned i = 0; i < text->nr_lines; i++) {
		if (i > 0)
			putchar(',');
		printf("%u", text->lines[i].height);
	}
	printf("},\n");
	indent_printf(indent, "text.line_space = %u,\n", text->line_space);
	indent_printf(indent, "text.cursor = ");
	gfx_print_point(&text->cursor);
	printf(",\n");
	indent_printf(indent, "text.ts = ");
	gfx_print_text_style(&text->ts, indent);
}

static void parts_animation_print(struct parts_animation *anim, int indent)
{
	indent_printf(indent, "anim.cg_no = %u,\n", anim->cg_no);
	indent_printf(indent, "anim.frame_time = %u,\n", anim->frame_time);
	indent_printf(indent, "anim.elapsed = %u,\n", anim->elapsed);
	indent_printf(indent, "anim.current_frame = %u,\n", anim->current_frame);
	indent_printf(indent, "anim.frames = {\n");
	for (unsigned i = 0; i < anim->nr_frames; i++) {
		indent_printf(indent+1, "[%u] = ", i);
		gfx_print_texture(&anim->frames[i], indent+1);
		indent_printf(indent+1, ",\n");
	}
	indent_printf(indent, "}\n");
}

static void parts_numeral_print(struct parts_numeral *num, int indent)
{
	indent_printf(indent, "num.space = %d,\n", num->space);
	indent_printf(indent, "num.show_comma = %d,\n", num->show_comma);
	indent_printf(indent, "num.cg_no = %d,\n", num->cg_no);
	indent_printf(indent, "num.cg = {\n");
	for (int i = 0; i < 12; i++) {
		indent_printf(indent+1, "[%d] = ", i);
		gfx_print_texture(&num->cg[i], indent+1);
		indent_printf(indent+1, ",\n");
	}
	indent_printf(indent, "}\n");
}

static void parts_gauge_print(struct parts_gauge *gauge, int indent)
{
	indent_printf(indent, "gauge.cg = ");
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

	indent_printf(indent, "cproc.ops = {\n");
	indent++;

	int i = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		indent_printf(indent, "[%d] = {\n", i++);
		indent++;

		const char *type = "INVALID_CONSTRUCTION_PROCERSS_TYPE";
		if (op->type >= 0 && op->type < PARTS_NR_CP_TYPES)
			type = type_names[op->type];
		indent_printf(indent, "type = %s,\n", type);

		switch (op->type) {
		case PARTS_CP_CREATE:
		case PARTS_CP_CREATE_PIXEL_ONLY:
			indent_printf(indent, "create = {w=%d,h=%d}\n", op->create.w, op->create.h);
			break;
		case PARTS_CP_CG:
			indent_printf(indent, "cg.no = %d\n", op->cg.no);
			break;
		case PARTS_CP_FILL:
		case PARTS_CP_FILL_ALPHA_COLOR:
		case PARTS_CP_FILL_AMAP:
			indent_printf(indent, "fill.rect = {x=%d,y=%d,w=%d,h=%d},\n",
					op->fill.x, op->fill.y, op->fill.w, op->fill.h);
			indent_printf(indent, "fill.color = (%d,%d,%d,%d)\n",
					op->fill.r, op->fill.g, op->fill.b, op->fill.a);
			break;
		case PARTS_CP_DRAW_CUT_CG:
		case PARTS_CP_COPY_CUT_CG:
			indent_printf(indent, "cut_cg.cg_no = %d,\n", op->cut_cg.cg_no);
			indent_printf(indent, "cut_cg.dst = {x=%d,y=%d,w=%d,h=%d},\n",
					op->cut_cg.dx, op->cut_cg.dy, op->cut_cg.dw, op->cut_cg.dh);
			indent_printf(indent, "cut_cg.src = {x=%d,y=%d,w=%d,h=%d},\n",
					op->cut_cg.sx, op->cut_cg.sy, op->cut_cg.sw, op->cut_cg.sh);
			indent_printf(indent, "cut_cg.interp_type = %d\n", op->cut_cg.interp_type);
			break;
		case PARTS_CP_DRAW_TEXT:
		case PARTS_CP_COPY_TEXT:
			indent_printf(indent, "text.text = \"%s\",\n", display_sjis0(op->text.text->text));
			indent_printf(indent, "text.pos = {x=%d,y=%d},\n", op->text.x, op->text.y);
			indent_printf(indent, "text.line_space = %d,\n", op->text.line_space);
			indent_printf(indent, "text.style = ");
			gfx_print_text_style(&op->text.style, indent);
			printf("\n");
			break;
		}
		indent--;
		printf("},\n");
	}

	indent--;
	indent_printf(indent, "}\n");
}

static void parts_print_state(struct parts_state *state, int indent)
{
	if (state->type == PARTS_UNINITIALIZED) {
		printf("UNINITIALIZED");
		return;
	}
	printf("{\n");
	indent++;

	struct parts_common *com = &state->common;

	indent_printf(indent, "texture = ");
	gfx_print_texture(&state->common.texture, indent);
	printf(",\n");
	indent_printf(indent, "dims = {w=%d,h=%d},\n", state->common.w, state->common.h);
	indent_printf(indent, "origin_offset = "); gfx_print_point(&com->origin_offset); printf(",\n");
	indent_printf(indent, "hitbox = "); gfx_print_rectangle(&com->hitbox); printf(",\n");
	indent_printf(indent, "surface_area = "); gfx_print_rectangle(&com->surface_area); printf(",\n");
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
	indent_printf(indent, "}");
}

static void parts_print_motion_param(union parts_motion_param param, enum parts_motion_type type)
{
	switch (type) {
	case PARTS_MOTION_POS:
		printf("{x=%d,y=%d}", param.x, param.y);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		printf("{w=%d,h=%d}", param.x, param.y);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		printf("%d", param.i);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		printf("%f", param.f);
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

	printf("{\n");
	indent++;

	indent_printf(indent, "type = %s,\n", type);
	indent_printf(indent, "begin = ");
	parts_print_motion_param(motion->begin, motion->type);
	printf(",\n");
	indent_printf(indent, "end = ");
	parts_print_motion_param(motion->end, motion->type);
	printf(",\n");
	indent_printf(indent, "begin_time = %d,\n", motion->begin_time);
	indent_printf(indent, "end_time = %d,\n", motion->end_time);

	indent--;
	indent_printf(indent, "}");
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

	indent_printf(indent, "parts %d = {\n", parts->no);
	indent++;

	// print states
	indent_printf(indent, "state = %s,\n", state);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		indent_printf(indent, "state[%s] = ", state_names[i]);
		parts_print_state(&parts->states[i], indent);
		printf(",\n");
	}

	// print misc. data
	indent_printf(indent, "delegate_index = %d,\n", parts->delegate_index);
	indent_printf(indent, "sprite_deform = %d,\n", parts->sprite_deform);
	indent_printf(indent, "clickable = %s,\n", parts->clickable ? "true" : "false");
	indent_printf(indent, "on_cursor_sound = %d,\n", parts->on_cursor_sound);
	indent_printf(indent, "on_click_sound = %d,\n", parts->on_click_sound);
	indent_printf(indent, "origin_mode = %d,\n", parts->origin_mode);
	indent_printf(indent, "parent = %d,\n", parts->parent);
	indent_printf(indent, "linked_to = %d,\n", parts->linked_to);
	indent_printf(indent, "linked_from = %d,\n", parts->linked_from);
	indent_printf(indent, "alpha = %u,\n", (unsigned)parts->alpha);
	indent_printf(indent, "pos = "); gfx_print_point(&parts->pos); printf(",\n");
	indent_printf(indent, "z = %d,\n", parts->z);
	indent_printf(indent, "show = %s,\n", parts->show ? "true" : "false");
	indent_printf(indent, "scale = {x=%f,y=%f},\n", parts->scale.x, parts->scale.y);
	indent_printf(indent, "rotation = {x=%f,y=%f,z=%f},\n",
			parts->rotation.x, parts->rotation.y, parts->rotation.z);

	// print motion data
	if (TAILQ_EMPTY(&parts->motion)) {
		indent_printf(indent, "motion = {},\n");
	} else {
		indent_printf(indent, "motion = {\n");
		int i = 0;
		struct parts_motion *motion;
		TAILQ_FOREACH(motion, &parts->motion, entry) {
			indent_printf(indent+1, "[%d] = ", i++);
			parts_print_motion(motion, indent+1);
			printf(",\n");
		}
		indent_printf(indent, "},\n");
	}

	// print children
	if (TAILQ_EMPTY(&parts->children)) {

	} else {
		indent_printf(indent, "children = {\n");
		struct parts *child;
		PARTS_FOREACH_CHILD(child, parts) {
			_parts_print(child, indent+1);
			printf(",\n");
		}
		indent_printf(indent, "},\n");
	}

	indent--;
	indent_printf(indent, "}");
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
		if (parts->parent < 0)
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
	indent_printf(indent, parts->show ? "+ " : "- ");
	printf("parts %d ", parts->no);
	struct parts_state *state = &parts->states[parts->state];
	switch (state->type) {
	case PARTS_UNINITIALIZED:
		printf("(uninitialized)");
		break;
	case PARTS_CG:
		printf("(cg %d)", state->cg.no);
		break;
	case PARTS_TEXT:
		printf("(text)"); // TODO? store actual text and print it here
		break;
	case PARTS_ANIMATION:
		printf("(animation %d+%d)", state->anim.cg_no, state->anim.nr_frames);
		break;
	case PARTS_NUMERAL:
		printf("(numeral %d)", state->num.cg_no);
		break;
	case PARTS_HGAUGE:
		printf("(hgauge)"); // TODO? store rate and cg and print them here
		break;
	case PARTS_VGAUGE:
		printf("(vgauge)");
		break;
	case PARTS_CONSTRUCTION_PROCESS: {
		printf("(construction process:");
		struct parts_cp_op *op;
		TAILQ_FOREACH(op, &state->cproc.ops, entry) {
			switch (op->type) {
			case PARTS_CP_CREATE:
				printf(" create");
				break;
			case PARTS_CP_CREATE_PIXEL_ONLY:
				printf(" create-pixel-only");
				break;
			case PARTS_CP_CG:
				printf(" cg");
				break;
			case PARTS_CP_FILL:
				printf(" fill");
				break;
			case PARTS_CP_FILL_ALPHA_COLOR:
				printf(" fill-alpha-color");
				break;
			case PARTS_CP_FILL_AMAP:
				printf(" fill-amap");
				break;
			case PARTS_CP_DRAW_CUT_CG:
				printf(" draw-cut-cg");
				break;
			case PARTS_CP_COPY_CUT_CG:
				printf(" copy-cut-cg");
				break;
			case PARTS_CP_DRAW_TEXT:
				printf(" draw-text");
				break;
			case PARTS_CP_COPY_TEXT:
				printf(" copy-text");
				break;
			}
		}
		printf(")");
		break;
	}
	}

	printf(" @ z=%d (%d)\n", parts->global_z, parts->z);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_list_print(child, indent + 1);
	}
}

static void parts_cmd_parts_list(unsigned nr_args, char **args)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts->parent < 0)
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
		{ "show", NULL, "<parts-no> - Display parts object", 1, 1, parts_cmd_parts },
		{ "list", NULL, "- Display parts list", 0, 0, parts_cmd_parts_list },
		{ "save", NULL, "<parts-no> <file-name> - Save parts texture to an image file",
			2, 2, parts_cmd_parts_save },
		{ "render", NULL, "<parts-no> <file-name> - Render parts object to an image file",
			2, 2, parts_cmd_parts_render },
	};

	dbg_cmd_add_module("parts", sizeof(cmds)/sizeof(*cmds), cmds);
}

#else

void parts_debug_init(void) {}

#endif // DEBUGGER ENABLED
