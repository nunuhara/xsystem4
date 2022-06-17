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
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "parts_internal.h"

static int extract_sjis_char(const char *src, char *dst)
{
	if (SJIS_2BYTE(*src)) {
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = '\0';
		return 2;
	}
	dst[0] = src[0];
	dst[1] = '\0';
	return 1;
}

static void parts_text_newline(struct parts_text *text)
{
	const unsigned height = text->lines[text->nr_lines-1].height;
	text->cursor = POINT(0, text->cursor.y + height + text->line_space);
	text->lines = xrealloc_array(text->lines, text->nr_lines, text->nr_lines+1, sizeof(struct parts_text_line));
	text->nr_lines++;
}

static void parts_text_append(struct parts *parts, struct string *text, int state)
{
	struct parts_text *t = parts_get_text(parts, state);

	if (!t->common.texture.handle) {
		gfx_init_texture_rgba(&t->common.texture, config.view_width, config.view_height,
				      (SDL_Color){ 0, 0, 0, 0 });
	}

	if (!t->nr_lines) {
		t->lines = xcalloc(1, sizeof(struct parts_text_line));
		t->lines[0].height = 0;
		t->nr_lines = 1;
	}

	const char *msgp = text->text;
	while (*msgp) {
		char c[4];
		int len = extract_sjis_char(msgp, c);
		msgp += len;

		if (c[0] == '\n') {
			parts_text_newline(t);
			continue;
		}

		t->cursor.x += gfx_render_text(&t->common.texture, t->cursor.x, t->cursor.y, c, &t->ts);

		const unsigned old_height = t->lines[t->nr_lines-1].height;
		const unsigned new_height = t->ts.size;
		t->lines[t->nr_lines-1].height = max(old_height, new_height);
	}
	parts_set_dims(parts, &t->common, t->cursor.x, t->cursor.y + t->lines[t->nr_lines-1].height);
}

static void parts_text_clear(struct parts *parts, int state)
{
	struct parts_text *text = parts_get_text(parts, state);
	free(text->lines);
	text->lines = NULL;
	text->nr_lines = 0;
	text->cursor.x = 0;
	text->cursor.y = 0;
	gfx_delete_texture(&text->common.texture);
}

bool PE_SetText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_clear(parts, state);
	parts_text_append(parts, text, state);
	return true;
}

bool PE_AddPartsText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_append(parts, text, state);
	return true;
}

bool PE_SetPartsTextSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	parts_set_surface_area(parts, &text->common, x, y, w, h);
	return true;
}

//bool PE_DeletePartsTopTextLine(int PartsNumber, int State);

bool PE_SetFont(int parts_no, int type, int size, int r, int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_text *text = parts_get_text(parts_get(parts_no), state);
	text->ts.face = type;
	text->ts.size = size;
	text->ts.color = (SDL_Color) { r, g, b, 255 };
	text->ts.weight = bold_weight * 1000;
	text->ts.edge_color = (SDL_Color) { edge_r, edge_g, edge_b, 255 };
	text_style_set_edge_width(&text->ts, edge_weight);
	return true;
}

bool PE_SetPartsFontType(int parts_no, int type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	parts_get_text(parts_get(parts_no), state)->ts.face = type;
	return true;
}

bool PE_SetPartsFontSize(int parts_no, int size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	parts_get_text(parts_get(parts_no), state)->ts.size = size;
	return true;
}

bool PE_SetPartsFontColor(int parts_no, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	parts_get_text(parts_get(parts_no), state)->ts.color = (SDL_Color) { r, g, b, 255 };
	return true;
}

bool PE_SetPartsFontBoldWeight(int parts_no, float bold_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	parts_get_text(parts_get(parts_no), state)->ts.weight = bold_weight * 1000;
	return true;
}

bool PE_SetPartsFontEdgeColor(int parts_no, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	parts_get_text(parts_get(parts_no), state)->ts.edge_color = (SDL_Color) { r, g, b, 255 };
	return true;
}

bool PE_SetPartsFontEdgeWeight(int parts_no, float edge_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_text *text = parts_get_text(parts_get(parts_no), state);
	text_style_set_edge_width(&text->ts, edge_weight);
	return true;
}

bool PE_SetTextCharSpace(int parts_no, int char_space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	parts_get_text(parts_get(parts_no), state)->ts.font_spacing = char_space;
	return true;
}

bool PE_SetTextLineSpace(int parts_no, int line_space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	parts_get_text(parts_get(parts_no), state)->line_space = line_space;
	return true;
}


