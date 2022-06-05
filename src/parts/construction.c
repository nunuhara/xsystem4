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
#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"

void parts_cp_op_free(struct parts_cp_op *op)
{
	free(op);
}

static bool parts_add_cp_op(int parts_no, struct parts_cp_op *op, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("Invalid parts state: %d", state);
		parts_cp_op_free(op);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	TAILQ_INSERT_TAIL(&parts->states[state].construction_process, op, entry);
	return true;
}

bool PE_AddCreateCGToProcess(int parts_no, struct string *cg_name, int state)
{
	int cg_no = asset_name_to_index(ASSET_CG, cg_name->text);
	if (!cg_no) {
		WARNING("Invalid CG name: %s", cg_name);
		return false;
	}

	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CG;
	op->cg.no = cg_no;

	return parts_add_cp_op(parts_no, op, state);
}

bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int a, int state)
{
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_ALPHA_COLOR;
	op->fill_alpha_color = (struct parts_cp_fill_alpha_color) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	return parts_add_cp_op(parts_no, op, state);
}

static void build_cg(struct parts *parts, struct parts_cp_cg *op, int state)
{
	// TODO: should this allow composing CGs?
	parts_set_cg_by_index(parts, op->no, state);
}

static void build_fill_alpha_color(struct parts *parts, struct parts_cp_fill_alpha_color *op, int state)
{
	NOTICE("FILL ALPHA COLOR");
	struct parts_cg *cg = parts_get_cg(parts, state);
	gfx_fill_alpha_color(&cg->texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b, op->a);
}

bool PE_BuildPartsConstructionProcess(int parts_no, int state)
{
	state--;
	struct parts *parts = parts_get(parts_no);

	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &parts->states[state].construction_process, entry) {
		switch (op->type) {
		case PARTS_CP_CG:
			build_cg(parts, &op->cg, state);
			break;
		case PARTS_CP_FILL_ALPHA_COLOR:
			build_fill_alpha_color(parts, &op->fill_alpha_color, state);
			break;
		}
	}
	return true;
}
