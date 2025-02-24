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

struct string *parts_text_line_get(struct parts_text_line *line)
{
	struct string *s = make_string("", 0);
	for (int i = 0; i < line->nr_chars; i++) {
		string_append_cstr(&s, line->chars[i].ch, strlen(line->chars[i].ch));
	}
	return s;
}

struct string *parts_text_get(struct parts_text *t)
{
	struct string *s = make_string("", 0);
	for (int i = 0; i < t->nr_lines; i++) {
		if (i > 0)
			string_push_back(&s, '\n');
		struct parts_text_line *line = &t->lines[i];
		for (int i = 0; i < line->nr_chars; i++) {
			string_append_cstr(&s, line->chars[i].ch, strlen(line->chars[i].ch));
		}
	}
	return s;
}

static Point text_style_offset(struct text_style *ts)
{
	int x = max(ts->bold_width, ts->edge_left) * ts->scale_x;
	int y = max(ts->bold_width, ts->edge_up);
	return (Point){x,y};
}

static const char *parts_text_append_char(struct parts_text *t, const char *str)
{
	if (*str == '\n') {
		t->lines = xrealloc_array(t->lines, t->nr_lines, t->nr_lines + 1,
				sizeof(struct parts_text_line));
		t->nr_lines++;
		return str + 1;
	}

	struct parts_text_line *line = &t->lines[t->nr_lines - 1];
	line->chars = xrealloc_array(line->chars, line->nr_chars, line->nr_chars + 1,
			sizeof(struct parts_text_char));
	struct parts_text_char *ch = &line->chars[line->nr_chars++];

	ch->off = text_style_offset(&t->ts);
	int len = extract_sjis_char(str, ch->ch);
	int width = ceilf(text_style_width(&t->ts, ch->ch));
	int height = text_style_height(&t->ts);
	gfx_init_texture_rgba(&ch->t, width, height, (SDL_Color){0,0,0,0});
	ch->advance = gfx_render_textf(&ch->t, 0, 0, ch->ch, &t->ts, false);

	line->width += ch->advance;
	line->height = max(line->height, height);
	return str + len;
}

void parts_text_append(struct parts *parts, struct parts_text *t, struct string *text)
{
	if (!t->nr_lines) {
		t->lines = xcalloc(1, sizeof(struct parts_text_line));
		t->nr_lines = 1;
	}

	const char *msgp = text->text;
	while (*msgp) {
		msgp = parts_text_append_char(t, msgp);
	}

	// calculate dimensions of the text
	float f_width = 0;
	int height = 0;
	for (int i = 0; i < t->nr_lines; i++) {
		f_width = max(f_width, t->lines[i].width);
		height += t->lines[i].height;
	}
	int width = ceilf(f_width);
	if (!width || !height)
		return;

	parts_set_dims(parts, &t->common, width, height);
}

void parts_text_free(struct parts_text *t)
{
	gfx_delete_texture(&t->common.texture);
	for (int i = 0; i < t->nr_lines; i++) {
		struct parts_text_line *line = &t->lines[i];
		for (int i = 0; i < line->nr_chars; i++) {
			gfx_delete_texture(&line->chars[i].t);
		}
		free(line->chars);
	}
	free(t->lines);
}

static void parts_text_clear(struct parts *parts, int state)
{
	struct parts_text *text = parts_get_text(parts, state);
	parts_text_free(text);
	text->lines = NULL;
	text->nr_lines = 0;
	text->cursor.x = 0;
	text->cursor.y = 0;
}

bool PE_SetText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_clear(parts, state);
	parts_text_append(parts, parts_get_text(parts, state), text);
	return true;
}

bool PE_AddPartsText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_append(parts, parts_get_text(parts, state), text);
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

	parts_get_text(parts_get(parts_no), state)->ts.bold_width = bold_weight;
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


