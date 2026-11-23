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
#include "system4/cg.h"
#include "system4/string.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"

static int max_pos(struct parts_scrollbar *s)
{
	return max(s->total_size - s->view_size, 0);
}

static int track_length(struct parts_scrollbar *s)
{
	return s->length - s->leading - s->trailing;
}

static int travel(struct parts_scrollbar *s)
{
	return track_length(s) - s->thumb_length;
}

static int thumb_offset(struct parts_scrollbar *s)
{
	return s->leading + max((int)(travel(s) * s->rate), 0);
}

static bool is_dragging(struct parts_scrollbar *s)
{
	return s->pressed == PARTS_SCROLLBAR_THUMB || s->pressed == PARTS_SCROLLBAR_TRACK;
}

static bool is_button(enum parts_scrollbar_region region)
{
	return region == PARTS_SCROLLBAR_LEADING || region == PARTS_SCROLLBAR_TRAILING;
}

static int width(struct parts_scrollbar *s)
{
	return s->horizontal ? s->length : s->thickness;
}

static int height(struct parts_scrollbar *s)
{
	return s->horizontal ? s->thickness : s->length;
}

static void set_pos(struct parts_scrollbar *s, int pos)
{
	pos = min(max(pos, 0), max_pos(s));
	float rate = max_pos(s) ? (float)pos / max_pos(s) : 1.f;
	s->needs_compose |= s->pos != pos || s->rate != rate;
	s->pos = pos;
	s->rate = rate;
}

static void set_rate(struct parts_scrollbar *s, float rate)
{
	rate = min(max(rate, 0.f), 1.f);
	int pos = (int)(max_pos(s) * rate);
	s->needs_compose |= s->pos != pos || s->rate != rate;
	s->pos = pos;
	s->rate = rate;
}

static void free_resources(struct parts_scrollbar *s)
{
	for (int b = 0; b < 2; b++) {
		for (int i = 0; i < PARTS_NR_LOOKS; i++)
			gfx_delete_texture(&s->buttons[b][i]);
	}
	for (int i = 0; i < PARTS_NR_STATES; i++)
		gfx_delete_texture(&s->thumb[i]);
	gfx_delete_texture(&s->track);
}

void parts_scrollbar_free(struct parts_scrollbar *s)
{
	free_resources(s);
	free_string(s->cg_name);
	free(s);
}

struct parts_scrollbar *parts_get_scrollbar(struct parts *p, bool horizontal)
{
	enum parts_type type = horizontal ? PARTS_TYPE_HSCROLLBAR : PARTS_TYPE_VSCROLLBAR;
	if (p->type == type)
		return p->widget.scrollbar;
	parts_type_reset(p, type, PARTS_SPRITE_CONSTRUCTION_PROCESS);
	p->state = PARTS_STATE_DEFAULT;
	struct parts_scrollbar *s = p->widget.scrollbar = xcalloc(1, sizeof(*s));
	s->horizontal = horizontal;
	s->length = 256;
	s->thickness = s->leading = s->trailing = 24;
	s->total_size = 100;
	s->view_size = s->move_size = 1;
	s->cg_name = string_ref(&EMPTY_STRING);
	parts_scrollbar_rebuild(p);
	return s;
}

static void load_cg(Texture *t, struct string *base, const char *infix, const char *suffix)
{
	if (!base->size)
		return;
	struct string *name = parts_widget_resource_name(base, infix, suffix);
	struct cg *cg = asset_cg_load_by_name(name->text, NULL);
	free_string(name);
	if (cg) {
		gfx_init_texture_with_cg(t, cg);
		cg_free(cg);
	}
}

static int calc_thumb_length(struct parts_scrollbar *s)
{
	if (!max_pos(s))
		return 0;
	Texture *cg = &s->thumb[PARTS_STATE_DEFAULT];
	if (cg->handle)
		return s->horizontal ? cg->w : cg->h;
	return max(16, (int)((float)track_length(s) / max(s->total_size, 1) * s->view_size));
}

void parts_scrollbar_rebuild(struct parts *p)
{
	static const char *buttons[2][2] = {
		{ "／上ボタン", "／下ボタン" },
		{ "／左ボタン", "／右ボタン" },
	};
	static const char *thumb = "／バー";
	static const char *track = "／背景";
	static bool converted;
	if (!converted) {
		parts_utf2sjis_array(buttons[0], 2);
		parts_utf2sjis_array(buttons[1], 2);
		parts_utf2sjis_array(&thumb, 1);
		parts_utf2sjis_array(&track, 1);
		converted = true;
	}
	struct parts_scrollbar *s = p->widget.scrollbar;
	free_resources(s);
	for (int b = 0; b < 2; b++) {
		for (int look = 0; look < PARTS_NR_LOOKS; look++) {
			load_cg(&s->buttons[b][look], s->cg_name, buttons[s->horizontal][b],
					parts_widget_look_suffix(look));
		}
	}
	for (int state = 0; state < PARTS_NR_STATES; state++)
		load_cg(&s->thumb[state], s->cg_name, thumb, parts_widget_look_suffix(state));
	load_cg(&s->track, s->cg_name, track, "");
	s->thumb_length = calc_thumb_length(s);
	s->length = max(s->length, s->leading + s->trailing + s->thumb_length);
	for (int i = 0; i < PARTS_NR_STATES; i++)
		parts_set_dims(p, &p->sprites[i].common, width(s), height(s));
	parts_recalculate_hitbox(p);
	s->needs_compose = true;
}

static void draw_control(struct parts_scrollbar *s, Texture *dst, Texture *src,
		int offset, int length, int shade)
{
	if (s->thickness <= 0 || length <= 0)
		return;
	int x = s->horizontal ? offset : 0;
	int y = s->horizontal ? 0 : offset;
	if (src->handle)
		gfx_copy_with_alpha_map(dst, x, y, src, 0, 0, src->w, src->h);
	else if (s->horizontal)
		gfx_fill_alpha_color(dst, x, y, length, s->thickness, shade, shade, shade, 255);
	else
		gfx_fill_alpha_color(dst, x, y, s->thickness, length, shade, shade, shade, 255);
}

static void grow_to_fit(struct parts_scrollbar *s, Texture *t, int offset, int *w, int *h)
{
	if (!t->handle)
		return;
	*w = max(*w, (s->horizontal ? offset : 0) + t->w);
	*h = max(*h, (s->horizontal ? 0 : offset) + t->h);
}

static void notify_scroll(struct parts *p)
{
	struct parts_scrollbar *s = p->widget.scrollbar;
	if (s->pos == s->notified_pos && s->rate == s->notified_rate)
		return;
	parts_msg_push(p, PARTS_MSG_SCROLL, "ii", s->pos, max_pos(s));
	s->notified_pos = s->pos;
	s->notified_rate = s->rate;
}

static void compose(struct parts *p)
{
	struct parts_scrollbar *s = p->widget.scrollbar;
	s->needs_compose = false;
	// Draw all controls into the texture of the default state sprite.
	// Each control shows its own look. The parts state does not select it.
	Texture *dst = &p->sprites[0].common.texture;
	gfx_delete_texture(dst);
	if (s->thickness <= 0 || s->length <= 0)
		return;
	Texture *buttons[2];
	for (int b = 0; b < 2; b++) {
		enum parts_scrollbar_region region =
			b == 0 ? PARTS_SCROLLBAR_LEADING : PARTS_SCROLLBAR_TRAILING;
		int look = !max_pos(s) ? PARTS_LOOK_DISABLED
			: s->pressed == region ? PARTS_STATE_CLICKED
			: s->hover == region ? PARTS_STATE_HOVERED
			: PARTS_STATE_DEFAULT;
		buttons[b] = &s->buttons[b][look];
	}
	int state = is_dragging(s) ? PARTS_STATE_CLICKED
		: s->hover == PARTS_SCROLLBAR_THUMB ? PARTS_STATE_HOVERED
		: PARTS_STATE_DEFAULT;
	Texture *thumb = max_pos(s) ? &s->thumb[state] : NULL;
	// A control CG can be larger than the scrollbar. Make the texture large
	// enough to show all of each CG.
	int w = width(s), h = height(s);
	grow_to_fit(s, &s->track, s->leading, &w, &h);
	grow_to_fit(s, buttons[0], 0, &w, &h);
	grow_to_fit(s, buttons[1], s->length - s->trailing, &w, &h);
	if (thumb)
		grow_to_fit(s, thumb, thumb_offset(s), &w, &h);
	gfx_init_texture_rgba(dst, w, h, (SDL_Color){0, 0, 0, 0});
	draw_control(s, dst, &s->track, s->leading, track_length(s), 200);
	draw_control(s, dst, buttons[0], 0, s->leading, 150);
	draw_control(s, dst, buttons[1], s->length - s->trailing, s->trailing, 150);
	if (thumb)
		draw_control(s, dst, thumb, thumb_offset(s), s->thumb_length, 150);
	parts_dirty(p);
}

void parts_scrollbar_update(struct parts *p)
{
	if (!parts_is_visible(p))
		return;
	notify_scroll(p);
	if (p->widget.scrollbar->needs_compose)
		compose(p);
}

static enum parts_scrollbar_region hit_region(struct parts_scrollbar *s, int offset)
{
	if (offset < s->leading)
		return PARTS_SCROLLBAR_LEADING;
	if (offset >= s->length - s->trailing)
		return PARTS_SCROLLBAR_TRAILING;
	int thumb = thumb_offset(s);
	if (offset >= thumb && offset < thumb + s->thumb_length)
		return PARTS_SCROLLBAR_THUMB;
	return PARTS_SCROLLBAR_TRACK;
}

static void set_hover(struct parts_scrollbar *s, enum parts_scrollbar_region region)
{
	if (s->hover == region)
		return;
	s->hover = region;
	s->needs_compose = true;
}

static void release(struct parts_scrollbar *s)
{
	if (s->pressed == PARTS_SCROLLBAR_NONE)
		return;
	s->pressed = PARTS_SCROLLBAR_NONE;
	s->needs_compose = true;
}

void parts_scrollbar_reset_input(struct parts *p)
{
	struct parts_scrollbar *s = p->widget.scrollbar;
	set_hover(s, PARTS_SCROLLBAR_NONE);
	release(s);
}

void parts_scrollbar_mouse(struct parts *p, Point pos, bool down,
		bool trigger, bool hovered, int passed_time)
{
	struct parts_scrollbar *s = p->widget.scrollbar;
	int offset = s->horizontal
		? pos.x - p->global.pos.x - p->sprites[0].common.origin_offset.x
		: pos.y - p->global.pos.y - p->sprites[0].common.origin_offset.y;
	enum parts_scrollbar_region region = hovered ? hit_region(s, offset) : PARTS_SCROLLBAR_NONE;
	set_hover(s, region);
	if (!down || !parts_began_click || !parts_is_visible(p)) {
		release(s);
		return;
	}
	if (trigger && hovered && max_pos(s)) {
		s->pressed = region;
		s->press_offset = offset;
		s->repeat_time = 500;
		if (region == PARTS_SCROLLBAR_TRACK && travel(s) > 0)
			set_rate(s, (float)(offset - s->leading - s->thumb_length / 2) / travel(s));
		s->press_rate = s->rate;
		if (is_button(region))
			set_pos(s, s->pos + (region == PARTS_SCROLLBAR_LEADING ? -s->move_size : s->move_size));
		s->needs_compose = true;
	} else if (is_dragging(s) && travel(s) > 0) {
		set_rate(s, s->press_rate + (float)(offset - s->press_offset) / travel(s));
	} else if (is_button(s->pressed)) {
		s->repeat_time -= max(passed_time, 0);
		if (s->repeat_time < 0) {
			if (hovered && region == s->pressed)
				set_pos(s, s->pos + (s->pressed == PARTS_SCROLLBAR_LEADING ? -s->move_size : s->move_size));
			s->repeat_time = 80;
		}
	}
}

static struct parts_scrollbar *get_scrollbar(int parts_no, bool horizontal)
{
	return parts_get_scrollbar(parts_get(parts_no), horizontal);
}

static void set_total_size(int parts_no, bool horizontal, int size)
{
	struct parts *p = parts_get(parts_no);
	struct parts_scrollbar *s = parts_get_scrollbar(p, horizontal);
	s->total_size = size;
	set_pos(s, s->pos);
	parts_scrollbar_rebuild(p);
}

static void set_view_size(int parts_no, bool horizontal, int size)
{
	struct parts *p = parts_get(parts_no);
	struct parts_scrollbar *s = parts_get_scrollbar(p, horizontal);
	s->view_size = size;
	set_pos(s, s->pos);
	parts_scrollbar_rebuild(p);
}

void PE_SetVScrollbarTotalSize(int parts_no, int size)
{
	set_total_size(parts_no, false, size);
}

void PE_SetVScrollbarViewSize(int parts_no, int size)
{
	set_view_size(parts_no, false, size);
}

void PE_SetVScrollbarScrollPos(int parts_no, int pos)
{
	set_pos(get_scrollbar(parts_no, false), pos);
}

void PE_SetVScrollbarScrollRate(int parts_no, float rate)
{
	set_rate(get_scrollbar(parts_no, false), rate);
}

int PE_GetVScrollbarViewSize(int parts_no)
{
	return get_scrollbar(parts_no, false)->view_size;
}

int PE_GetVScrollbarScrollPos(int parts_no)
{
	return get_scrollbar(parts_no, false)->pos;
}

float PE_GetVScrollbarScrollRate(int parts_no)
{
	return get_scrollbar(parts_no, false)->rate;
}

int PE_GetVScrollbarMoveSizeByButton(int parts_no)
{
	return get_scrollbar(parts_no, false)->move_size;
}

void PE_SetHScrollbarTotalSize(int parts_no, int size)
{
	set_total_size(parts_no, true, size);
}

void PE_SetHScrollbarViewSize(int parts_no, int size)
{
	set_view_size(parts_no, true, size);
}

void PE_SetHScrollbarScrollPos(int parts_no, int pos)
{
	set_pos(get_scrollbar(parts_no, true), pos);
}

void PE_SetHScrollbarScrollRate(int parts_no, float rate)
{
	set_rate(get_scrollbar(parts_no, true), rate);
}

int PE_GetHScrollbarViewSize(int parts_no)
{
	return get_scrollbar(parts_no, true)->view_size;
}

int PE_GetHScrollbarScrollPos(int parts_no)
{
	return get_scrollbar(parts_no, true)->pos;
}

float PE_GetHScrollbarScrollRate(int parts_no)
{
	return get_scrollbar(parts_no, true)->rate;
}

int PE_GetHScrollbarMoveSizeByButton(int parts_no)
{
	return get_scrollbar(parts_no, true)->move_size;
}
