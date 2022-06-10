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

#include <assert.h>
#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"

static struct parts_construction_process *get_cproc(int parts_no, int state)
{
	return parts_get_construction_process(parts_get(parts_no), state);
}

void parts_cp_op_free(struct parts_cp_op *op)
{
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
	case PARTS_CP_CG:
	case PARTS_CP_FILL_ALPHA_COLOR:
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
		free_string(op->text.text);
		break;
	}
	free(op);
}

static void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op)
{
	TAILQ_INSERT_TAIL(&cproc->ops, op, entry);
}

bool PE_ClearPartsConstructionProcess(int parts_no, int state);

bool PE_AddCreateToPartsConstructionProcess(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CREATE;
	op->create.w = w;
	op->create.h = h;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CREATE_PIXEL_ONLY;
	op->create.w = w;
	op->create.h = h;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCreateCGToProcess(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int no;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &no)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CG;
	op->cg.no = no;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int state);

bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_ALPHA_COLOR;
	op->fill_alpha_color = (struct parts_cp_fill_alpha_color) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillAMapToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int a, int state);
bool PE_AddFillWithAlphaToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state);
bool PE_AddFillGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int top_r, int top_g, int top_b, int bot_r, int bot_g, int bot_b, int state);
bool PE_AddDrawRectToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state);
bool PE_AddDrawCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddCopyCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddGrayFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		bool full_size, int state);
bool PE_AddAddFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddMulFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddDrawLineToPartsConstructionProcess(int parts_no, int x1, int y1, int x2, int y2,
		int r, int g, int b, int a, int state);

static bool add_text_to_cproc(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state, enum parts_cp_op_type op_type)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = op_type;
	op->text = (struct parts_cp_text) {
		.text = string_dup(text),
		.x = x,
		.y = y,
		.line_space = line_space,
		.style = {
			.face = type,
			.size = size,
			.bold_width = bold_weight,
			.weight = 0,
			.edge_left = edge_weight,
			.edge_up = edge_weight,
			.edge_right = edge_weight,
			.edge_down = edge_weight,
			.color = { r, g, b, 255 },
			.edge_color = { edge_r, edge_g, edge_b, 255 },
			.scale_x = 1.0f,
			.space_scale_x = 1.0f,
			.font_spacing = char_space
		}
	};

	parts_add_cp_op(get_cproc(parts_no, state), op);
	return true;

}


bool PE_AddDrawTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_DRAW_TEXT);
}

bool PE_AddCopyTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_COPY_TEXT);
}

static void build_create(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgba(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,0});
	parts->rect.w = op->w;
	parts->rect.h = op->h;
	parts_recalculate_pos(parts);
}

static void build_create_pixel_only(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgb(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,255});
	parts->rect.w = op->w;
	parts->rect.h = op->h;
	parts_recalculate_pos(parts);
}

static void build_cg(struct parts *parts, struct parts_construction_process *cproc, struct parts_cp_cg *op)
{
	struct cg *cg = asset_cg_load(op->no);
	assert(cg);
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_with_cg(&cproc->common.texture, cg);
	parts_set_cg_dims(parts, cg);
	parts_recalculate_pos(parts);
	cg_free(cg);
}

static void build_fill_alpha_color(struct parts_construction_process *cproc,
		struct parts_cp_fill_alpha_color *op)
{
	gfx_fill_alpha_color(&cproc->common.texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b, op->a);
}

static void build_draw_text(struct parts_construction_process *cproc, struct parts_cp_text *op)
{
	// FIXME: this should set alpha to 0 in text area
	gfx_render_text(&cproc->common.texture, op->x, op->y, op->text->text, &op->style);
}

static void build_copy_text(struct parts_construction_process *cproc, struct parts_cp_text *op)
{
	gfx_render_text(&cproc->common.texture, op->x, op->y, op->text->text, &op->style);
}

bool PE_BuildPartsConstructionProcess(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		switch (op->type) {
		case PARTS_CP_CREATE:
			build_create(parts, cproc, &op->create);
			break;
		case PARTS_CP_CREATE_PIXEL_ONLY:
			build_create_pixel_only(parts, cproc, &op->create);
			break;
		case PARTS_CP_CG:
			build_cg(parts, cproc, &op->cg);
			break;
		case PARTS_CP_FILL_ALPHA_COLOR:
			build_fill_alpha_color(cproc, &op->fill_alpha_color);
			break;
		case PARTS_CP_DRAW_TEXT:
			build_draw_text(cproc, &op->text);
			break;
		case PARTS_CP_COPY_TEXT:
			build_copy_text(cproc, &op->text);
			break;
		}
	}
	parts_recalculate_pos(parts);
	parts_dirty(parts);
	return true;
}

bool PE_SetPartsConstructionSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	parts_set_surface_area(&cproc->common, x, y, w, h);
	return true;
}
