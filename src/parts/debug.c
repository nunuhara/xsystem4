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

#include <math.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "cJSON.h"
#include "json.h"
#include "parts_internal.h"

static void cJSON_AddSjisToObject(cJSON *obj, const char *name, const char *sjis)
{
	// TODO: can avoid copy by implementing this as part of cJSON proper
	char *utf = sjis2utf(sjis, 0);
	cJSON_AddStringToObject(obj, name, utf);
	free(utf);
}

static void parts_cg_to_json(struct parts_cg *cg, cJSON *out, bool verbose)
{
	cJSON_AddNumberToObject(out, "no", cg->no);
}

static void parts_text_to_json(struct parts_text *text, cJSON *out, bool verbose)
{
	cJSON *lines;

	cJSON_AddItemToObjectCS(out, "lines", lines = cJSON_CreateArray());
	for (unsigned i = 0; i < text->nr_lines; i++) {
		cJSON *line;
		struct string *s = parts_text_line_get(&text->lines[i]);
		cJSON_AddItemToArray(lines, line = cJSON_CreateObject());
		cJSON_AddSjisToObject(line, "contents", s->text);
		cJSON_AddNumberToObject(line, "width", text->lines[i].width);
		cJSON_AddNumberToObject(line, "height", text->lines[i].height);
		free_string(s);
	}
	cJSON_AddNumberToObject(out, "line_space", text->line_space);
	Point cursor = { lroundf(text->cursor.x), text->cursor.y };
	cJSON_AddItemToObjectCS(out, "cursor", point_to_json(&cursor, verbose));
	cJSON_AddItemToObjectCS(out, "text_style", text_style_to_json(&text->ts, verbose));
}

static void parts_animation_to_json(struct parts_animation *anim, cJSON *out, bool verbose)
{
	cJSON *frames;

	cJSON_AddNumberToObject(out, "start_no", anim->start_no);
	cJSON_AddNumberToObject(out, "frame_time", anim->frame_time);
	cJSON_AddNumberToObject(out, "elapsed", anim->elapsed);
	cJSON_AddNumberToObject(out, "current_frame", anim->current_frame);
	cJSON_AddItemToObjectCS(out, "frames", frames = cJSON_CreateArray());
	for (unsigned i = 0; i < anim->nr_frames; i++) {
		cJSON_AddItemToArray(frames, texture_to_json(&anim->frames[i], verbose));
	}
}

static void parts_numeral_to_json(struct parts_numeral *num, cJSON *out, bool verbose)
{
	cJSON *cg;

	cJSON_AddBoolToObject(out, "have_num", num->have_num);
	cJSON_AddNumberToObject(out, "num", num->num);
	cJSON_AddNumberToObject(out, "space", num->space);
	cJSON_AddBoolToObject(out, "show_comma", num->show_comma);
	cJSON_AddNumberToObject(out, "length", num->length);
	if (num->font_no >= 0) {
		struct parts_numeral_font *font = &parts_numeral_fonts[num->font_no];
		cJSON_AddNumberToObject(out, "cg_no", font->cg_no);
		cJSON_AddItemToObjectCS(out, "cg", cg = cJSON_CreateArray());
		for (int i = 0; i < 12; i++) {
			cJSON_AddItemToArray(cg, texture_to_json(&font->cg[i], verbose));
		}
	} else {
		cJSON_AddNumberToObject(out, "cg_no", -1);
		cJSON_AddItemToObjectCS(out, "cg", cg = cJSON_CreateArray());
	}
}

static void parts_gauge_to_json(struct parts_gauge *gauge, cJSON *out, bool verbose)
{
	cJSON_AddItemToObject(out, "cg", texture_to_json(&gauge->cg, verbose));
}

static void parts_construction_process_to_json(struct parts_construction_process *cproc,
		cJSON *out, bool verbose)
{
	static const char *type_names[PARTS_NR_CP_TYPES] = {
		[PARTS_CP_CREATE] = "create",
		[PARTS_CP_CREATE_PIXEL_ONLY] = "create_pixel_only",
		[PARTS_CP_CG] = "cg",
		[PARTS_CP_FILL] = "fill",
		[PARTS_CP_FILL_ALPHA_COLOR] = "fill_alpha_color",
		[PARTS_CP_FILL_AMAP] = "fill_amap",
		[PARTS_CP_DRAW_RECT] = "draw_rect",
		[PARTS_CP_DRAW_CUT_CG] = "draw_cut_cg",
		[PARTS_CP_COPY_CUT_CG] = "copy_cut_cg",
		[PARTS_CP_DRAW_TEXT] = "draw_text",
		[PARTS_CP_COPY_TEXT] = "copy_text",
	};

	cJSON *ops, *tmp;

	cJSON_AddItemToObjectCS(out, "operations", ops = cJSON_CreateArray());

	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		const char *type = "invalid";
		if (op->type >= 0 && op->type < PARTS_NR_CP_TYPES)
			type = type_names[op->type];

		cJSON *obj;
		cJSON_AddItemToArray(ops, obj = cJSON_CreateObject());
		cJSON_AddStringToObject(obj, "type", type);
		switch (op->type) {
		case PARTS_CP_CREATE:
		case PARTS_CP_CREATE_PIXEL_ONLY:
			cJSON_AddItemToObjectCS(obj, "size", tmp = cJSON_CreateObject());
			cJSON_AddNumberToObject(tmp, "w", op->create.w);
			cJSON_AddNumberToObject(tmp, "h", op->create.h);
			break;
		case PARTS_CP_CG:
			cJSON_AddNumberToObject(obj, "no", op->cg.no);
			break;
		case PARTS_CP_FILL:
		case PARTS_CP_FILL_ALPHA_COLOR:
		case PARTS_CP_FILL_AMAP:
		case PARTS_CP_DRAW_RECT:
			cJSON_AddItemToObjectCS(obj, "rect", tmp = cJSON_CreateObject());
			cJSON_AddNumberToObject(tmp, "x", op->fill.x);
			cJSON_AddNumberToObject(tmp, "y", op->fill.y);
			cJSON_AddNumberToObject(tmp, "w", op->fill.w);
			cJSON_AddNumberToObject(tmp, "h", op->fill.h);
			cJSON_AddItemToObjectCS(obj, "color", tmp = cJSON_CreateObject());
			cJSON_AddNumberToObject(tmp, "r", op->fill.r);
			cJSON_AddNumberToObject(tmp, "g", op->fill.g);
			cJSON_AddNumberToObject(tmp, "b", op->fill.b);
			cJSON_AddNumberToObject(tmp, "a", op->fill.a);
			break;
		case PARTS_CP_DRAW_CUT_CG:
		case PARTS_CP_COPY_CUT_CG:
			cJSON_AddNumberToObject(obj, "no", op->cut_cg.cg_no);
			cJSON_AddItemToObjectCS(obj, "dst", tmp = cJSON_CreateObject());
			cJSON_AddNumberToObject(tmp, "x", op->cut_cg.dx);
			cJSON_AddNumberToObject(tmp, "y", op->cut_cg.dy);
			cJSON_AddNumberToObject(tmp, "w", op->cut_cg.dw);
			cJSON_AddNumberToObject(tmp, "h", op->cut_cg.dh);
			cJSON_AddItemToObjectCS(obj, "src", tmp = cJSON_CreateObject());
			cJSON_AddNumberToObject(tmp, "x", op->cut_cg.sx);
			cJSON_AddNumberToObject(tmp, "y", op->cut_cg.sy);
			cJSON_AddNumberToObject(tmp, "w", op->cut_cg.sw);
			cJSON_AddNumberToObject(tmp, "h", op->cut_cg.sh);
			cJSON_AddNumberToObject(obj, "interp_type", op->cut_cg.interp_type);
			break;
		case PARTS_CP_DRAW_TEXT:
		case PARTS_CP_COPY_TEXT:
			cJSON_AddSjisToObject(obj, "text", op->text.text->text);
			cJSON_AddItemToObjectCS(obj, "pos", tmp = cJSON_CreateObject());
			cJSON_AddNumberToObject(tmp, "x", op->text.x);
			cJSON_AddNumberToObject(tmp, "y", op->text.y);
			cJSON_AddNumberToObject(obj, "line_space", op->text.line_space);
			cJSON_AddItemToObjectCS(obj, "style", text_style_to_json(&op->text.style, verbose));
			break;
		}
	}
}

static void parts_flash_to_json(struct parts_flash *flash, cJSON *out, bool verbose)
{
	cJSON_AddSjisToObject(out, "filename", flash->name->text);
	cJSON_AddNumberToObject(out, "frame_count", flash->swf->frame_count);
	cJSON_AddNumberToObject(out, "current_frame", flash->current_frame);
}

static cJSON *parts_state_to_json(struct parts_state *state, bool verbose)
{
	static const char *state_types[PARTS_NR_TYPES] = {
		[PARTS_UNINITIALIZED] = "uninitialized",
		[PARTS_CG] = "cg",
		[PARTS_TEXT] = "text",
		[PARTS_ANIMATION] = "animation",
		[PARTS_NUMERAL] = "numeral",
		[PARTS_HGAUGE] = "hgauge",
		[PARTS_VGAUGE] = "vgauge",
		[PARTS_CONSTRUCTION_PROCESS] = "construction_process",
		[PARTS_FLASH] = "flash"
	};
	const char *type = "invalid";
	if (state->type >= 0 && state->type < PARTS_NR_TYPES)
		type = state_types[state->type];

	cJSON *obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "type", type);
	cJSON_AddItemToObjectCS(obj, "size", wh_to_json(state->common.w, state->common.h, verbose));
	cJSON_AddItemToObjectCS(obj, "origin_offset", point_to_json(&state->common.origin_offset, verbose));
	cJSON_AddItemToObjectCS(obj, "hitbox", rectangle_to_json(&state->common.hitbox, verbose));
	cJSON_AddItemToObjectCS(obj, "surface_area", rectangle_to_json(&state->common.surface_area, verbose));

	switch (state->type) {
	case PARTS_UNINITIALIZED:
		break;
	case PARTS_CG:
		parts_cg_to_json(&state->cg, obj, verbose);
		break;
	case PARTS_TEXT:
		parts_text_to_json(&state->text, obj, verbose);
		break;
	case PARTS_ANIMATION:
		parts_animation_to_json(&state->anim, obj, verbose);
		break;
	case PARTS_NUMERAL:
		parts_numeral_to_json(&state->num, obj, verbose);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		parts_gauge_to_json(&state->gauge, obj, verbose);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		parts_construction_process_to_json(&state->cproc, obj, verbose);
		break;
	case PARTS_FLASH:
		parts_flash_to_json(&state->flash, obj, verbose);
		break;
	}

	return obj;
}

static cJSON *parts_params_to_json(struct parts_params *params, bool verbose)
{
	cJSON *obj, *tmp;

	obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "z", params->z);
	cJSON_AddItemToObjectCS(obj, "pos", point_to_json(&params->pos, verbose));
	cJSON_AddBoolToObject(obj, "show", params->show);
	cJSON_AddNumberToObject(obj, "alpha", params->alpha);
	cJSON_AddItemToObjectCS(obj, "scale", tmp = cJSON_CreateObject());
	cJSON_AddNumberToObject(tmp, "x", params->scale.x);
	cJSON_AddNumberToObject(tmp, "y", params->scale.y);
	cJSON_AddItemToObjectCS(obj, "rotation", tmp = cJSON_CreateObject());
	cJSON_AddNumberToObject(tmp, "x", params->rotation.x);
	cJSON_AddNumberToObject(tmp, "y", params->rotation.y);
	cJSON_AddNumberToObject(tmp, "z", params->rotation.z);
	cJSON_AddItemToObjectCS(obj, "add_color", color_to_json(&params->add_color, verbose));
	cJSON_AddItemToObjectCS(obj, "mul_color", color_to_json(&params->multiply_color, verbose));
	return obj;
}

static cJSON *parts_motion_param_to_json(union parts_motion_param param, enum parts_motion_type type)
{
	cJSON *obj;

	switch (type) {
	case PARTS_MOTION_POS:
		obj = cJSON_CreateObject();
		cJSON_AddNumberToObject(obj, "x", param.x);
		cJSON_AddNumberToObject(obj, "y", param.y);
		return obj;
	case PARTS_MOTION_VIBRATION_SIZE:
		obj = cJSON_CreateObject();
		cJSON_AddNumberToObject(obj, "w", param.x);
		cJSON_AddNumberToObject(obj, "h", param.y);
		return obj;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		return cJSON_CreateNumber(param.i);
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		return cJSON_CreateNumber(param.f);
	}
	return cJSON_CreateNull();
}

static cJSON *parts_motion_to_json(struct parts_motion *motion)
{
	static const char *type_names[PARTS_NR_MOTION_TYPES] = {
		[PARTS_MOTION_POS] = "pos",
		[PARTS_MOTION_ALPHA] = "alpha",
		[PARTS_MOTION_CG] = "cg",
		[PARTS_MOTION_HGAUGE_RATE] = "hgauge_rate",
		[PARTS_MOTION_VGAUGE_RATE] = "vgauge_rate",
		[PARTS_MOTION_NUMERAL_NUMBER] = "numeral_number",
		[PARTS_MOTION_MAG_X] = "mag_x",
		[PARTS_MOTION_MAG_Y] = "mag_y",
		[PARTS_MOTION_ROTATE_X] = "rotate_x",
		[PARTS_MOTION_ROTATE_Y] = "rotate_y",
		[PARTS_MOTION_ROTATE_Z] = "rotate_z",
		[PARTS_MOTION_VIBRATION_SIZE] = "vibration_size"
	};

	const char *type = "invalid";
	if (motion->type >= 0 && motion->type < PARTS_NR_MOTION_TYPES)
		type = type_names[motion->type];

	cJSON *obj;

	obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "type", type);
	cJSON_AddItemToObjectCS(obj, "begin", parts_motion_param_to_json(motion->begin, motion->type));
	cJSON_AddItemToObjectCS(obj, "end", parts_motion_param_to_json(motion->end, motion->type));
	cJSON_AddNumberToObject(obj, "begin_time", motion->begin_time);
	cJSON_AddNumberToObject(obj, "end_time", motion->end_time);
	return obj;
}

cJSON *parts_to_json(struct parts *parts, bool recursive, bool verbose)
{
	static const char *state_names[PARTS_NR_STATES] = {
		[PARTS_STATE_DEFAULT] = "default",
		[PARTS_STATE_HOVERED] = "hovered",
		[PARTS_STATE_CLICKED] = "clicked"
	};
	const char *state = "invalid";
	if (parts->state >= 0 && parts->state < PARTS_NR_STATES)
		state = state_names[parts->state];

	cJSON *obj, *motions, *children;

	obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "no", parts->no);
	cJSON_AddStringToObject(obj, "state", state);
	cJSON_AddItemToObjectCS(obj, "default", parts_state_to_json(&parts->states[PARTS_STATE_DEFAULT], verbose));
	cJSON_AddItemToObjectCS(obj, "hovered", parts_state_to_json(&parts->states[PARTS_STATE_HOVERED], verbose));
	cJSON_AddItemToObjectCS(obj, "clicked", parts_state_to_json(&parts->states[PARTS_STATE_CLICKED], verbose));
	cJSON_AddItemToObjectCS(obj, "local", parts_params_to_json(&parts->local, verbose));
	cJSON_AddItemToObjectCS(obj, "global", parts_params_to_json(&parts->global, verbose));
	if (parts->delegate_index >= 0)
		cJSON_AddNumberToObject(obj, "delegate_index", parts->delegate_index);
	cJSON_AddNumberToObject(obj, "sprite_deform", parts->sprite_deform);
	cJSON_AddBoolToObject(obj, "clickable", parts->clickable);
	if (parts->on_cursor_sound >= 0)
		cJSON_AddNumberToObject(obj, "on_cursor_sound", parts->on_cursor_sound);
	if (parts->on_click_sound >= 0)
		cJSON_AddNumberToObject(obj, "on_click_sound", parts->on_click_sound);
	cJSON_AddNumberToObject(obj, "origin_mode", parts->origin_mode);
	cJSON_AddNumberToObject(obj, "parent", parts->parent ? parts->parent->no : -1);
	if (parts->linked_to >= 0)
		cJSON_AddNumberToObject(obj, "linked_to", parts->linked_to);
	if (parts->linked_from >= 0)
		cJSON_AddNumberToObject(obj, "linked_from", parts->linked_from);
	cJSON_AddNumberToObject(obj, "draw_filter", parts->draw_filter);
	cJSON_AddBoolToObject(obj, "message_window", parts->message_window);

	cJSON_AddItemToObjectCS(obj, "motions", motions = cJSON_CreateArray());
	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &parts->motion, entry) {
		cJSON_AddItemToArray(motions, parts_motion_to_json(motion));
	}

	if (!recursive)
		return obj;

	cJSON_AddItemToObjectCS(obj, "children", children = cJSON_CreateArray());
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		cJSON_AddItemToArray(children, parts_to_json(child, true, verbose));
	}

	return obj;
}

cJSON *parts_sprite_to_json(struct sprite *sp, bool verbose)
{
	cJSON *ent = scene_sprite_to_json(sp, verbose);
	cJSON_AddItemToObjectCS(ent, "parts", parts_to_json((struct parts*)sp, false, verbose));
	return ent;
}

cJSON *parts_engine_to_json(struct sprite *sp, bool verbose)
{
	// XXX: only add parts to sprite object in verbose mode
	cJSON *obj = scene_sprite_to_json(sp, verbose);
	if (!verbose)
		return obj;

	cJSON *a;
	cJSON_AddItemToObjectCS(obj, "parts", a = cJSON_CreateArray());

	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!parts->parent)
			cJSON_AddItemToArray(a, parts_to_json(parts, true, verbose));
	}
	return obj;
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

	cJSON *json = parts_to_json(parts, true, true);
	char *text = cJSON_Print(json);

	sys_message("%s\n", text);

	free(text);
	cJSON_Delete(json);
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
		sys_message("(numeral %d)", state->num.font_no);
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
			case PARTS_CP_DRAW_RECT:
				sys_message(" draw-rect");
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
	case PARTS_FLASH:
		sys_message("(flash %s)", display_sjis0(state->flash.name->text));
		break;
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
