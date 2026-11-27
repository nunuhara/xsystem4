/* Sys43 vertical/horizontal scrollbars, GPL-2.0-or-later. */
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "parts.h"
#include "parts_scrollbar.h"
#include "parts_internal.h"
#include "vm.h"
#include "xsystem4.h"

static int s_modifier_scrollbar_scale_pct = 100;

void parts_set_modifier_scrollbar_scale(int pct)
{
	if (pct < 75) pct = 75;
	if (pct > 300) pct = 300;
	s_modifier_scrollbar_scale_pct = pct;
}

int parts_get_modifier_scrollbar_scale(void)
{
	return s_modifier_scrollbar_scale_pct;
}

void parts_scrollbar_end_session(void)
{
	s_modifier_scrollbar_scale_pct = 100;
}

int parts_scrollbar_range(struct parts_scrollbar *s)
{
	return min((int64_t)INT_MAX, max((int64_t)0, (int64_t)s->total - s->view));
}

static void set_pos(struct parts *parts, int pos)
{
	if (!parts || !parts->scrollbar)
		return;
	struct parts_scrollbar *s = parts->scrollbar;
	int range = parts_scrollbar_range(s);
	int new_pos = min(range, max(0, pos));
	float new_rate = range ? (float)new_pos / range : 1.f;
	if (s->pos == new_pos && s->rate == new_rate) return;
	s->pos = new_pos;
	s->rate = new_rate;
	parts_dirty(parts);
}

void parts_scrollbar_free(struct parts *parts)
{
	struct parts_scrollbar *s = parts->scrollbar;
	if (!s) return;
	for (int resource = 0; resource < 2; resource++)
		for (int piece = 0; piece < 5; piece++) free_string(s->names[resource][piece]);
	for (int piece = 0; piece < 4; piece++)
		for (int state = 0; state < 4; state++)
			parts_state_reset(&s->skins[piece][state], PARTS_UNINITIALIZED);
	free(s);
	parts->scrollbar = NULL;
}

static bool load_skin_enc(struct parts *parts, struct parts_state *state, struct string *base,
		bool flat, const char *piece, const char *suffix, bool force_sjis)
{
	struct string *name = string_dup(base);
	const char *names[] = {piece, suffix};
	for (int i = 0; i < 2; i++) {
		const char *encoded;
		if (force_sjis && sys4_text_is_gbk) {
			sys4_set_text_gbk(false);
			encoded = parts_utf8_to_cjk_literal(names[i]);
			sys4_set_text_gbk(true);
		} else {
			encoded = parts_utf8_to_cjk_literal(names[i]);
		}
		string_append_cstr(&name, encoded, strlen(encoded));
	}
	parts_state_reset(state, flat ? PARTS_FLAT : PARTS_CG);
	bool ok = flat ? parts_flat_load(parts, &state->flat, name)
		: parts_cg_set(parts, &state->cg, name);
	free_string(name);
	if (!ok) parts_state_reset(state, PARTS_UNINITIALIZED);
	return ok;
}

static bool load_skin(struct parts *parts, struct parts_state *state, struct string *base,
		bool flat, const char *piece, const char *suffix)
{
	if (load_skin_enc(parts, state, base, flat, piece, suffix, false))
		return true;
	if (sys4_text_is_gbk && ((piece && piece[0]) || (suffix && suffix[0])))
		return load_skin_enc(parts, state, base, flat, piece, suffix, true);
	return false;
}

static bool load_skin_variants(struct parts *parts, struct parts_state *state, struct string *base,
		bool flat, bool has_override, const char *const piece_alts[2],
		bool is_bg, const char *const state_alts[3])
{
	if (!base->size)
		return false;
	for (int p = 0; p < (has_override ? 1 : 2); p++) {
		const char *piece = has_override ? "" : piece_alts[p];
		if (!piece)
			continue;
		if (is_bg) {
			if (load_skin(parts, state, base, flat, piece, ""))
				return true;
		} else {
			for (int k = 0; k < 3; k++) {
				if (state_alts[k] && load_skin(parts, state, base, flat, piece, state_alts[k]))
					return true;
			}
		}
	}
	return false;
}

void parts_scrollbar_rebuild(struct parts *parts)
{
	struct parts_scrollbar *s = parts->scrollbar;
	static const char *const pieces[2][4][2] = {
		{ {"／上按钮", "／上ボタン"}, {"／下按钮", "／下ボタン"}, {"／栏", "／バー"}, {"／背景", NULL} },
		{ {"／左按钮", "／左ボタン"}, {"／右按钮", "／右ボタン"}, {"／栏", "／バー"}, {"／背景", NULL} }
	};
	static const char *const states[4][3] = {
		{ "／一般", "／通常", "／普通" },
		{ "／オン", "／on", "／ON" },
		{ "／DOWN", "／ダウン", "／down" },
		{ "／无效", "／無効", "／无効" }
	};
	for (int piece = 0; piece < 4; piece++) {
		if (s->is_slider && piece < 2) {
			for (int state = 0; state < 4; state++) {
				s->loaded[piece][state] = false;
				parts_state_reset(&s->skins[piece][state], PARTS_UNINITIALIZED);
			}
			continue;
		}
		bool flat = s->names[1][piece + 1]->size
			|| (!s->names[0][piece + 1]->size && s->names[1][0]->size);
		struct string *override = s->names[flat][piece + 1];
		struct string *base = override->size ? override : s->names[flat][0];
		for (int state = 0; state < 4; state++) {
			struct parts_state *target = &s->skins[piece][state];
			bool loaded = load_skin_variants(parts, target, base, flat,
					override->size > 0, pieces[s->horizontal][piece],
					piece == 3, states[state]);
			if (!loaded && state) {
				loaded = load_skin_variants(parts, target, base, flat,
						override->size > 0, pieces[s->horizontal][piece],
						piece == 3, states[0]);
			}
			if (!loaded && override->size > 0 && piece != 3) {
				loaded = load_skin(parts, target, base, flat, "", "");
			}
			s->loaded[piece][state] = loaded;
			if (!loaded) {
				parts_state_reset(target, PARTS_CG);
				int shade = piece == 3 ? 96 : state == 2 ? 144 : state == 1 ? 224 : 200;
				parts_set_dims(parts, &target->common, 1, 1);
				gfx_init_texture_rgba(&target->common.texture, 1, 1, (SDL_Color){shade, shade, shade, 255});
			}
		}
	}
	for (int state = 0; state < PARTS_NR_STATES; state++) {
		parts_state_reset(&parts->states[state], PARTS_RECT_DETECTION);
		parts_set_dims(parts, &parts->states[state].common, s->width, s->height);
	}
	parts_component_dirty(parts);
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

static void init_scrollbar_common(struct parts *parts, bool horizontal, bool is_slider)
{
	parts_scrollbar_free(parts);
	parts_widget_free(parts);
	parts_panel_free(parts);
	parts_message_window_free(parts);
	struct parts_scrollbar *s = parts->scrollbar = xcalloc(1, sizeof(*s));
	s->horizontal = horizontal;
	s->is_slider = is_slider;
	s->width = horizontal ? 256 : 24;
	s->height = horizontal ? 24 : 256;
	s->before = s->after = is_slider ? 0 : 24;
	s->total = 100;
	s->view = s->step = 1;
	s->reported_range = 99;
	s->hovered = s->pressed = -1;
	for (int resource = 0; resource < 2; resource++)
		for (int piece = 0; piece < 5; piece++) s->names[resource][piece] = string_ref(&EMPTY_STRING);
	parts->component_type = horizontal ? 3 : 2;
	parts_scrollbar_rebuild(parts);
}

void parts_scrollbar_create(struct parts *parts, bool horizontal)
{
	if (parts->scrollbar && parts->scrollbar->horizontal == horizontal && !parts->scrollbar->is_slider) return;
	init_scrollbar_common(parts, horizontal, false);
}

void parts_sliderbar_create(struct parts *parts, bool horizontal)
{
	if (parts->scrollbar && parts->scrollbar->horizontal == horizontal && parts->scrollbar->is_slider) return;
	init_scrollbar_common(parts, horizontal, true);
}

static struct parts *scrollbar(int no, bool horizontal)
{
	if (parts_v2 && no <= 0)
		return NULL;
	struct parts *parts = parts_get(no);
	parts_scrollbar_create(parts, horizontal);
	return parts;
}

static struct parts *scrollbar_get_existing(int no, bool horizontal, bool is_slider)
{
	if (parts_v2 && no <= 0)
		return NULL;
	struct parts *parts = parts_try_get(no);
	if (!parts || !parts->scrollbar)
		return NULL;
	if (parts->scrollbar->horizontal != horizontal || parts->scrollbar->is_slider != is_slider)
		return NULL;
	return parts;
}

static struct parts *sliderbar(int no, bool horizontal)
{
	if (parts_v2 && no <= 0)
		return NULL;
	struct parts *parts = parts_get(no);
	parts_sliderbar_create(parts, horizontal);
	return parts;
}

int parts_scrollbar_skin_state(struct parts_scrollbar *s, int piece)
{
	if (!parts_scrollbar_range(s) || (piece == 0 && !s->pos)
			|| (piece == 1 && s->pos == parts_scrollbar_range(s))) return 3;
	return s->pressed == piece ? 2 : s->hovered == piece ? 1 : 0;
}

static void scrollbar_effective_cross_metrics(struct parts *parts, int *out_length,
		int *out_breadth, int *out_cross_off)
{
	struct parts_scrollbar *s = parts->scrollbar;
	int length = max(0, s->horizontal ? s->width : s->height);
	int base_breadth = max(0, s->horizontal ? s->height : s->width);
	int breadth = base_breadth;
	int cross_off = 0;
	int pct = s_modifier_scrollbar_scale_pct;
	if (pct != 100 && base_breadth > 0) {
		float scale = (float)pct / 100.0f;
		breadth = max(1, (int)lroundf((float)base_breadth * scale));
		cross_off = -(breadth - base_breadth) / 2;
		if (breadth > base_breadth) {
			mat4 world;
			parts_world_matrix(parts, world);
			Point origin = parts->states[0].common.origin_offset;
			if (!s->horizontal && config.view_width > 0) {
				float sx = world[0][0];
				if (sx > 0.05f && fabsf(world[1][0]) < 1e-4f) {
					float wx0 = world[3][0] + (origin.x + cross_off) * sx;
					float wx1 = wx0 + breadth * sx;
					if (wx1 > (float)config.view_width) {
						cross_off -= (int)ceilf((wx1 - (float)config.view_width) / sx);
						wx0 = world[3][0] + (origin.x + cross_off) * sx;
					}
					if (wx0 < 0.0f) {
						cross_off += (int)ceilf((-wx0) / sx);
					}
				}
			} else if (s->horizontal && config.view_height > 0) {
				float sy = world[1][1];
				if (sy > 0.05f && fabsf(world[0][1]) < 1e-4f) {
					float wy0 = world[3][1] + (origin.y + cross_off) * sy;
					float wy1 = wy0 + breadth * sy;
					if (wy1 > (float)config.view_height) {
						cross_off -= (int)ceilf((wy1 - (float)config.view_height) / sy);
						wy0 = world[3][1] + (origin.y + cross_off) * sy;
					}
					if (wy0 < 0.0f) {
						cross_off += (int)ceilf((-wy0) / sy);
					}
				}
			}
		}
	}
	*out_length = length;
	*out_breadth = breadth;
	*out_cross_off = cross_off;
}

void parts_scrollbar_hitbox(struct parts *parts, int state, Rectangle *out_hitbox)
{
	if (!parts->scrollbar || s_modifier_scrollbar_scale_pct == 100) {
		*out_hitbox = parts->states[state].common.hitbox;
		return;
	}
	int length = 0, breadth = 0, cross_off = 0;
	scrollbar_effective_cross_metrics(parts, &length, &breadth, &cross_off);
	Point origin = parts->states[state].common.origin_offset;
	bool v12 = parts_v2;
	int off_x = v12 ? 0 : (int)parts->local.pos.x;
	int off_y = v12 ? 0 : (int)parts->local.pos.y;
	if (parts->scrollbar->horizontal) {
		*out_hitbox = (Rectangle){ off_x + origin.x, off_y + origin.y + cross_off, length, breadth };
	} else {
		*out_hitbox = (Rectangle){ off_x + origin.x + cross_off, off_y + origin.y, breadth, length };
	}
}

void parts_scrollbar_rects(struct parts *parts, Rectangle rects[4])
{
	struct parts_scrollbar *s = parts->scrollbar;
	int length = 0, breadth = 0, cross_off = 0;
	scrollbar_effective_cross_metrics(parts, &length, &breadth, &cross_off);
	int pct = s_modifier_scrollbar_scale_pct;
	float scale = (float)pct / 100.0f;

	int before = min(length, max(0, s->before));
	int after = min(length - before, max(0, s->after));
	if (pct != 100 && length > 0) {
		float btn_scale = min(scale, 1.6f);
		int sb = s->before > 0 ? (int)lroundf((float)s->before * btn_scale) : 0;
		int sa = s->after > 0 ? (int)lroundf((float)s->after * btn_scale) : 0;
		int max_ends = max(before + after, length / 2);
		if (sb + sa > max_ends && sb + sa > 0) {
			sb = (int)((int64_t)sb * max_ends / (sb + sa));
			sa = max_ends - sb;
		}
		before = min(length, max(0, sb));
		after = min(length - before, max(0, sa));
	}

	int track = length - before - after;
	int state = parts_scrollbar_skin_state(s, 2);
	struct parts_common *thumb = &s->skins[2][state].common;
	int size;
	if (pct == 100) {
		size = s->loaded[2][state] ? (s->horizontal ? thumb->w : thumb->h)
			: max(8, s->total > 0 ? (int)((int64_t)track * max(0, s->view) / s->total) : track);
	} else {
		int base_thumb = s->loaded[2][state] ? (s->horizontal ? thumb->w : thumb->h)
			: max(8, s->total > 0 ? (int)((int64_t)track * max(0, s->view) / s->total) : track);
		size = s->loaded[2][state] ? (int)lroundf((float)base_thumb * scale) : base_thumb;
		if (pct > 100 && track > 0) {
			int min_thumb = min(track * 3 / 5, (int)lroundf(24.0f * scale));
			if (size < min_thumb)
				size = min_thumb;
		}
	}
	size = min(track, max(0, size));
	int thumb_pos = before + (int)((track - size) * s->rate);
	int positions[] = {0, length - after, thumb_pos, before};
	int sizes[] = {before, after, size, track};
	for (int piece = 0; piece < 4; piece++) {
		rects[piece] = s->horizontal ? (Rectangle){positions[piece], cross_off, sizes[piece], breadth}
			: (Rectangle){cross_off, positions[piece], breadth, sizes[piece]};
	}
}

static float cursor_axis(struct parts *parts, Point cursor)
{
	mat4 world;
	parts_world_matrix(parts, world);
	float det = world[0][0] * world[1][1] - world[1][0] * world[0][1];
	if (!isfinite(det) || fabsf(det) < 1e-12f) return 0;
	float x = cursor.x - world[3][0], y = cursor.y - world[3][1];
	Point origin = parts->states[0].common.origin_offset;
	return parts->scrollbar->horizontal ? (world[1][1] * x - world[1][0] * y) / det - origin.x
		: (world[0][0] * y - world[0][1] * x) / det - origin.y;
}

void parts_scrollbar_input(struct parts *parts, Point cursor, bool hovered,
		bool down, bool pressed, int elapsed, int wheel)
{
	struct parts_scrollbar *s = parts->scrollbar;
	Rectangle rects[4];
	parts_scrollbar_rects(parts, rects);
	float axis = cursor_axis(parts, cursor);
	int begin = s->horizontal ? rects[3].x : rects[3].y;
	int length = s->horizontal ? rects[3].w : rects[3].h;
	int thumb = s->horizontal ? rects[2].x : rects[2].y;
	int size = s->horizontal ? rects[2].w : rects[2].h;
	int old_pos = s->pos, old_hover = s->hovered, old_pressed = s->pressed;
	s->hovered = !hovered ? -1 : axis < begin ? 0 : axis >= begin + length ? 1
		: axis >= thumb && axis < thumb + size ? 2 : 3;
	if (pressed) {
		if (s->is_slider && s->hovered == 3) {
			s->pressed = 2;
			s->drag_offset = size * 0.5f;
		} else {
			s->pressed = s->hovered;
			s->drag_offset = axis - thumb;
		}
		s->repeat_ms = -400;
	}
	if (hovered && wheel) set_pos(parts, min((int64_t)INT_MAX, max((int64_t)INT_MIN,
			(int64_t)s->pos + (int64_t)wheel * s->step)));
	if (down && s->pressed >= 0 && parts_scrollbar_range(s)) {
		s->repeat_ms += max(0, elapsed);
		if (s->pressed == 2 && length > size) {
			float rate = (axis - s->drag_offset - begin) / (length - size);
			s->rate = min(1.f, max(0.f, rate));
			s->pos = (int)(s->rate * parts_scrollbar_range(s));
		} else if (pressed || s->repeat_ms >= 60) {
			int direction = s->pressed == 0 ? -1 : s->pressed == 1 ? 1 : axis < thumb ? -1 : 1;
			int step = s->pressed < 2 ? s->step : s->view;
			set_pos(parts, min((int64_t)INT_MAX, max((int64_t)INT_MIN, (int64_t)s->pos + (int64_t)direction * step)));
			if (!pressed) s->repeat_ms = 0;
		}
	}
	if (!down) s->pressed = -1;
	if (s->pos != old_pos || s->hovered != old_hover || s->pressed != old_pressed) parts_dirty(parts);
}

void parts_scrollbar_notify_changes(void)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		struct parts_scrollbar *s = parts->scrollbar;
		if (!s) continue;
		int range = parts_scrollbar_range(s);
		if (s->pos == s->reported_pos && range == s->reported_range && s->rate == s->reported_rate)
			continue;
		// Sys43 coalesces API, linked-size and input changes at the frame boundary.
		// Rate changes notify even when rounding leaves the integer position unchanged.
		parts_msg_push(parts, PARTS_MSG_SCROLL, "ii", s->pos, range);
		s->reported_pos = s->pos;
		s->reported_range = range;
		s->reported_rate = s->rate;
	}
}

bool parts_scrollbar_update(struct parts *parts, int elapsed)
{
	struct parts_scrollbar *s = parts->scrollbar;
	bool dirty = false;
	for (int piece = 0; piece < 4; piece++)
		dirty |= parts_state_update(&s->skins[piece][parts_scrollbar_skin_state(s, piece)], elapsed);
	return dirty;
}

void parts_scrollbar_update_links(void)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		struct parts_scrollbar *s = parts->scrollbar;
		if (!s) continue;
		// Zero disables the link, even if the scene has a component numbered zero.
		struct parts *total = s->total_link ? parts_try_get(s->total_link) : NULL;
		struct parts *view = s->view_link ? parts_try_get(s->view_link) : NULL;
		int total_size = total ? (s->horizontal ? PE_GetPartsWidth(total->no, 1) : PE_GetPartsHeight(total->no, 1)) : s->total;
		int view_size = view ? (s->horizontal ? PE_GetPartsWidth(view->no, 1) : PE_GetPartsHeight(view->no, 1)) : s->view;
		if (s->total == total_size && s->view == view_size) continue;
		s->total = total_size;
		s->view = view_size;
		set_pos(parts, s->pos);
	}
}

#define SCROLLBAR_API(axis, horizontal) \
void PE_Set##axis##ScrollbarSize(int no, int width, int height) { \
	struct parts *p = scrollbar(no, horizontal); if (!p) return; \
	if (p->scrollbar->width == width && p->scrollbar->height == height) return; \
	p->scrollbar->width = width; p->scrollbar->height = height; parts_scrollbar_rebuild(p); } \
void PE_Set##axis##ScrollbarTotalSize(int no, int size) { \
	struct parts *p = scrollbar(no, horizontal); if (!p) return; \
	if (p->scrollbar->total == size) return; \
	p->scrollbar->total = size; set_pos(p, p->scrollbar->pos); } \
void PE_Set##axis##ScrollbarViewSize(int no, int size) { \
	struct parts *p = scrollbar(no, horizontal); if (!p) return; \
	if (p->scrollbar->view == size) return; \
	p->scrollbar->view = size; set_pos(p, p->scrollbar->pos); } \
void PE_Set##axis##ScrollbarScrollPos(int no, int pos) { set_pos(scrollbar(no, horizontal), pos); } \
void PE_Set##axis##ScrollbarScrollRate(int no, float rate) { \
	struct parts *p = scrollbar(no, horizontal); if (!p) return; \
	struct parts_scrollbar *s = p->scrollbar; \
	float clamped = isfinite(rate) ? min(1.f, max(0.f, rate)) : 0.f; \
	int new_pos = (int)((double)clamped * parts_scrollbar_range(s)); \
	if (s->rate == clamped && s->pos == new_pos) return; \
	s->rate = clamped; s->pos = new_pos; parts_dirty(p); } \
void PE_Set##axis##ScrollbarMoveSizeByButton(int no, int size) { \
	struct parts *p = scrollbar(no, horizontal); if (p) p->scrollbar->step = size; } \
int PE_Get##axis##ScrollbarTotalSize(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->total : 0; } \
int PE_Get##axis##ScrollbarViewSize(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->view : 0; } \
int PE_Get##axis##ScrollbarScrollPos(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->pos : 0; } \
float PE_Get##axis##ScrollbarScrollRate(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->rate : 0.f; } \
int PE_Get##axis##ScrollbarMoveSizeByButton(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->step : 0; } \
void PE_Set##axis##ScrollTotalSizeLinkNumber(int no, int target) { \
	struct parts *p = scrollbar(no, horizontal); if (p) p->scrollbar->total_link = target; } \
void PE_Set##axis##ScrollViewSizeLinkNumber(int no, int target) { \
	struct parts *p = scrollbar(no, horizontal); if (p) p->scrollbar->view_link = target; } \
int PE_Get##axis##ScrollTotalSizeLinkNumber(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->total_link : 0; } \
int PE_Get##axis##ScrollViewSizeLinkNumber(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->view_link : 0; }
SCROLLBAR_API(V, false)
SCROLLBAR_API(H, true)
#undef SCROLLBAR_API

#define END_API(axis, horizontal, end, field) \
void PE_Set##axis##Scrollbar##end(int no, int size) { struct parts *p = scrollbar(no, horizontal); if (!p || p->scrollbar->field == size) return; p->scrollbar->field = size; parts_dirty(p); } \
int PE_Get##axis##Scrollbar##end(int no) { struct parts *p = scrollbar_get_existing(no, horizontal, false); return p ? p->scrollbar->field : 0; }
END_API(V, false, UpHeight, before)
END_API(V, false, DownHeight, after)
END_API(H, true, LeftWidth, before)
END_API(H, true, RightWidth, after)
#undef END_API

#define NAME_API(axis, horizontal, part, index, kind, flat) \
void PE_Set##axis##Scrollbar##part##kind##Name(int no, struct string *name) { \
	struct parts *p = scrollbar(no, horizontal); if (!p) return; \
	struct string **target = &p->scrollbar->names[flat][index]; \
	if (!strcmp((*target)->text, name->text)) return; \
	struct string *copy = string_dup(name); free_string(*target); *target = copy; parts_scrollbar_rebuild(p); } \
struct string *PE_Get##axis##Scrollbar##part##kind##Name(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, false); \
	return p ? string_dup(p->scrollbar->names[flat][index]) : string_ref(&EMPTY_STRING); }
#define NAMES(axis, horizontal, first, last, kind, flat) \
NAME_API(axis, horizontal, , 0, kind, flat) \
NAME_API(axis, horizontal, Override##first, 1, kind, flat) \
NAME_API(axis, horizontal, Override##last, 2, kind, flat) \
NAME_API(axis, horizontal, OverrideBar, 3, kind, flat) \
NAME_API(axis, horizontal, OverrideBG, 4, kind, flat)
NAMES(V, false, Up, Down, CG, 0)
NAMES(V, false, Up, Down, Flat, 1)
NAMES(H, true, Left, Right, CG, 0)
NAMES(H, true, Left, Right, Flat, 1)
#undef NAMES
#undef NAME_API

#define LINK_API(name, index) \
void PE_SetComponentScroll##name##LinkNumber(int no, int target) { \
	if (parts_v2 && no <= 0) return; \
	struct parts *p = parts_get(no); p->scroll_links[index] = target; parts_component_dirty(p); parts_dirty(p); } \
int PE_GetComponentScroll##name##LinkNumber(int no) { \
	if (parts_v2 && no <= 0) return 0; \
	struct parts *p = parts_try_get(no); return p ? p->scroll_links[index] : 0; }
LINK_API(PosX, 0)
LINK_API(PosY, 1)
LINK_API(Alpha, 2)
#undef LINK_API

#define SLIDERBAR_API(axis, horizontal) \
void PE_Set##axis##SliderBarSize(int no, int width, int height) { \
	struct parts *p = sliderbar(no, horizontal); if (!p) return; \
	if (p->scrollbar->width == width && p->scrollbar->height == height) return; \
	p->scrollbar->width = width; p->scrollbar->height = height; parts_scrollbar_rebuild(p); } \
void PE_Set##axis##SliderBarTotalSize(int no, int size) { \
	struct parts *p = sliderbar(no, horizontal); if (!p) return; \
	if (p->scrollbar->total == size) return; \
	p->scrollbar->total = size; set_pos(p, p->scrollbar->pos); } \
void PE_Set##axis##SliderBarViewSize(int no, int size) { \
	struct parts *p = sliderbar(no, horizontal); if (!p) return; \
	if (p->scrollbar->view == size) return; \
	p->scrollbar->view = size; set_pos(p, p->scrollbar->pos); } \
void PE_Set##axis##SliderBarScrollPos(int no, int pos) { set_pos(sliderbar(no, horizontal), pos); } \
void PE_Set##axis##SliderBarScrollRate(int no, float rate) { \
	struct parts *p = sliderbar(no, horizontal); if (!p) return; \
	struct parts_scrollbar *s = p->scrollbar; \
	float clamped = isfinite(rate) ? min(1.f, max(0.f, rate)) : 0.f; \
	int new_pos = (int)((double)clamped * parts_scrollbar_range(s)); \
	if (s->rate == clamped && s->pos == new_pos) return; \
	s->rate = clamped; s->pos = new_pos; parts_dirty(p); } \
int PE_Get##axis##SliderBarTotalSize(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, true); return p ? p->scrollbar->total : 0; } \
int PE_Get##axis##SliderBarViewSize(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, true); return p ? p->scrollbar->view : 0; } \
int PE_Get##axis##SliderBarScrollPos(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, true); return p ? p->scrollbar->pos : 0; } \
float PE_Get##axis##SliderBarScrollRate(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, true); return p ? p->scrollbar->rate : 0.f; } \
void PE_Set##axis##SliderTotalSizeLinkNumber(int no, int target) { \
	struct parts *p = sliderbar(no, horizontal); if (p) p->scrollbar->total_link = target; } \
void PE_Set##axis##SliderViewSizeLinkNumber(int no, int target) { \
	struct parts *p = sliderbar(no, horizontal); if (p) p->scrollbar->view_link = target; } \
int PE_Get##axis##SliderTotalSizeLinkNumber(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, true); return p ? p->scrollbar->total_link : 0; } \
int PE_Get##axis##SliderViewSizeLinkNumber(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, true); return p ? p->scrollbar->view_link : 0; }
SLIDERBAR_API(V, false)
SLIDERBAR_API(H, true)
#undef SLIDERBAR_API

#define SLIDER_NAME_API(axis, horizontal, part, index, kind, flat) \
void PE_Set##axis##SliderBar##part##kind##Name(int no, struct string *name) { \
	struct parts *p = sliderbar(no, horizontal); if (!p) return; \
	struct string **target = &p->scrollbar->names[flat][index]; \
	if (!strcmp((*target)->text, name->text)) return; \
	struct string *copy = string_dup(name); free_string(*target); *target = copy; parts_scrollbar_rebuild(p); } \
struct string *PE_Get##axis##SliderBar##part##kind##Name(int no) { \
	struct parts *p = scrollbar_get_existing(no, horizontal, true); \
	return p ? string_dup(p->scrollbar->names[flat][index]) : string_ref(&EMPTY_STRING); }
#define SLIDER_NAMES(axis, horizontal, kind, flat) \
SLIDER_NAME_API(axis, horizontal, , 0, kind, flat) \
SLIDER_NAME_API(axis, horizontal, OverrideBar, 3, kind, flat) \
SLIDER_NAME_API(axis, horizontal, OverrideBG, 4, kind, flat)
SLIDER_NAMES(V, false, CG, 0)
SLIDER_NAMES(V, false, Flat, 1)
SLIDER_NAMES(H, true, CG, 0)
SLIDER_NAMES(H, true, Flat, 1)
#undef SLIDER_NAMES
#undef SLIDER_NAME_API
