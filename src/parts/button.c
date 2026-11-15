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

#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"
#include "xsystem4.h"

#define LABEL_MARGIN 3

void parts_button_style_init(struct parts_button_style *s, int w, int h)
{
	*s = (struct parts_button_style) {
		.w = w, .h = h,
		.r = 200, .g = 200, .b = 200,
		.font = {
			.face = FONT_GOTHIC, .size = 16,
			.color = {0, 0, 0, 255}, .edge_color = {0, 0, 0, 255},
			.scale_x = 1, .space_scale_x = 1,
		},
		.cg_name = string_ref(&EMPTY_STRING),
		.flat_name = string_ref(&EMPTY_STRING),
		.text = string_ref(&EMPTY_STRING),
		.text_align = PARTS_ALIGN_CENTER,
	};
}

void parts_button_style_free(struct parts_button_style *s)
{
	free_string(s->cg_name);
	free_string(s->flat_name);
	free_string(s->text);
}

// Load the flat or CG named "<base name><`infix`><suffix of `look`>" into
// the sprite of `state`. If neither exists, reset the sprite to an empty
// construction process.
enum parts_button_resource parts_button_style_load(struct parts *parts, int state,
		struct parts_button_style *s, const char *infix, int look)
{
	const char *suffix = parts_widget_look_suffix(look);
	struct string *flat = parts_widget_resource_name(s->flat_name, infix, suffix);
	if (asset_exists_by_name(ASSET_FLAT, flat->text, NULL)) {
		parts_sprite_reset(&parts->sprites[state], PARTS_SPRITE_FLAT);
		parts_flat_load(parts, &parts->sprites[state].flat, flat);
		free_string(flat);
		return PARTS_BUTTON_FLAT;
	}
	free_string(flat);
	struct string *name = parts_widget_resource_name(s->cg_name, infix, suffix);
	struct cg *cg = asset_cg_load_by_name(name->text, NULL);
	free_string(name);
	parts_sprite_reset(&parts->sprites[state], PARTS_SPRITE_CONSTRUCTION_PROCESS);
	if (!cg)
		return PARTS_BUTTON_GENERATED;
	struct parts_common *c = &parts->sprites[state].common;
	gfx_init_texture_with_cg(&c->texture, cg);
	parts_set_dims(parts, c, cg->metrics.w, cg->metrics.h);
	cg_free(cg);
	return PARTS_BUTTON_CG;
}

static SDL_Color button_color(struct parts_button_style *s, int offset)
{
	return (SDL_Color) {
		min(max(s->r + offset, 0), 255), min(max(s->g + offset, 0), 255),
		min(max(s->b + offset, 0), 255), 255
	};
}

// The original engine fills the background with a vertical gradation from
// the top color to the bottom color (construction command
// FillGradationHorizon). This fills it with one color between the two.
static void button_draw_background(Texture *t, struct parts_button_style *s, int look)
{
	int top = 10, bottom = -10, light = 40, dark = -90;
	if (look == PARTS_LOOK_DISABLED) {
		top = 40; bottom = 30; light = 30; dark = 40;
	} else if (look == PARTS_STATE_HOVERED) {
		top = 40; bottom = 20;
	} else if (look == PARTS_STATE_CLICKED) {
		top = 0; bottom = -20; light = -50; dark = 40;
	}
	gfx_init_texture_rgba(t, s->w, s->h, button_color(s, (top + bottom) / 2));
	if (s->w <= 7 || s->h <= 7)
		return;
	SDL_Color c1 = button_color(s, light);
	SDL_Color c2 = button_color(s, dark);
	gfx_fill(t, 0, 0, s->w, 2, c1.r, c1.g, c1.b);
	gfx_fill(t, 0, s->h - 2, s->w, 2, c2.r, c2.g, c2.b);
	gfx_draw_line(t, 0, 2, 0, s->h - 2, c1.r, c1.g, c1.b);
	gfx_draw_line(t, 1, 2, 1, s->h - 3, c1.r, c1.g, c1.b);
	gfx_draw_line(t, s->w - 2, 2, s->w - 2, s->h - 3, c2.r, c2.g, c2.b);
	gfx_draw_line(t, s->w - 1, 1, s->w - 1, s->h - 3, c2.r, c2.g, c2.b);
}

void parts_button_style_draw_background(struct parts *parts, int state,
		struct parts_button_style *s, int look)
{
	struct parts_common *c = &parts->sprites[state].common;
	if (s->w <= 0 || s->h <= 0) {
		parts_set_dims(parts, c, 0, 0);
		return;
	}
	button_draw_background(&c->texture, s, look);
	parts_set_dims(parts, c, s->w, s->h);
}

void parts_button_style_measure_text(struct parts_button_style *s, int *width, int *height,
		int *line_height)
{
	int font_size = s->font.size;
	int decoration = min(font_size, (int)ceilf(max(s->font.bold_width, s->font.edge_up)));
	int line_h = font_size + (font_size & 1) + decoration * 2;
	int w = 0, h = 0;
	char *text = xstrdup(s->text->text);
	for (char *line = text; line; ) {
		char *end = strchr(line, '\n');
		if (end)
			*end = '\0';
		w = max(w, (int)ceilf(gfx_size_text(&s->font, line)));
		h += line_h + s->line_space;
		line = end ? end + 1 : NULL;
	}
	free(text);
	// The renderer rounds the origin of each glyph.
	*width = w > 0 ? w + 1 : 0;
	*height = max(h - s->line_space, 0);
	*line_height = line_h;
}

void parts_button_style_draw_text(Texture *t, struct parts_button_style *s, int x, int y,
		int line_height, bool blend)
{
	char *text = xstrdup(s->text->text);
	char *line = text;
	for (;;) {
		char *end = strchr(line, '\n');
		if (end)
			*end = 0;
		gfx_render_text(t, x, y, line, &s->font, blend);
		if (!end)
			break;
		line = end + 1;
		y += line_height + s->line_space;
	}
	free(text);
}

void parts_button_style_draw_label(Texture *t, struct parts_button_style *s, bool pressed)
{
	if (!t->handle || !s->text->size)
		return;
	int w, h, line_h;
	parts_button_style_measure_text(s, &w, &h, &line_h);
	int x = 0, y = 0;
	if (parts_align_valid(s->text_align)) {
		x = LABEL_MARGIN + parts_align_x(s->text_align, t->w - w - LABEL_MARGIN * 2);
		y = LABEL_MARGIN + parts_align_y(s->text_align, t->h - h - LABEL_MARGIN * 2);
	}
	if (pressed) {
		x++;
		y++;
	}
	parts_button_style_draw_text(t, s, x, y, line_h, true);
}

struct parts_button *parts_get_button(struct parts *parts)
{
	if (parts->type == PARTS_TYPE_BUTTON)
		return parts->widget.button;
	parts_type_reset(parts, PARTS_TYPE_BUTTON, PARTS_SPRITE_CONSTRUCTION_PROCESS);
	struct parts_button *b = parts->widget.button = xcalloc(1, sizeof(*b));
	parts_button_style_init(&b->style, 200, 30);
	b->enabled = true;
	parts_button_rebuild(parts);
	return b;
}

void parts_button_free(struct parts_button *b)
{
	parts_button_style_free(&b->style);
	free(b);
}

void parts_button_rebuild(struct parts *parts)
{
	struct parts_button *b = parts->widget.button;
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		int look = b->enabled ? i : PARTS_LOOK_DISABLED;
		Texture *t = &parts->sprites[i].common.texture;
		switch (parts_button_style_load(parts, i, &b->style, "", look)) {
		case PARTS_BUTTON_FLAT:
			break;
		case PARTS_BUTTON_CG:
			parts_button_style_draw_label(t, &b->style, false);
			break;
		case PARTS_BUTTON_GENERATED:
			parts_button_style_draw_background(parts, i, &b->style, look);
			parts_button_style_draw_label(t, &b->style,
					b->enabled && i == PARTS_STATE_CLICKED);
			break;
		}
	}
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

void PE_SetButtonSize(int parts_no, int width, int height)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_button *b = parts_get_button(parts);
	if (b->style.w == width && b->style.h == height)
		return;
	b->style.w = width;
	b->style.h = height;
	parts_button_rebuild(parts);
}

void PE_SetButtonEnable(int parts_no, bool enabled)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_button *b = parts_get_button(parts);
	if (b->enabled == enabled)
		return;
	b->enabled = enabled;
	parts_button_rebuild(parts);
}

void PE_SetButtonText(int parts_no, struct string *text)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_button *b = parts_get_button(parts);
	if (!strcmp(b->style.text->text, text->text))
		return;
	free_string(b->style.text);
	b->style.text = string_dup(text);
	parts_button_rebuild(parts);
}
