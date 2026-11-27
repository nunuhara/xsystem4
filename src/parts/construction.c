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
#include <math.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "gfx/gl.h"
#include "vm.h"
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
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_FILL_GRADATION_HORIZON:
	case PARTS_CP_MUL_AMAP_GRADATION_HORIZON:
	case PARTS_CP_MUL_AMAP_GRADATION_VERTICAL:
	case PARTS_CP_DRAW_RECT:
	case PARTS_CP_GRAY_FILTER:
	case PARTS_CP_ADD_FILTER:
	case PARTS_CP_MUL_FILTER:
	case PARTS_CP_HBLUR_FILTER:
	case PARTS_CP_VBLUR_FILTER:
	case PARTS_CP_DRAW_LINE:
		break;
	case PARTS_CP_CG:
	case PARTS_CP_BLEND_CG:
		free_string(op->cg.name);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
	case PARTS_CP_ALPHA_BLEND_CUT_CG:
	case PARTS_CP_ONLY_ALPHA_CUT_CG:
		free_string(op->cut_cg.cg_name);
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
	case PARTS_CP_ALPHA_BLEND_TEXT:
	case PARTS_CP_ONLY_ALPHA_TEXT:
		free_string(op->text.text);
		break;
	case PARTS_CP_V14:
		if (op->v14.cg_name)
			free_string(op->v14.cg_name);
		free(op->v14.coords);
		break;
	}
	free(op);
}

void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op)
{
	TAILQ_INSERT_TAIL(&cproc->ops, op, entry);
}

void parts_cp_set_last_fill_full_size(int parts_no, int state, bool full_size)
{
	if (!full_size || !parts_state_valid(--state))
		return;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_CONSTRUCTION_PROCESS)
		return;
	struct parts_cp_op *last = NULL, *it;
	TAILQ_FOREACH(it, &parts->states[state].cproc.ops, entry)
		last = it;
	if (last)
		last->fill.full_size = true;
}

bool parts_cp_add_v14(int parts_no, const struct parts_cp_v14 *src, int state)
{
	if (!parts_state_valid(--state) || !src)
		return false;
	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_V14;
	op->v14 = *src;
	op->v14.cg_name = src->cg_name ? string_dup(src->cg_name) : string_ref(&EMPTY_STRING);
	if (src->nr_coords > 0 && src->coords) {
		op->v14.coords = xmalloc((size_t)src->nr_coords * sizeof(int));
		memcpy(op->v14.coords, src->coords, (size_t)src->nr_coords * sizeof(int));
	} else {
		op->v14.nr_coords = 0;
		op->v14.coords = NULL;
	}
	parts_add_cp_op(cproc, op);
	return true;
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

	if (!parts_v2 && !asset_exists_by_name(ASSET_CG, cg_name->text, NULL)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CG;
	op->cg.name = string_ref(cg_name);
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = 255
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_ALPHA_COLOR;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillAMapToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_AMAP;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = 0, .g = 0, .b = 0, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillWithAlphaToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_WITH_ALPHA;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int top_r, int top_g, int top_b, int bot_r, int bot_g, int bot_b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_GRADATION_HORIZON;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = top_r, .g = top_g, .b = top_b, .a = 255,
		.r2 = bot_r, .g2 = bot_g, .b2 = bot_b, .a2 = 255
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddMulAMapGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int top_a, int bot_a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_MUL_AMAP_GRADATION_HORIZON;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.a = top_a, .a2 = bot_a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddMulAMapGradationVerticalToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int left_a, int right_a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_MUL_AMAP_GRADATION_VERTICAL;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.a = left_a, .a2 = right_a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCutCGAlphaBlendToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, bool scale, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_ALPHA_BLEND_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_name = string_ref(cg_name),
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type,
		.scale = scale
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCutCGOnlyAlphaToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, bool scale, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_ONLY_ALPHA_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_name = string_ref(cg_name),
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type,
		.scale = scale
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCGBlendToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_BLEND_CG;
	op->cg = (struct parts_cp_cg) {
		.name = string_ref(cg_name),
		.x = dx, .y = dy
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddDrawRectToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_DRAW_RECT;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = 255
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddDrawLineToPartsConstructionProcess(int parts_no, int x0, int y0, int x1, int y1,
		int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts_cp_op *op = xcalloc(1, sizeof(*op));
	op->type = PARTS_CP_DRAW_LINE;
	op->line = (struct parts_cp_line) { x0, y0, x1, y1, r, g, b, a };
	parts_add_cp_op(get_cproc(parts_no, state), op);
	return true;
}

bool PE_AddDrawCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!parts_v2 && !asset_exists_by_name(ASSET_CG, cg_name->text, NULL)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_DRAW_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_name = string_ref(cg_name),
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type,
		.scale = (sw != dw || sh != dh)
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCopyCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!parts_v2 && !asset_exists_by_name(ASSET_CG, cg_name->text, NULL)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_COPY_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_name = string_ref(cg_name),
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type,
		.scale = (sw != dw || sh != dh)
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddGrayFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		bool full_size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_GRAY_FILTER;
	op->filter = (struct parts_cp_filter) {
		.x = x, .y = y, .w = w, .h = h,
		.full_size = full_size
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddAddFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_ADD_FILTER;
	op->filter = (struct parts_cp_filter) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b,
		.full_size = full_size
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddMulFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_MUL_FILTER;
	op->filter = (struct parts_cp_filter) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b,
		.full_size = full_size
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddHBlurFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int blur, bool full_size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_HBLUR_FILTER;
	op->filter = (struct parts_cp_filter) {
		.x = x, .y = y, .w = w, .h = h,
		.blur = blur,
		.full_size = full_size
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddVBlurFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int blur, bool full_size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_VBLUR_FILTER;
	op->filter = (struct parts_cp_filter) {
		.x = x, .y = y, .w = w, .h = h,
		.blur = blur,
		.full_size = full_size
	};

	parts_add_cp_op(cproc, op);
	return true;
}

static bool add_text_to_cproc(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state, enum parts_cp_op_type op_type)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = op_type;
	char *dict_mapped = text ? sys4_text_dict_translate_lines(text->text) : NULL;
	struct string *op_text = dict_mapped ? make_string(dict_mapped, strlen(dict_mapped)) : string_dup(text);
	free(dict_mapped);
	op->text = (struct parts_cp_text) {
		.text = op_text,
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

bool PE_AddAlphaBlendTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_ALPHA_BLEND_TEXT);
}

bool PE_AddOnlyAlphaTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_ONLY_ALPHA_TEXT);
}

static bool build_create(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgba(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,255});
	if (!cproc->common.texture.handle)
		return false;
	parts_set_dims(parts, &cproc->common, op->w, op->h);
	return true;
}

static bool build_create_pixel_only(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgb(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,255});
	if (!cproc->common.texture.handle)
		return false;
	parts_set_dims(parts, &cproc->common, op->w, op->h);
	return true;
}

static struct cg *cproc_load_cg_with_fallback(const char *name)
{
	if (!name || !name[0])
		return NULL;
	struct cg *cg = asset_cg_load_by_name(name, NULL);
	if (cg)
		return cg;

	const char *fallback = NULL;
	if (sys4_text_is_gbk) {
		/* 卡牌／物品／ -> 卡牌／物品／？？？ */
		if (!strncmp(name, "\xbf\xa8\xc5\xc6\xa3\xaf\xce\xef\xc6\xb7\xa3\xaf", 12)) {
			fallback = "\xbf\xa8\xc5\xc6\xa3\xaf\xce\xef\xc6\xb7\xa3\xaf\xa3\xbf\xa3\xbf\xa3\xbf";
		/* 卡牌／ -> 卡牌／？？？ */
		} else if (!strncmp(name, "\xbf\xa8\xc5\xc6\xa3\xaf", 6)) {
			fallback = "\xbf\xa8\xc5\xc6\xa3\xaf\xa3\xbf\xa3\xbf\xa3\xbf";
		/* シス／卡牌／技能／ -> シス／卡牌／技能／？ */
		} else if (!strncmp(name, "\xa5\xb7\xa5\xb9\xa3\xaf\xbf\xa8\xc5\xc6\xa3\xaf\xbc\xbc\xc4\xdc\xa3\xaf", 18)) {
			fallback = "\xa5\xb7\xa5\xb9\xa3\xaf\xbf\xa8\xc5\xc6\xa3\xaf\xbc\xbc\xc4\xdc\xa3\xaf\xa3\xbf";
		}
	} else {
		/* カード／アイテム／ -> カード／アイテム／？？？ */
		if (!strncmp(name, "\x83\x4a\x81\x5b\x83\x68\x81\x5e\x83\x41\x83\x43\x83\x65\x83\x80\x81\x5e", 18)) {
			fallback = "\x83\x4a\x81\x5b\x83\x68\x81\x5e\x83\x41\x83\x43\x83\x65\x83\x80\x81\x5e\x81\x48\x81\x48\x81\x48";
		/* カード／ -> カード／？？？ */
		} else if (!strncmp(name, "\x83\x4a\x81\x5b\x83\x68\x81\x5e", 8)) {
			fallback = "\x83\x4a\x81\x5b\x83\x68\x81\x5e\x81\x48\x81\x48\x81\x48";
		/* シス／カード／スキル／ -> シス／カード／スキル／？ */
		} else if (!strncmp(name, "\x83\x56\x83\x58\x81\x5e\x83\x4a\x81\x5b\x83\x68\x81\x5e\x83\x58\x83\x4c\x83\x8b\x81\x5e", 22)) {
			fallback = "\x83\x56\x83\x58\x81\x5e\x83\x4a\x81\x5b\x83\x68\x81\x5e\x83\x58\x83\x4c\x83\x8b\x81\x5e\x81\x48";
		}
	}

	if (fallback && strcmp(name, fallback) != 0)
		return asset_cg_load_by_name(fallback, NULL);
	return NULL;
}

static bool build_cg(struct parts *parts, struct parts_construction_process *cproc, struct parts_cp_cg *op)
{
	struct cg *cg = cproc_load_cg_with_fallback(op->name->text);
	if (!cg)
		return false;
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_with_cg(&cproc->common.texture, cg);
	parts_set_dims(parts, &cproc->common, cg->metrics.w, cg->metrics.h);
	cg_free(cg);
	return true;
}

static bool build_fill(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->full_size ? 0 : op->x;
	int y = op->full_size ? 0 : op->y;
	int w = op->full_size ? cproc->common.texture.w : op->w;
	int h = op->full_size ? cproc->common.texture.h : op->h;
	gfx_fill(&cproc->common.texture, x, y, w, h, op->r, op->g, op->b);
	return true;
}

static bool build_fill_alpha_color(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->full_size ? 0 : op->x;
	int y = op->full_size ? 0 : op->y;
	int w = op->full_size ? cproc->common.texture.w : op->w;
	int h = op->full_size ? cproc->common.texture.h : op->h;
	gfx_fill_alpha_color(&cproc->common.texture, x, y, w, h, op->r, op->g, op->b, op->a);
	return true;
}

static bool build_fill_amap(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->full_size ? 0 : op->x;
	int y = op->full_size ? 0 : op->y;
	int w = op->full_size ? cproc->common.texture.w : op->w;
	int h = op->full_size ? cproc->common.texture.h : op->h;
	gfx_fill_amap(&cproc->common.texture, x, y, w, h, op->a);
	return true;
}

static bool build_fill_with_alpha(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->full_size ? 0 : op->x;
	int y = op->full_size ? 0 : op->y;
	int w = op->full_size ? cproc->common.texture.w : op->w;
	int h = op->full_size ? cproc->common.texture.h : op->h;
	gfx_fill_with_alpha(&cproc->common.texture, x, y, w, h, op->r, op->g, op->b, op->a);
	return true;
}

static bool build_fill_gradation_horizon(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->full_size ? 0 : op->x;
	int y = op->full_size ? 0 : op->y;
	int w = op->full_size ? cproc->common.texture.w : op->w;
	int h = op->full_size ? cproc->common.texture.h : op->h;
	if (w <= 0 || h <= 0)
		return true;
	SDL_Color *pixels = xmalloc((size_t)w * h * sizeof(*pixels));
	int denom = max(1, h - 1);
	for (int row = 0; row < h; row++) {
		uint8_t r = min(255, max(0, op->r + (op->r2 - op->r) * row / denom));
		uint8_t g = min(255, max(0, op->g + (op->g2 - op->g) * row / denom));
		uint8_t b = min(255, max(0, op->b + (op->b2 - op->b) * row / denom));
		SDL_Color c = { r, g, b, 255 };
		for (int col = 0; col < w; col++)
			pixels[(size_t)row * w + col] = c;
	}
	Texture src;
	gfx_init_texture_with_pixels(&src, w, h, pixels);
	free(pixels);
	gfx_copy(&cproc->common.texture, x, y, &src, 0, 0, w, h);
	gfx_delete_texture(&src);
	return true;
}

static bool build_mul_amap_gradation(struct parts_construction_process *cproc, struct parts_cp_fill *op, bool vertical)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->full_size ? 0 : op->x;
	int y = op->full_size ? 0 : op->y;
	int w = op->full_size ? cproc->common.texture.w : op->w;
	int h = op->full_size ? cproc->common.texture.h : op->h;
	if (w <= 0 || h <= 0)
		return true;
	SDL_Color *pixels = xmalloc((size_t)w * h * sizeof(*pixels));
	int denom = max(1, (vertical ? w : h) - 1);
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			int pos = vertical ? col : row;
			uint8_t a = min(255, max(0, op->a + (op->a2 - op->a) * pos / denom));
			pixels[(size_t)row * w + col] = (SDL_Color){ 255, 255, 255, a };
		}
	}
	Texture src;
	gfx_init_texture_with_pixels(&src, w, h, pixels);
	free(pixels);
	gfx_blend_da_daxsa(&cproc->common.texture, x, y, &src, 0, 0, w, h);
	gfx_delete_texture(&src);
	return true;
}

static bool build_draw_rect(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->full_size ? 0 : op->x;
	int y = op->full_size ? 0 : op->y;
	int w = op->full_size ? cproc->common.texture.w : op->w;
	int h = op->full_size ? cproc->common.texture.h : op->h;
	int x2 = x + w - 1;
	int y2 = y + h - 1;
	gfx_draw_line(&cproc->common.texture, x, y, x2, y, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, x, y, x, y2, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, x2, y, x2, y2, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, x, y2, x2, y2, op->r, op->g, op->b);
	return true;
}

#define PARTS_CPROC_CACHE_SIZE 256

struct cproc_cg_cache_entry {
	struct string *name;
	Texture texture;
};

static struct cproc_cg_cache_entry cproc_cg_cache[PARTS_CPROC_CACHE_SIZE];
static int nr_cproc_cg_cache = 0;

void parts_cproc_cache_clear(void)
{
	for (int i = 0; i < nr_cproc_cg_cache; i++) {
		gfx_delete_texture(&cproc_cg_cache[i].texture);
		if (cproc_cg_cache[i].name)
			free_string(cproc_cg_cache[i].name);
	}
	nr_cproc_cg_cache = 0;
}

static void cproc_cg_cache_on_gl_reset(void *user)
{
	(void)user;
	int out = 0;
	for (int i = 0; i < nr_cproc_cg_cache; i++) {
		if (gfx_remap_texture(&cproc_cg_cache[i].texture) && cproc_cg_cache[i].texture.handle) {
			if (out != i)
				cproc_cg_cache[out] = cproc_cg_cache[i];
			out++;
		} else {
			if (cproc_cg_cache[i].name)
				free_string(cproc_cg_cache[i].name);
		}
	}
	nr_cproc_cg_cache = out;
}

static Texture *cproc_get_cached_texture(struct string *cg_name)
{
	gfx_register_reset_callback(cproc_cg_cache_on_gl_reset, NULL);
	if (!cg_name || !cg_name->size)
		return NULL;
	for (int i = 0; i < nr_cproc_cg_cache; i++) {
		if (!strcmp(cproc_cg_cache[i].name->text, cg_name->text)) {
			if (i > 0) {
				struct cproc_cg_cache_entry hit = cproc_cg_cache[i];
				memmove(&cproc_cg_cache[1], &cproc_cg_cache[0],
						i * sizeof(struct cproc_cg_cache_entry));
				cproc_cg_cache[0] = hit;
			}
			if (gfx_remap_texture(&cproc_cg_cache[0].texture) && cproc_cg_cache[0].texture.handle)
				return &cproc_cg_cache[0].texture;
			if (cproc_cg_cache[0].name)
				free_string(cproc_cg_cache[0].name);
			nr_cproc_cg_cache--;
			if (nr_cproc_cg_cache > 0)
				memmove(&cproc_cg_cache[0], &cproc_cg_cache[1], nr_cproc_cg_cache * sizeof(struct cproc_cg_cache_entry));
			break;
		}
	}

	struct cg *cg = cproc_load_cg_with_fallback(cg_name->text);
	if (!cg)
		return NULL;

	if (nr_cproc_cg_cache == PARTS_CPROC_CACHE_SIZE) {
		struct cproc_cg_cache_entry *evict = &cproc_cg_cache[PARTS_CPROC_CACHE_SIZE - 1];
		gfx_delete_texture(&evict->texture);
		if (evict->name)
			free_string(evict->name);
		nr_cproc_cg_cache--;
	}
	if (nr_cproc_cg_cache > 0) {
		memmove(&cproc_cg_cache[1], &cproc_cg_cache[0],
				nr_cproc_cg_cache * sizeof(struct cproc_cg_cache_entry));
	}
	memset(&cproc_cg_cache[0], 0, sizeof(cproc_cg_cache[0]));
	cproc_cg_cache[0].name = string_dup(cg_name);
	gfx_init_texture_with_cg(&cproc_cg_cache[0].texture, cg);
	cg_free(cg);
	nr_cproc_cg_cache++;
	return &cproc_cg_cache[0].texture;
}

static bool build_draw_cut_cg(struct parts_construction_process *cproc, struct parts_cp_op *cmd)
{
	if (!cproc->common.texture.handle)
		return false;

	struct parts_cp_cut_cg *op = &cmd->cut_cg;
	Texture *src = cproc_get_cached_texture(op->cg_name);
	if (!src)
		return false;

	int sw = op->sw > 0 ? op->sw : src->w;
	int sh = op->sh > 0 ? op->sh : src->h;
	if (game_okusama && cmd->type == PARTS_CP_ALPHA_BLEND_CUT_CG)
		gfx_copy_stretch_blend_straight_alpha(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
				src, op->sx, op->sy, sw, sh);
	else
		gfx_copy_stretch_blend_amap(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
				src, op->sx, op->sy, sw, sh);
	return true;
}

static bool build_copy_cut_cg(struct parts_construction_process *cproc, struct parts_cp_cut_cg *op)
{
	if (!cproc->common.texture.handle)
		return false;

	Texture *src = cproc_get_cached_texture(op->cg_name);
	if (!src)
		return false;

	int sw = op->sw > 0 ? op->sw : src->w;
	int sh = op->sh > 0 ? op->sh : src->h;
	gfx_copy_stretch_with_alpha_map(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
			src, op->sx, op->sy, sw, sh);
	return true;
}

static bool build_only_alpha_cut_cg(struct parts_construction_process *cproc, struct parts_cp_cut_cg *op)
{
	if (!cproc->common.texture.handle)
		return false;

	Texture *src = cproc_get_cached_texture(op->cg_name);
	if (!src)
		return false;

	int sw = op->sw > 0 ? op->sw : src->w;
	int sh = op->sh > 0 ? op->sh : src->h;
	gfx_copy_stretch_amap(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
			src, op->sx, op->sy, sw, sh);
	return true;
}

static bool build_blend_cg(struct parts_construction_process *cproc, struct parts_cp_cg *op)
{
	if (!cproc->common.texture.handle)
		return false;

	Texture *src = cproc_get_cached_texture(op->name);
	if (!src)
		return false;

	gfx_blend_amap(&cproc->common.texture, op->x, op->y, src, 0, 0, src->w, src->h);
	return true;
}

static float cproc_measure_line_width(struct text_style *ts, const char *line)
{
	int nchars = sjis_count_char(line);
	if (nchars <= 0)
		return 0.0f;
	return gfx_size_text(ts, line) + (nchars - 1) * ts->font_spacing;
}

static bool build_text(struct parts_construction_process *cproc, struct parts_cp_text *op,
		enum text_render_mode mode)
{
	if (!cproc->common.texture.handle)
		return false;
	const char *source = op->text->text;
	char *display = parts_prepare_display_text(source);
	if (parts_uses_mixed_display_text() && strchr(source, '\r')) {
		if (!display)
			display = xstrdup(source);
		char *dst = display;
		for (const char *src = display; *src; src++) {
			if (*src != '\r')
				*dst++ = *src;
		}
		*dst = '\0';
	}
	if (display)
		source = display;
	struct text_style style = op->style;
	int line_space = op->line_space;
	int pct = parts_get_modifier_font_scale();
	if (pct != 100 && style.size > 0.0f && op->text && op->text->size > 0) {
		style = parts_get_effective_text_style_pct(&op->style, pct);
		if (pct > 100) {
			int avail_w = cproc->common.texture.w - op->x;
			int avail_h = cproc->common.texture.h - op->y;
			float orig_max_w = 0.0f;
			float scaled_max_w = 0.0f;
			int nr_lines = 0;
			struct text_style orig_style = op->style;
			char *probe = xstrdup(source);
			for (char *ln = probe; ln;) {
				char *nxt = strchr(ln, '\n');
				if (nxt)
					*nxt++ = '\0';
				nr_lines++;
				if (*ln) {
					float ow = cproc_measure_line_width(&orig_style, ln);
					float sw = cproc_measure_line_width(&style, ln);
					if (ow > orig_max_w) orig_max_w = ow;
					if (sw > scaled_max_w) scaled_max_w = sw;
				}
				ln = nxt;
			}
			free(probe);

			// Prevent scaled vertical overflow on multi-line fixed canvases (e.g. cards)
			int orig_lh = (int)ceilf(text_style_height(&orig_style));
			int orig_total_h = nr_lines > 0 ? nr_lines * orig_lh + (nr_lines - 1) * op->line_space : 0;
			if (avail_h > 4 && nr_lines > 1 && orig_total_h <= avail_h + 4) {
				int cur_lh = (int)ceilf(text_style_height(&style));
				int cur_total_h = nr_lines * cur_lh + (nr_lines - 1) * line_space;
				if (cur_total_h > avail_h) {
					int min_sp = min(-1, op->line_space);
					int needed_sp = (avail_h - nr_lines * cur_lh) / (nr_lines - 1);
					line_space = max(min_sp, min(line_space, needed_sp));
					cur_total_h = nr_lines * cur_lh + (nr_lines - 1) * line_space;
					while (cur_total_h > avail_h && style.size > op->style.size) {
						style.size = max(op->style.size, style.size - 1.0f);
						style.font_size = NULL;
						cur_lh = (int)ceilf(text_style_height(&style));
						cur_total_h = nr_lines * cur_lh + (nr_lines - 1) * line_space;
					}
					// Recompute scaled_max_w if style.size stepped down
					scaled_max_w = 0.0f;
					char *reprobe = xstrdup(source);
					for (char *ln = reprobe; ln;) {
						char *nxt = strchr(ln, '\n');
						if (nxt) *nxt++ = '\0';
						if (*ln) {
							float sw = cproc_measure_line_width(&style, ln);
							if (sw > scaled_max_w) scaled_max_w = sw;
						}
						ln = nxt;
					}
					free(reprobe);
				}
			}

			// Prevent scaled horizontal right-edge clipping on fixed canvases
			int limit_w = max(avail_w, (int)ceilf(orig_max_w));
			if (avail_w > 4 && scaled_max_w > (float)limit_w && scaled_max_w > 1.0f) {
				float ratio = (float)limit_w / scaled_max_w;
				float min_sx = op->style.scale_x * 0.68f;
				float next_sx = style.scale_x * ratio;
				if (next_sx < min_sx) {
					style.scale_x = min_sx;
					float rem = next_sx / min_sx;
					float next_sz = floorf(style.size * rem);
					style.size = max(min(op->style.size, style.size), next_sz);
				} else {
					style.scale_x = next_sx;
				}
				style.font_size = NULL;
			}
		}
	}

	int h = ceilf(text_style_height(&style));
	int y = op->y;
	char *text = display ? display : xstrdup(source);
	for (char *line = text; line;) {
		char *next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		if (*line) {
			if (mode == RENDER_COPY || mode == RENDER_ALPHA_ONLY) {
				int w = ceilf(gfx_size_text(&style, line)
						+ (sjis_count_char(line) - 1) * style.font_spacing);
				if (mode == RENDER_COPY)
					gfx_fill_with_alpha(&cproc->common.texture, op->x, y, w, h,
							style.edge_color.r, style.edge_color.g, style.edge_color.b, 0);
				else
					gfx_fill_amap(&cproc->common.texture, op->x, y, w, h, 0);
			}
			gfx_render_text_mode(&cproc->common.texture, op->x, y, line, &style, mode);
		}
		y += h + line_space;
		line = next;
	}
	free(text);
	return true;
}

static bool build_gray_filter(struct parts_construction_process *cproc, struct parts_cp_filter *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full_size) {
		x = 0; y = 0;
		w = cproc->common.texture.w;
		h = cproc->common.texture.h;
	}
	if (w <= 0 || h <= 0)
		return true;

	Texture source = {0};
	gfx_init_texture_blank(&source, cproc->common.texture.w, cproc->common.texture.h);
	gfx_copy_with_alpha_map(&source, 0, 0, &cproc->common.texture, 0, 0, source.w, source.h);
	gfx_copy_grayscale(&cproc->common.texture, x, y, &source, x, y, w, h);
	gfx_delete_texture(&source);
	return true;
}

static bool build_add_filter(struct parts_construction_process *cproc, struct parts_cp_filter *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full_size) {
		x = 0; y = 0;
		w = cproc->common.texture.w;
		h = cproc->common.texture.h;
	}

	gfx_fill_add(&cproc->common.texture, x, y, w, h, op->r, op->g, op->b);
	return true;
}

static bool build_mul_filter(struct parts_construction_process *cproc, struct parts_cp_filter *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full_size) {
		x = 0; y = 0;
		w = cproc->common.texture.w;
		h = cproc->common.texture.h;
	}

	gfx_fill_multiply(&cproc->common.texture, x, y, w, h, op->r, op->g, op->b);
	return true;
}

static bool blur_op_is_full_texture(const struct parts_construction_process *cproc,
		const struct parts_cp_filter *op)
{
	if (!cproc->common.texture.handle || op->blur <= 0)
		return false;
	if (op->full_size)
		return true;
	return op->x == 0 && op->y == 0
		&& op->w == cproc->common.texture.w
		&& op->h == cproc->common.texture.h;
}

static bool build_blur_filter_pair(struct parts_construction_process *cproc,
		struct parts_cp_filter *op1, bool vert1,
		struct parts_cp_filter *op2, bool vert2)
{
	if (!cproc->common.texture.handle)
		return false;
	int w = cproc->common.texture.w;
	int h = cproc->common.texture.h;
	if (w <= 0 || h <= 0)
		return true;
	Texture tmp = {0};
	gfx_init_texture_blank(&tmp, w, h);
	gfx_blur_rgb_to_fresh(&tmp, &cproc->common.texture, op1->blur, vert1);
	gfx_blur_rgb_to_fresh(&cproc->common.texture, &tmp, op2->blur, vert2);
	gfx_delete_texture(&tmp);
	return true;
}

static bool build_blur_filter_ex(struct parts_construction_process *cproc, struct parts_cp_filter *op,
		bool vertical, bool preserve_target)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full_size) {
		x = 0; y = 0;
		w = cproc->common.texture.w;
		h = cproc->common.texture.h;
	}
	if (w <= 0 || h <= 0 || op->blur <= 0)
		return true;

	if (!preserve_target && x == 0 && y == 0
			&& w == cproc->common.texture.w && h == cproc->common.texture.h) {
		Texture dst = {0};
		gfx_init_texture_blank(&dst, w, h);
		gfx_blur_rgb_to_fresh(&dst, &cproc->common.texture, op->blur, vertical);
		gfx_delete_texture(&cproc->common.texture);
		cproc->common.texture = dst;
		return true;
	}

	// Sub-rect or format-preserving (RGB pixel-only) blur keeps an immutable copy of
	// the source so sampling never creates a GLES framebuffer feedback loop while
	// preserving outside pixels and the destination texture's GL storage format.
	Texture source = {0};
	gfx_init_texture_blank(&source, cproc->common.texture.w, cproc->common.texture.h);
	gfx_copy_with_alpha_map(&source, 0, 0, &cproc->common.texture, 0, 0, source.w, source.h);
	if (vertical)
		gfx_copy_height_blur(&cproc->common.texture, x, y, &source, x, y, w, h, op->blur);
	else
		gfx_copy_width_blur(&cproc->common.texture, x, y, &source, x, y, w, h, op->blur);
	gfx_delete_texture(&source);
	return true;
}

static bool build_blur_filter(struct parts_construction_process *cproc, struct parts_cp_filter *op, bool vertical)
{
	return build_blur_filter_ex(cproc, op, vertical, false);
}

static inline float cp_seg_dist2(float px, float py, float x0, float y0, float x1, float y1, bool round_cap)
{
	float vx = x1 - x0, vy = y1 - y0;
	float len2 = vx * vx + vy * vy;
	if (len2 < 1e-6f) {
		float dx = px - x0, dy = py - y0;
		return dx * dx + dy * dy;
	}
	float t = ((px - x0) * vx + (py - y0) * vy) / len2;
	if (!round_cap && (t < 0.0f || t > 1.0f))
		return 1e30f;
	if (t < 0.0f) t = 0.0f;
	else if (t > 1.0f) t = 1.0f;
	float dx = px - (x0 + t * vx), dy = py - (y0 + t * vy);
	return dx * dx + dy * dy;
}

static inline bool cp_angle_in_sweep(float px, float py, float cx, float cy, int start_deg, int sweep_deg)
{
	if (sweep_deg >= 360 || sweep_deg <= -360)
		return true;
	if (sweep_deg == 0)
		return false;
	float ang = atan2f(py - cy, px - cx) * (180.0f / (float)M_PI);
	if (ang < 0.0f)
		ang += 360.0f;
	float s = fmodf((float)start_deg, 360.0f);
	if (s < 0.0f)
		s += 360.0f;
	if (sweep_deg > 0) {
		float diff = ang - s;
		if (diff < 0.0f)
			diff += 360.0f;
		return diff <= (float)sweep_deg;
	} else {
		float diff = s - ang;
		if (diff < 0.0f)
			diff += 360.0f;
		return diff <= (float)(-sweep_deg);
	}
}

static inline float cp_rrect_sdf(float lx, float ly, float hw, float hh, float rx, float ry)
{
	float cr = min(min(hw, hh), max(0.0f, max(rx, ry)));
	float qx = fabsf(lx) - (hw - cr);
	float qy = fabsf(ly) - (hh - cr);
	float ox = max(qx, 0.0f), oy = max(qy, 0.0f);
	return hypotf(ox, oy) + min(max(qx, qy), 0.0f) - cr;
}

static inline bool cp_poly_contains(float px, float py, const int *coords, int nr_pts)
{
	bool inside = false;
	for (int i = 0, j = nr_pts - 1; i < nr_pts; j = i++) {
		float xi = (float)coords[i * 2], yi = (float)coords[i * 2 + 1];
		float xj = (float)coords[j * 2], yj = (float)coords[j * 2 + 1];
		if (((yi > py) != (yj > py)) &&
		    (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
			inside = !inside;
	}
	return inside;
}

static bool build_v14(struct parts_construction_process *cproc, struct parts_cp_v14 *op)
{
	Texture *dst = &cproc->common.texture;
	if (!dst->handle)
		return false;
	int cmd = op->command;

	if (cmd == 132) {
		int x = op->full_size ? 0 : op->dx;
		int y = op->full_size ? 0 : op->dy;
		int w = op->full_size ? dst->w : op->dw;
		int h = op->full_size ? dst->h : op->dh;
		if (w > 0 && h > 0) {
			Texture source = {0};
			gfx_init_texture_blank(&source, dst->w, dst->h);
			gfx_copy_with_alpha_map(&source, 0, 0, dst, 0, 0, source.w, source.h);
			gfx_copy_color_reverse(dst, x, y, &source, x, y, w, h);
			gfx_delete_texture(&source);
		}
		return true;
	}

	if (cmd >= 128 && cmd <= 131) {
		Texture *src = cproc_get_cached_texture(op->cg_name);
		if (!src || src->w <= 0 || src->h <= 0)
			return true;
		int sx = max(0, op->sx), sy = max(0, op->sy);
		int sw = op->sw > 0 ? min(op->sw, src->w - sx) : (src->w - sx);
		int sh = op->sh > 0 ? min(op->sh, src->h - sy) : (src->h - sy);
		int dx = op->full_size ? 0 : op->dx;
		int dy = op->full_size ? 0 : op->dy;
		int dw = op->full_size ? dst->w : op->dw;
		int dh = op->full_size ? dst->h : op->dh;
		if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
			return true;
		for (int ty = 0; ty < dh; ty += sh) {
			int ch = min(sh, dh - ty);
			for (int tx = 0; tx < dw; tx += sw) {
				int cw = min(sw, dw - tx);
				if (cmd == 128)
					gfx_blend_amap(dst, dx + tx, dy + ty, src, sx, sy, cw, ch);
				else if (cmd == 129)
					gfx_copy_with_alpha_map(dst, dx + tx, dy + ty, src, sx, sy, cw, ch);
				else if (cmd == 130)
					gfx_blend_alpha(dst, dx + tx, dy + ty, src, sx, sy, cw, ch);
				else
					gfx_copy_amap(dst, dx + tx, dy + ty, src, sx, sy, cw, ch);
			}
		}
		return true;
	}

	// Fast GPU path for axis-aligned unrounded FillRect variants
	if (cmd >= 88 && cmd <= 90 && (op->angle % 360) == 0 && !op->round_corner
			&& op->radius_x <= 0 && op->radius_y <= 0) {
		int x = op->full_size ? 0 : op->dx;
		int y = op->full_size ? 0 : op->dy;
		int w = op->full_size ? dst->w : op->dw;
		int h = op->full_size ? dst->h : op->dh;
		if (w <= 0 || h <= 0)
			return true;
		if (cmd == 88)
			gfx_fill(dst, x, y, w, h, op->r, op->g, op->b);
		else if (cmd == 89)
			gfx_fill_with_alpha(dst, x, y, w, h, op->r, op->g, op->b, op->a);
		else
			gfx_fill_amap(dst, x, y, w, h, op->a);
		return true;
	}

	int mode = 0;
	bool in_rect = false;
	enum {
		SH_LINE, SH_POLYLINE, SH_RECT_STROKE, SH_POLY_STROKE,
		SH_ELLIPSE_STROKE, SH_ARC_STROKE, SH_PIE_STROKE,
		SH_RECT_FILL, SH_POLY_FILL, SH_ELLIPSE_FILL, SH_PIE_FILL
	} kind;

	if (cmd >= 30 && cmd <= 33) { kind = SH_LINE; mode = (cmd - 30) + 1; }
	else if (cmd >= 34 && cmd <= 38) { kind = SH_POLYLINE; mode = cmd - 34; }
	else if (cmd >= 39 && cmd <= 42) { kind = SH_RECT_STROKE; mode = (cmd - 39) + 1; }
	else if (cmd >= 43 && cmd <= 47) { kind = SH_POLY_STROKE; mode = cmd - 43; }
	else if (cmd >= 48 && cmd <= 57) { kind = SH_ELLIPSE_STROKE; mode = (cmd - 48) / 2; in_rect = (cmd - 48) % 2; }
	else if (cmd >= 58 && cmd <= 67) { kind = SH_ELLIPSE_STROKE; mode = (cmd - 58) / 2; in_rect = (cmd - 58) % 2; }
	else if (cmd >= 68 && cmd <= 77) { kind = SH_ARC_STROKE; mode = (cmd - 68) / 2; in_rect = (cmd - 68) % 2; }
	else if (cmd >= 78 && cmd <= 87) { kind = SH_PIE_STROKE; mode = (cmd - 78) / 2; in_rect = (cmd - 78) % 2; }
	else if (cmd >= 88 && cmd <= 92) { kind = SH_RECT_FILL; mode = cmd - 88; }
	else if (cmd >= 93 && cmd <= 97) { kind = SH_POLY_FILL; mode = cmd - 93; }
	else if (cmd >= 98 && cmd <= 107) { kind = SH_ELLIPSE_FILL; mode = (cmd - 98) / 2; in_rect = (cmd - 98) % 2; }
	else if (cmd >= 108 && cmd <= 117) { kind = SH_ELLIPSE_FILL; mode = (cmd - 108) / 2; in_rect = (cmd - 108) % 2; }
	else if (cmd >= 118 && cmd <= 127) { kind = SH_PIE_FILL; mode = (cmd - 118) / 2; in_rect = (cmd - 118) % 2; }
	else {
		WARNING("Unsupported v14 construction command: %d", cmd);
		return true;
	}

	float half_lw = max(1, op->line_width) * 0.5f;
	float half_lw2 = half_lw * half_lw;
	int nr_pts = op->nr_coords / 2;
	float cx = 0.0f, cy = 0.0f, rx = 0.0f, ry = 0.0f;
	float cos_a = 1.0f, sin_a = 0.0f;
	int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;

	if (kind == SH_LINE) {
		int pad = (int)ceilf(half_lw) + 2;
		bx0 = min(op->dx, op->dx2) - pad;
		by0 = min(op->dy, op->dy2) - pad;
		bx1 = max(op->dx, op->dx2) + pad + 1;
		by1 = max(op->dy, op->dy2) + pad + 1;
	} else if (kind == SH_POLYLINE || kind == SH_POLY_STROKE || kind == SH_POLY_FILL) {
		if (nr_pts < (kind == SH_POLYLINE ? 2 : 3))
			return true;
		bx0 = bx1 = op->coords[0];
		by0 = by1 = op->coords[1];
		for (int i = 1; i < nr_pts; i++) {
			int px = op->coords[i * 2], py = op->coords[i * 2 + 1];
			if (px < bx0) bx0 = px;
			if (px > bx1) bx1 = px;
			if (py < by0) by0 = py;
			if (py > by1) by1 = py;
		}
		int pad = (kind == SH_POLY_FILL) ? 2 : ((int)ceilf(half_lw) + 2);
		bx0 -= pad; by0 -= pad; bx1 += pad + 1; by1 += pad + 1;
	} else if (kind == SH_RECT_FILL || kind == SH_RECT_STROKE) {
		float rw = (float)(op->full_size ? dst->w : op->dw);
		float rh = (float)(op->full_size ? dst->h : op->dh);
		if (rw <= 0.0f || rh <= 0.0f)
			return true;
		float rx_pos = (float)(op->full_size ? 0 : op->dx);
		float ry_pos = (float)(op->full_size ? 0 : op->dy);
		cx = rx_pos + rw * 0.5f;
		cy = ry_pos + rh * 0.5f;
		rx = rw * 0.5f;
		ry = rh * 0.5f;
		float rad = -op->angle * ((float)M_PI / 180.0f);
		cos_a = cosf(rad);
		sin_a = sinf(rad);
		float ext_x = fabsf(cos_a) * rx + fabsf(sin_a) * ry + half_lw + 2.0f;
		float ext_y = fabsf(sin_a) * rx + fabsf(cos_a) * ry + half_lw + 2.0f;
		bx0 = (int)floorf(cx - ext_x);
		by0 = (int)floorf(cy - ext_y);
		bx1 = (int)ceilf(cx + ext_x);
		by1 = (int)ceilf(cy + ext_y);
	} else {
		bool is_circle = (cmd >= 48 && cmd <= 57) || (cmd >= 98 && cmd <= 107);
		if (in_rect) {
			int ir_x = op->full_size ? 0 : op->dx;
			int ir_y = op->full_size ? 0 : op->dy;
			int ir_w = op->full_size ? dst->w : op->dw;
			int ir_h = op->full_size ? dst->h : op->dh;
			if (ir_w <= 0 || ir_h <= 0)
				return true;
			cx = ir_x + ir_w * 0.5f;
			cy = ir_y + ir_h * 0.5f;
			rx = ir_w * 0.5f;
			ry = ir_h * 0.5f;
		} else {
			cx = (float)op->dx;
			cy = (float)op->dy;
			rx = (float)op->radius_x;
			ry = is_circle && op->radius_y <= 0 ? rx : (float)op->radius_y;
			if (rx <= 0.0f || ry <= 0.0f)
				return true;
		}
		int pad = (int)ceilf(half_lw) + 2;
		bx0 = (int)floorf(cx - rx) - pad;
		by0 = (int)floorf(cy - ry) - pad;
		bx1 = (int)ceilf(cx + rx) + pad + 1;
		by1 = (int)ceilf(cy + ry) + pad + 1;
	}

	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 > dst->w) bx1 = dst->w;
	if (by1 > dst->h) by1 = dst->h;
	int bw = bx1 - bx0, bh = by1 - by0;
	if (bw <= 0 || bh <= 0)
		return true;

	uint8_t *cov = xcalloc((size_t)bw * bh, 1);
	bool round_cap = op->round_edge || op->line_width <= 2;
	float r_corner = op->round_corner ? (float)max(op->radius_x, op->radius_y) : 0.0f;

	for (int iy = 0; iy < bh; iy++) {
		float base_y = (float)(by0 + iy);
		for (int ix = 0; ix < bw; ix++) {
			float base_x = (float)(bx0 + ix);
			int hits = 0;
			for (int sy_sub = 0; sy_sub < 4; sy_sub++) {
				float py = base_y + (sy_sub + 0.5f) * 0.25f;
				for (int sx_sub = 0; sx_sub < 4; sx_sub++) {
					float px = base_x + (sx_sub + 0.5f) * 0.25f;
					bool hit = false;
					switch (kind) {
					case SH_LINE:
						hit = cp_seg_dist2(px, py, (float)op->dx, (float)op->dy,
								(float)op->dx2, (float)op->dy2, round_cap) <= half_lw2;
						break;
					case SH_POLYLINE:
					case SH_POLY_STROKE: {
						int segs = (kind == SH_POLY_STROKE) ? nr_pts : (nr_pts - 1);
						for (int k = 0; k < segs; k++) {
							int k2 = (k + 1) % nr_pts;
							if (cp_seg_dist2(px, py,
									(float)op->coords[k * 2], (float)op->coords[k * 2 + 1],
									(float)op->coords[k2 * 2], (float)op->coords[k2 * 2 + 1],
									true) <= half_lw2) {
								hit = true;
								break;
							}
						}
						break;
					}
					case SH_POLY_FILL:
						hit = cp_poly_contains(px, py, op->coords, nr_pts);
						break;
					case SH_RECT_FILL:
					case SH_RECT_STROKE: {
						float dx = px - cx, dy = py - cy;
						float lx = dx * cos_a - dy * sin_a;
						float ly = dx * sin_a + dy * cos_a;
						float d = cp_rrect_sdf(lx, ly, rx, ry, r_corner, r_corner);
						if (kind == SH_RECT_FILL)
							hit = (d <= 0.0f);
						else
							hit = (fabsf(d + half_lw) <= half_lw);
						break;
					}
					case SH_ELLIPSE_FILL: {
						float nx = (px - cx) / rx, ny = (py - cy) / ry;
						hit = (nx * nx + ny * ny <= 1.0f);
						break;
					}
					case SH_PIE_FILL: {
						float nx = (px - cx) / rx, ny = (py - cy) / ry;
						hit = (nx * nx + ny * ny <= 1.0f) &&
						      cp_angle_in_sweep(px, py, cx, cy, op->start_angle, op->sweep_angle);
						break;
					}
					case SH_ELLIPSE_STROKE:
					case SH_ARC_STROKE:
					case SH_PIE_STROKE: {
						float ri_x = max(0.01f, rx - half_lw), ri_y = max(0.01f, ry - half_lw);
						float ro_x = rx + half_lw, ro_y = ry + half_lw;
						float ox = (px - cx) / ro_x, oy = (py - cy) / ro_y;
						float ix_n = (px - cx) / ri_x, iy_n = (py - cy) / ri_y;
						bool on_ring = (ox * ox + oy * oy <= 1.0f) &&
						               (rx <= half_lw || ry <= half_lw || (ix_n * ix_n + iy_n * iy_n >= 1.0f));
						if (kind == SH_ELLIPSE_STROKE) {
							hit = on_ring;
						} else {
							bool in_ang = cp_angle_in_sweep(px, py, cx, cy, op->start_angle, op->sweep_angle);
							hit = on_ring && in_ang;
							if (!hit && kind == SH_PIE_STROKE && (ox * ox + oy * oy <= 1.0f) &&
							    abs(op->sweep_angle) < 360) {
								float a0 = op->start_angle * ((float)M_PI / 180.0f);
								float a1 = (op->start_angle + op->sweep_angle) * ((float)M_PI / 180.0f);
								if (cp_seg_dist2(px, py, cx, cy, cx + cosf(a0) * rx, cy + sinf(a0) * ry, true) <= half_lw2 ||
								    cp_seg_dist2(px, py, cx, cy, cx + cosf(a1) * rx, cy + sinf(a1) * ry, true) <= half_lw2)
									hit = true;
							}
						}
						break;
					}
					}
					if (hit)
						hits++;
				}
			}
			cov[(size_t)iy * bw + ix] = (uint8_t)((hits * 255 + 8) / 16);
		}
	}

	SDL_Color col = {
		(uint8_t)min(255, max(0, op->r)),
		(uint8_t)min(255, max(0, op->g)),
		(uint8_t)min(255, max(0, op->b)),
		(uint8_t)min(255, max(0, op->a))
	};
	gfx_draw_shape_mask(dst, bx0, by0, bw, bh, cov, col, mode);
	free(cov);
	return true;
}

bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc)
{
	parts_frame_did_heavy_load = true;
	cproc->needs_build = false;
	parts_common_free_alpha_mask(&cproc->common);
	bool resume_pass = parts_render_pass_suspend();
	glEnable(GL_BLEND);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
	bool ok = false;
	bool is_rgb_tex = false;
	bool preserve_blur_target = false;
	struct parts_cp_op *first_op = TAILQ_FIRST(&cproc->ops);
	if (first_op && (first_op->type == PARTS_CP_CREATE ||
	                 first_op->type == PARTS_CP_CREATE_PIXEL_ONLY ||
	                 first_op->type == PARTS_CP_CG)) {
		cproc->needs_pixel_snapshot = false;
	} else if (first_op && cproc->common.texture.handle) {
		preserve_blur_target = true;
		gfx_begin_draw_target(&cproc->common.texture);
	}
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		switch (op->type) {
		case PARTS_CP_CREATE:
			is_rgb_tex = false;
			preserve_blur_target = false;
			gfx_end_draw_target();
			if (!build_create(parts, cproc, &op->create))
				goto done;
			gfx_begin_draw_target(&cproc->common.texture);
			break;
		case PARTS_CP_CREATE_PIXEL_ONLY:
			gfx_end_draw_target();
			if (!build_create_pixel_only(parts, cproc, &op->create))
				goto done;
			is_rgb_tex = true;
			preserve_blur_target = true;
			gfx_begin_draw_target(&cproc->common.texture);
			break;
		case PARTS_CP_CG:
			is_rgb_tex = false;
			preserve_blur_target = false;
			gfx_end_draw_target();
			if (!build_cg(parts, cproc, &op->cg))
				goto done;
			gfx_begin_draw_target(&cproc->common.texture);
			break;
		case PARTS_CP_FILL:
			if (!build_fill(cproc, &op->fill))
				goto done;
			break;
		case PARTS_CP_FILL_ALPHA_COLOR:
			if (!build_fill_alpha_color(cproc, &op->fill))
				goto done;
			break;
		case PARTS_CP_FILL_AMAP:
			if (!build_fill_amap(cproc, &op->fill))
				goto done;
			break;
		case PARTS_CP_FILL_WITH_ALPHA:
			if (!build_fill_with_alpha(cproc, &op->fill))
				goto done;
			break;
		case PARTS_CP_FILL_GRADATION_HORIZON:
			if (!build_fill_gradation_horizon(cproc, &op->fill))
				goto done;
			break;
		case PARTS_CP_MUL_AMAP_GRADATION_HORIZON:
			if (!build_mul_amap_gradation(cproc, &op->fill, false))
				goto done;
			break;
		case PARTS_CP_MUL_AMAP_GRADATION_VERTICAL:
			if (!build_mul_amap_gradation(cproc, &op->fill, true))
				goto done;
			break;
		case PARTS_CP_DRAW_RECT:
			if (!build_draw_rect(cproc, &op->fill))
				goto done;
			break;
		case PARTS_CP_DRAW_LINE:
			if (!cproc->common.texture.handle)
				goto done;
			gfx_draw_line(&cproc->common.texture, op->line.x0, op->line.y0,
					op->line.x1, op->line.y1, op->line.r, op->line.g, op->line.b);
			gfx_draw_line_to_amap(&cproc->common.texture, op->line.x0, op->line.y0,
					op->line.x1, op->line.y1, op->line.a);
			break;
		case PARTS_CP_DRAW_CUT_CG:
		case PARTS_CP_ALPHA_BLEND_CUT_CG:
			if (!build_draw_cut_cg(cproc, op) && !cproc->common.texture.handle)
				goto done;
			break;
		case PARTS_CP_COPY_CUT_CG:
			if (!build_copy_cut_cg(cproc, &op->cut_cg) && !cproc->common.texture.handle)
				goto done;
			break;
		case PARTS_CP_ONLY_ALPHA_CUT_CG:
			if (!build_only_alpha_cut_cg(cproc, &op->cut_cg) && !cproc->common.texture.handle)
				goto done;
			break;
		case PARTS_CP_BLEND_CG:
			if (!build_blend_cg(cproc, &op->cg) && !cproc->common.texture.handle)
				goto done;
			break;
		case PARTS_CP_DRAW_TEXT:
			if (!build_text(cproc, &op->text, RENDER_BLENDED))
				goto done;
			break;
		case PARTS_CP_COPY_TEXT:
			if (!build_text(cproc, &op->text, RENDER_COPY))
				goto done;
			break;
		case PARTS_CP_ALPHA_BLEND_TEXT:
			if (!build_text(cproc, &op->text, RENDER_BLEND_ALPHA))
				goto done;
			break;
		case PARTS_CP_ONLY_ALPHA_TEXT:
			if (!build_text(cproc, &op->text, RENDER_ALPHA_ONLY))
				goto done;
			break;
		case PARTS_CP_GRAY_FILTER:
			if (!build_gray_filter(cproc, &op->filter))
				goto done;
			break;
		case PARTS_CP_ADD_FILTER:
			if (!build_add_filter(cproc, &op->filter))
				goto done;
			break;
		case PARTS_CP_MUL_FILTER:
			if (!build_mul_filter(cproc, &op->filter))
				goto done;
			break;
		case PARTS_CP_HBLUR_FILTER:
		case PARTS_CP_VBLUR_FILTER: {
			bool vert1 = (op->type == PARTS_CP_VBLUR_FILTER);
			struct parts_cp_op *next_op = TAILQ_NEXT(op, entry);
			if (next_op && (next_op->type == PARTS_CP_HBLUR_FILTER || next_op->type == PARTS_CP_VBLUR_FILTER)
					&& blur_op_is_full_texture(cproc, &op->filter)
					&& blur_op_is_full_texture(cproc, &next_op->filter)) {
				bool vert2 = (next_op->type == PARTS_CP_VBLUR_FILTER);
				if (!build_blur_filter_pair(cproc, &op->filter, vert1, &next_op->filter, vert2))
					goto done;
				op = next_op;
				break;
			}
			GLuint prev_handle = cproc->common.texture.handle;
			if (!preserve_blur_target && blur_op_is_full_texture(cproc, &op->filter))
				gfx_end_draw_target();
			if (!build_blur_filter_ex(cproc, &op->filter, vert1, preserve_blur_target))
				goto done;
			if (cproc->common.texture.handle != prev_handle)
				gfx_begin_draw_target(&cproc->common.texture);
			break;
		}
		case PARTS_CP_V14:
			if (!build_v14(cproc, &op->v14))
				goto done;
			break;
		}
	}
	parts_dirty(parts);
	ok = true;
done:
	gfx_end_draw_target();
	if (ok && is_rgb_tex)
		cproc->common.texture.opaque = true;
	parts_render_pass_resume(resume_pass);
	return ok;
}

static bool cproc_determine_dims(struct parts_construction_process *cproc, int *w_out, int *h_out)
{
	bool found = false;
	int w = 0, h = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		switch (op->type) {
		case PARTS_CP_CREATE:
		case PARTS_CP_CREATE_PIXEL_ONLY:
			w = op->create.w;
			h = op->create.h;
			found = (w > 0 && h > 0);
			break;
		case PARTS_CP_CG: {
			Texture *cached = cproc_get_cached_texture(op->cg.name);
			if (!cached)
				return false;
			w = cached->w;
			h = cached->h;
			found = (w > 0 && h > 0);
			break;
		}
		default:
			break;
		}
	}
	if (!found)
		return false;
	*w_out = w;
	*h_out = h;
	return true;
}

static bool build_explicit_construction_process(struct parts *parts,
		struct parts_construction_process *cproc)
{
	if (game_okusama && parts_v2 && TAILQ_EMPTY(&cproc->ops)) {
		cproc->needs_build = false;
		cproc->needs_pixel_snapshot = false;
		gfx_delete_texture(&cproc->common.texture);
		parts_common_free_alpha_mask(&cproc->common);
		parts_set_dims(parts, &cproc->common, 0, 0);
		parts_dirty(parts);
		return true;
	}
	return parts_build_construction_process(parts, cproc);
}

bool PE_BuildPartsConstructionProcess(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	return build_explicit_construction_process(parts, cproc);
}

bool PE_BuildPartsConstructionProcessThread(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	int w = 0, h = 0;
	if (!cproc_determine_dims(cproc, &w, &h))
		return build_explicit_construction_process(parts, cproc);

	gfx_delete_texture(&cproc->common.texture);
	parts_common_free_alpha_mask(&cproc->common);
	parts_set_dims(parts, &cproc->common, w, h);
	cproc->needs_build = true;
	parts_frame_did_heavy_load = true;
	parts_dirty(parts);
	return true;
}

bool parts_clear_construction_process(struct parts_construction_process *cproc)
{
	cproc->needs_build = false;
	cproc->needs_pixel_snapshot = false;
	while (!TAILQ_EMPTY(&cproc->ops)) {
		struct parts_cp_op *op = TAILQ_FIRST(&cproc->ops);
		TAILQ_REMOVE(&cproc->ops, op, entry);
		parts_cp_op_free(op);
	}
	return true;
}

bool PE_ClearPartsConstructionProcess(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	if (cproc->needs_build)
		parts_build_construction_process(parts, cproc);
	bool keep_snapshot = (cproc->common.texture.handle != 0 || parts->textures_suspended) &&
		(cproc->needs_pixel_snapshot || !TAILQ_EMPTY(&cproc->ops));
	parts_clear_construction_process(cproc);
	cproc->needs_pixel_snapshot = keep_snapshot;
	return true;
}

bool PE_SetPartsConstructionSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	parts_set_surface_area(parts, &cproc->common, x, y, w, h);
	return true;
}
