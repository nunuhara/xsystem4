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

#include "system4.h"
#include "system4/string.h"

#include "input.h"
#include "parts.h"
#include "parts_internal.h"

struct parts_checkbox *parts_get_checkbox(struct parts *parts)
{
	if (parts->type == PARTS_TYPE_CHECKBOX)
		return parts->widget.checkbox;
	parts_type_reset(parts, PARTS_TYPE_CHECKBOX, PARTS_SPRITE_CONSTRUCTION_PROCESS);
	struct parts_checkbox *c = parts->widget.checkbox = xcalloc(1, sizeof(*c));
	parts_button_style_init(&c->style, 16, 16);
	c->style.font.color = (SDL_Color) {255, 255, 255, 255};
	c->style.text_align = PARTS_ALIGN_MIDDLE_LEFT;
	parts_checkbox_rebuild(parts);
	return c;
}

void parts_checkbox_free(struct parts_checkbox *c)
{
	parts_button_style_free(&c->style);
	free(c);
}

static void checkbox_draw_mark(Texture *t, bool pressed)
{
	int x = t->w / 5 + pressed, y = t->h / 5 + pressed;
	int w = t->w * 3 / 5, h = t->h * 3 / 5;
	if (w < 3 || h < 3)
		return;
	int bend = max(w / 3, 1);
	for (int i = 0; i < w; i++) {
		int top = i < bend ? h / 2 + i * (h / 2) / bend
			: h - 1 - (i - bend) * (h - 1) / max(w - bend - 1, 1);
		gfx_draw_line(t, x + i, y + top, x + i, y + min(top + max(h / 5, 1), h - 1), 0, 0, 0);
	}
}

// Extend the box texture of each state to the right and draw the label there.
static void checkbox_draw_label(struct parts *parts)
{
	struct parts_checkbox *cb = parts->widget.checkbox;
	if (cb->button_mode || !cb->style.text->size)
		return;
	int tw, th, line_h;
	parts_button_style_measure_text(&cb->style, &tw, &th, &line_h);
	if (tw <= 0 || th <= 0)
		return;
	int bw = parts->sprites[0].common.w, bh = parts->sprites[0].common.h;
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		struct parts_common *c = &parts->sprites[i].common;
		if (parts->sprites[i].type == PARTS_SPRITE_FLAT)
			continue;
		Texture t;
		int w = bw + tw, h = max(bh, th);
		gfx_init_texture_blank(&t, w, h);
		if (c->texture.handle) {
			gfx_copy_with_alpha_map(&t, 0, (h - bh) / 2, &c->texture, 0, 0, c->w, c->h);
			gfx_delete_texture(&c->texture);
		}
		// Blended text preserves destination alpha; the label area starts
		// transparent, so copy the glyph coverage as well as its color.
		parts_button_style_draw_text(&t, &cb->style, bw, (h - th) / 2, line_h, false);
		c->texture = t;
		parts_set_dims(parts, c, w, h);
	}
}

void parts_checkbox_rebuild(struct parts *parts)
{
	static const char *checked_infix = "／チェック";
	static bool converted;
	if (!converted) {
		parts_utf2sjis_array(&checked_infix, 1);
		converted = true;
	}
	struct parts_checkbox *cb = parts->widget.checkbox;
	const char *infix = cb->checked ? checked_infix : "";
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		struct parts_common *c = &parts->sprites[i].common;
		switch (parts_button_style_load(parts, i, &cb->style, infix, i)) {
		case PARTS_BUTTON_FLAT:
			break;
		case PARTS_BUTTON_CG:
			if (!cb->checked && i == PARTS_STATE_DEFAULT) {
				cb->style.w = c->w;
				cb->style.h = c->h;
			}
			break;
		case PARTS_BUTTON_GENERATED:
			parts_button_style_draw_background(parts, i, &cb->style, i);
			if (!c->texture.handle)
				break;
			if (cb->checked && !cb->button_mode)
				checkbox_draw_mark(&c->texture, i == PARTS_STATE_CLICKED);
			if (cb->button_mode)
				parts_button_style_draw_label(&c->texture, &cb->style,
						i == PARTS_STATE_CLICKED);
			break;
		}
	}
	checkbox_draw_label(parts);
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

void parts_checkbox_set_checked(struct parts *parts, bool checked)
{
	struct parts_checkbox *cb = parts_get_checkbox(parts);
	bool old = cb->checked;
	if (old == checked)
		return;
	cb->checked = checked;
	parts_checkbox_rebuild(parts);
	// Radio selection runs before the changed event, and can change the
	// checked state again.
	parts_radiobox_member_changed(parts, checked);
	if (old != cb->checked)
		parts_msg_push(parts, PARTS_MSG_CHANGED, "b", checked);
}

void parts_checkbox_click(struct parts *parts, Point pos)
{
	parts_checkbox_set_checked(parts, !parts->widget.checkbox->checked);
	parts_msg_push(parts, PARTS_MSG_MOUSE_CLICK, "iii",
			pos.x - parts->global.pos.x, pos.y - parts->global.pos.y, VK_LBUTTON);
}

void PE_CheckBoxChecked(int parts_no, bool checked)
{
	parts_checkbox_set_checked(parts_get(parts_no), !!checked);
}

bool PE_IsCheckBoxChecked(int parts_no)
{
	return parts_get_checkbox(parts_get(parts_no))->checked;
}

void PE_SetCheckBoxColor(int parts_no, int r, int g, int b)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_checkbox *cb = parts_get_checkbox(parts);
	if (cb->style.r == r && cb->style.g == g && cb->style.b == b)
		return;
	cb->style.r = r;
	cb->style.g = g;
	cb->style.b = b;
	parts_checkbox_rebuild(parts);
}
