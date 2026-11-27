/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdbool.h>
#include <stdlib.h>

#include "system4/ain.h"
#include "system4/flat.h"

#include "parts.h"
#include "parts_internal.h"
#include "vm.h"
#include "xsystem4.h"

void PE_SetComponentMargin(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_get(parts_no);
	parts->margin_top = top;
	parts->margin_bottom = bottom;
	parts->margin_left = left;
	parts->margin_right = right;
}

int PE_GetComponentMarginTop(int parts_no)
{
	return parts_get(parts_no)->margin_top;
}

int PE_GetComponentMarginBottom(int parts_no)
{
	return parts_get(parts_no)->margin_bottom;
}

int PE_GetComponentMarginLeft(int parts_no)
{
	return parts_get(parts_no)->margin_left;
}

int PE_GetComponentMarginRight(int parts_no)
{
	return parts_get(parts_no)->margin_right;
}

void PE_SetLayoutBoxLayoutType(int parts_no, int type)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->layout_type != type) {
		lb->layout_type = type;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxLayoutType(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return PARTS_LAYOUT_FREE;
	return parts->states[0].layout_box.layout_type;
}

void PE_SetLayoutBoxReturn(int parts_no, bool return_flag, int return_size)
{
	PE_SetLayoutBoxReturnF(parts_no, return_flag, return_size);
}

void PE_SetLayoutBoxReturnF(int parts_no, bool return_flag, float return_size)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	lb->wrap = return_flag;
	lb->wrap_size = return_size;
	parts_component_dirty(parts);
}

bool PE_IsLayoutBoxReturn(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return false;
	return parts->states[0].layout_box.wrap;
}

int PE_GetLayoutBoxReturnSize(int parts_no)
{
	return PE_GetLayoutBoxReturnSizeF(parts_no);
}

float PE_GetLayoutBoxReturnSizeF(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.wrap_size;
}

void PE_SetLayoutBoxReturnSizeForRate(int parts_no, bool size_for_rate)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->wrap_size_is_rate != size_for_rate) {
		lb->wrap_size_is_rate = size_for_rate;
		parts_component_dirty(parts);
	}
}

bool PE_IsLayoutBoxReturnSizeForRate(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return false;
	return parts->states[0].layout_box.wrap_size_is_rate;
}

static float layout_wrap_limit(struct parts_layout_box *lb, bool horizontal)
{
	// Older PACT data (e.g. Tsumamigui 3) sets the rate flag on pixel sizes such as 870.
	if (!lb->wrap_size_is_rate || lb->wrap_size > 1.0f)
		return lb->wrap_size;
	int view = horizontal
		? (config.view_width > 0 ? config.view_width : 1280)
		: (config.view_height > 0 ? config.view_height : 720);
	return lb->wrap_size * view;
}

void PE_SetLayoutBoxAlign(int parts_no, int align)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->align != align) {
		lb->align = align;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxAlign(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.align;
}

void PE_set_layoutbox_padding(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	lb->padding_top = top;
	lb->padding_bottom = bottom;
	lb->padding_left = left;
	lb->padding_right = right;
	parts_component_dirty(parts);
}

int PE_get_layoutbox_padding_top(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_top;
}

int PE_get_layoutbox_padding_bottom(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_bottom;
}

int PE_get_layoutbox_padding_left(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_left;
}

int PE_get_layoutbox_padding_right(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_right;
}

static void parts_layout_calc_size_horizontal(struct parts_layout_box *lb, struct parts *parts,
		bool cached, int *out_w, int *out_h);
static void parts_layout_calc_size_vertical(struct parts_layout_box *lb, struct parts *parts,
		bool cached, int *out_w, int *out_h);

static void parts_get_child_layout_size(struct parts *child, bool cached, int *w, int *h)
{
	if (!cached) {
		parts_get_layout_size(child, w, h);
		return;
	}
	struct parts_state *state = &child->states[child->state];
	if (parts_v2 && child->widget) {
		parts_widget_get_size(child, w, h);
		return;
	}
	if (state->type == PARTS_FLAT && state->flat.flat) {
		*w = state->flat.flat->hdr.width;
		*h = state->flat.flat->hdr.height;
	} else {
		*w = state->common.w;
		*h = state->common.h;
	}
}

static void parts_get_layout_size_impl(struct parts *parts, bool cached, int *w, int *h)
{
	struct parts_state *state = &parts->states[parts->state];
	if (parts_v2) {
		if (parts->widget) {
			parts_widget_get_size(parts, w, h);
			return;
		}
		if (ain->version >= 11 && parts->component_type == 15) {
			// A user component takes its size from its first child (the activity root).
			struct parts *child = TAILQ_FIRST(&parts->children);
			if (child) parts_get_child_layout_size(child, cached, w, h);
			else *w = *h = 0;
			return;
		}
		if (state->type == PARTS_LAYOUT_BOX) {
			struct parts_layout_box *lb = &state->layout_box;
			if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL)
				parts_layout_calc_size_horizontal(lb, parts, cached, w, h);
			else if (lb->layout_type == PARTS_LAYOUT_VERTICAL)
				parts_layout_calc_size_vertical(lb, parts, cached, w, h);
			else {
				*w = *h = 0;
				struct parts *child;
				PARTS_FOREACH_CHILD(child, parts) {
					if (!child->local.show) continue;
					int cw, ch;
					parts_get_child_layout_size(child, cached, &cw, &ch);
					struct parts_common *c = &child->states[child->state].common;
					SDL_FPoint pos = parts_local_position(child);
					*w = max(*w, pos.x + c->origin_offset.x + cw);
					*h = max(*h, pos.y + c->origin_offset.y + ch);
				}
			}
			return;
		}
	}
	if (state->type == PARTS_FLAT && state->flat.flat) {
		// The layout size of Flat is specified in the header, and this may
		// differ from the hitbox size.
		*w = state->flat.flat->hdr.width;
		*h = state->flat.flat->hdr.height;
	} else {
		*w = state->common.w;
		*h = state->common.h;
	}
}

void parts_get_layout_size(struct parts *parts, int *w, int *h)
{
	parts_get_layout_size_impl(parts, false, w, h);
}

static int align_offset_x(int align, int total_w)
{
	switch (align) {
	case 2: case 5: case 8: return -(total_w / 2);
	case 3: case 6: case 9: return -total_w;
	default: return 0;
	}
}

static int align_offset_y(int align, int total_h)
{
	switch (align) {
	case 4: case 5: case 6: return -(total_h / 2);
	case 7: case 8: case 9: return -total_h;
	default: return 0;
	}
}

// Calculates the total size of a horizontal layout box by summing child widths per row.
static void parts_layout_calc_size_horizontal(struct parts_layout_box *lb, struct parts *parts,
		bool cached, int *out_w, int *out_h)
{
	int row_w = 0, max_row_w = 0, max_row_h = 0, total_h = 0;
	float wrap_limit = layout_wrap_limit(lb, true);
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		if (parts_v2 && !child->local.show) continue;
		int child_w, child_h;
		parts_get_child_layout_size(child, cached, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && row_w + cw > wrap_limit && row_w > 0) {
			if (row_w > max_row_w)
				max_row_w = row_w;
			total_h += max_row_h;
			row_w = 0;
			max_row_h = 0;
		}
		row_w += cw;
		if (ch > max_row_h)
			max_row_h = ch;
	}
	if (row_w > max_row_w)
		max_row_w = row_w;
	total_h += max_row_h;
	*out_w = max_row_w + lb->padding_left + lb->padding_right;
	*out_h = total_h + lb->padding_top + lb->padding_bottom;
}

// Calculates the total size of a vertical layout box by summing child heights per column.
static void parts_layout_calc_size_vertical(struct parts_layout_box *lb, struct parts *parts,
		bool cached, int *out_w, int *out_h)
{
	int col_h = 0, max_col_h = 0, max_col_w = 0, total_w = 0;
	float wrap_limit = layout_wrap_limit(lb, false);
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		if (parts_v2 && !child->local.show) continue;
		int child_w, child_h;
		parts_get_child_layout_size(child, cached, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && col_h + ch > wrap_limit && col_h > 0) {
			if (col_h > max_col_h)
				max_col_h = col_h;
			total_w += max_col_w;
			col_h = 0;
			max_col_w = 0;
		}
		col_h += ch;
		if (cw > max_col_w)
			max_col_w = cw;
	}
	if (col_h > max_col_h)
		max_col_h = col_h;
	total_w += max_col_w;
	*out_w = total_w + lb->padding_left + lb->padding_right;
	*out_h = max_col_h + lb->padding_top + lb->padding_bottom;
}

static void parts_do_layout_impl(struct parts *parts, bool cached)
{
	if (parts->states[0].type != PARTS_LAYOUT_BOX)
		return;
	struct parts_layout_box *lb = &parts->states[0].layout_box;
	int total_w = 0, total_h = 0;
	if (parts_v2) {
		parts_get_layout_size_impl(parts, cached, &total_w, &total_h);
		if (lb->common.w != total_w || lb->common.h != total_h)
			parts_set_dims(parts, &lb->common, total_w, total_h);
	}
	if (lb->layout_type == PARTS_LAYOUT_FREE)
		return;

	if (!parts_v2) {
		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL)
			parts_layout_calc_size_horizontal(lb, parts, cached, &total_w, &total_h);
		else
			parts_layout_calc_size_vertical(lb, parts, cached, &total_w, &total_h);
	}

	int align = lb->align;
	bool is_left   = (align == 1 || align == 4 || align == 7);
	bool is_right  = (align == 3 || align == 6 || align == 9);
	bool is_top    = (align == 1 || align == 2 || align == 3);
	bool is_bottom = (align == 7 || align == 8 || align == 9);

	int origin_x = align_offset_x(parts->origin_mode, total_w);
	int origin_y = align_offset_y(parts->origin_mode, total_h);
	int align_x = align_offset_x(align, total_w);
	int align_y = align_offset_y(align, total_h);

	int start_x = (is_left ? lb->padding_left : -lb->padding_right)
		+ (origin_x - align_x);
	int start_y = (is_top ? lb->padding_top : -lb->padding_bottom)
		+ (origin_y - align_y);

	int x_dir = is_right ? -1 : 1;
	int y_dir = is_bottom ? -1 : 1;

	int cur_x = start_x;
	int cur_y = start_y;
	int max_cross = 0;
	float wrap_limit = layout_wrap_limit(lb, lb->layout_type == PARTS_LAYOUT_HORIZONTAL);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_child_layout_size(child, cached, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (parts_v2 && !child->local.show) cw = ch = 0;

		// The layout box forces the child's origin_mode to match its own align.
		if (child->origin_mode != align) {
			child->origin_mode = align;
			parts_recalculate_hitbox(child);
		}

		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
			if (lb->wrap && abs((cur_x + cw * x_dir) - start_x) > wrap_limit
					&& max_cross > 0) {
				cur_y += max_cross * y_dir;
				max_cross = 0;
				cur_x = start_x;
			}
		} else {
			if (lb->wrap && abs((cur_y + ch * y_dir) - start_y) > wrap_limit
					&& max_cross > 0) {
				cur_x += max_cross * x_dir;
				max_cross = 0;
				cur_y = start_y;
			}
		}

		// margin offset depends on alignment direction
		int mx = is_left ? child->margin_left : is_right ? -child->margin_right : 0;
		int my = is_top ? child->margin_top : is_bottom ? -child->margin_bottom : 0;

		int pos_x = cur_x + mx;
		int pos_y = cur_y + my;
		if (child->local.pos.x != pos_x || child->local.pos.y != pos_y) {
			child->local.pos.x = pos_x;
			child->local.pos.y = pos_y;
			parts_recalculate_hitbox(child);
			parts_component_dirty(child);
		}

		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
			cur_x += cw * x_dir;
			if (ch > max_cross)
				max_cross = ch;
		} else {
			cur_y += ch * y_dir;
			if (cw > max_cross)
				max_cross = cw;
		}
	}
}

void parts_do_layout(struct parts *parts)
{
	parts_do_layout_impl(parts, false);
}

void parts_update_layout_tree(struct parts *parts)
{
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) parts_update_layout_tree(child);
	parts_do_layout_impl(parts, true);
	if (ain->version >= 11 && parts->component_type == 15) {
		int w, h;
		parts_get_layout_size_impl(parts, true, &w, &h);
		for (int i = 0; i < PARTS_NR_STATES; i++) {
			if (parts->states[i].common.w != w || parts->states[i].common.h != h)
				parts_set_dims(parts, &parts->states[i].common, w, h);
		}
	}
}
