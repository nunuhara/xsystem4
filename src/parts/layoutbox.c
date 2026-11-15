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

#include "system4.h"
#include "system4/flat.h"

#include "parts.h"
#include "parts_internal.h"

struct parts_layoutbox *parts_get_layoutbox(struct parts *parts)
{
	if (parts->type == PARTS_TYPE_LAYOUTBOX)
		return parts->widget.layoutbox;
	parts_type_reset(parts, PARTS_TYPE_LAYOUTBOX, PARTS_SPRITE_NONE);
	struct parts_layoutbox *lb = parts->widget.layoutbox = xcalloc(1, sizeof(*lb));
	lb->layout_type = PARTS_LAYOUT_VERTICAL;
	lb->align = PARTS_ALIGN_TOP_LEFT;
	return lb;
}

static struct parts_layoutbox *find_layoutbox(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->type != PARTS_TYPE_LAYOUTBOX)
		return NULL;
	return parts->widget.layoutbox;
}

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
	struct parts_layoutbox *lb = parts_get_layoutbox(parts);
	if (lb->layout_type != type) {
		lb->layout_type = type;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxLayoutType(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->layout_type : PARTS_LAYOUT_FREE;
}

void PE_SetLayoutBoxReturn(int parts_no, bool return_flag, int return_size)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layoutbox *lb = parts_get_layoutbox(parts);
	lb->wrap = return_flag;
	lb->wrap_size = return_size;
	parts_component_dirty(parts);
}

bool PE_IsLayoutBoxReturn(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->wrap : false;
}

int PE_GetLayoutBoxReturnSize(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->wrap_size : 0;
}

void PE_SetLayoutBoxAlign(int parts_no, int align)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layoutbox *lb = parts_get_layoutbox(parts);
	if (lb->align != align) {
		lb->align = align;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxAlign(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->align : 0;
}

void PE_set_layoutbox_padding(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layoutbox *lb = parts_get_layoutbox(parts);
	lb->padding_top = top;
	lb->padding_bottom = bottom;
	lb->padding_left = left;
	lb->padding_right = right;
	parts_component_dirty(parts);
}

int PE_get_layoutbox_padding_top(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->padding_top : 0;
}

int PE_get_layoutbox_padding_bottom(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->padding_bottom : 0;
}

int PE_get_layoutbox_padding_left(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->padding_left : 0;
}

int PE_get_layoutbox_padding_right(int parts_no)
{
	struct parts_layoutbox *lb = find_layoutbox(parts_no);
	return lb ? lb->padding_right : 0;
}

static void parts_get_layout_size(struct parts *parts, int *w, int *h)
{
	struct parts_sprite *sprite = &parts->sprites[parts->state];
	if (sprite->type == PARTS_SPRITE_FLAT && sprite->flat.flat) {
		// The layout size of Flat is specified in the header, and this may
		// differ from the hitbox size.
		*w = sprite->flat.flat->hdr.width;
		*h = sprite->flat.flat->hdr.height;
	} else {
		*w = sprite->common.w;
		*h = sprite->common.h;
	}
}

// Calculates the total size of a horizontal layout box by summing child widths per row.
static void parts_layout_calc_size_horizontal(struct parts_layoutbox *lb, struct parts *parts,
		int *out_w, int *out_h)
{
	int row_w = 0, max_row_w = 0, max_row_h = 0, total_h = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && row_w + cw > lb->wrap_size && row_w > 0) {
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
static void parts_layout_calc_size_vertical(struct parts_layoutbox *lb, struct parts *parts,
		int *out_w, int *out_h)
{
	int col_h = 0, max_col_h = 0, max_col_w = 0, total_w = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && col_h + ch > lb->wrap_size && col_h > 0) {
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

void parts_do_layout(struct parts *parts)
{
	if (parts->type != PARTS_TYPE_LAYOUTBOX)
		return;
	struct parts_layoutbox *lb = parts->widget.layoutbox;
	if (lb->layout_type == PARTS_LAYOUT_FREE)
		return;

	int total_w, total_h;
	if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL)
		parts_layout_calc_size_horizontal(lb, parts, &total_w, &total_h);
	else
		parts_layout_calc_size_vertical(lb, parts, &total_w, &total_h);

	int align = lb->align;
	bool is_left   = (align == 1 || align == 4 || align == 7);
	bool is_right  = (align == 3 || align == 6 || align == 9);
	bool is_top    = (align == 1 || align == 2 || align == 3);
	bool is_bottom = (align == 7 || align == 8 || align == 9);

	int start_x = (is_left ? lb->padding_left : -lb->padding_right)
		+ parts_align_x(align, total_w) - parts_align_x(parts->origin_mode, total_w);
	int start_y = (is_top ? lb->padding_top : -lb->padding_bottom)
		+ parts_align_y(align, total_h) - parts_align_y(parts->origin_mode, total_h);

	int x_dir = is_right ? -1 : 1;
	int y_dir = is_bottom ? -1 : 1;

	int cur_x = start_x;
	int cur_y = start_y;
	int max_cross = 0;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;

		// The layout box forces the child's origin_mode to match its own align.
		if (child->origin_mode != align) {
			child->origin_mode = align;
			parts_recalculate_hitbox(child);
		}

		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
			if (lb->wrap && abs((cur_x + cw * x_dir) - start_x) > lb->wrap_size
					&& max_cross > 0) {
				cur_y += max_cross * y_dir;
				max_cross = 0;
				cur_x = start_x;
			}
		} else {
			if (lb->wrap && abs((cur_y + ch * y_dir) - start_y) > lb->wrap_size
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
