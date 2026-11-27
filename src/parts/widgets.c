/* Native Parts button rendering and properties, GPL-2.0-or-later. */
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_activity.h"
#include "parts_widgets.h"
#include "vm.h"
#include "xsystem4.h"
#include "parts_internal.h"

static void set_string(struct string **out, struct string *value)
{
	struct string *copy = string_dup(value);
	free_string(*out);
	*out = copy;
}

void parts_widget_free(struct parts *parts)
{
	struct parts_widget *w = parts->widget;
	if (!w) return;
	parts_state_reset(&w->disabled, PARTS_UNINITIALIZED);
	parts_state_reset(&w->checked_disabled, PARTS_UNINITIALIZED);
	for (int i = 0; i < PARTS_NR_STATES; i++)
		parts_state_reset(&w->checked_states[i], PARTS_UNINITIALIZED);
	parts_state_reset(&w->label, PARTS_UNINITIALIZED);
	free_string(w->cg_name);
	free_string(w->flat_name);
	free_string(w->text);
	for (int i = 0; i < 2; i++) {
		free_string(w->override_cg[i]);
		free_string(w->override_flat[i]);
	}
	free(w);
	parts->widget = NULL;
}

/*
 * Convert a static UTF-8 string literal to the active game encoding (SJIS/GBK),
 * memoized permanently to avoid repeated allocations during UI rebuilds.
 */
const char *parts_utf8_to_cjk_literal(const char *literal)
{
	if (!literal || !literal[0])
		return "";
	static struct {
		const char *src;
		char *enc;
		bool gbk;
	} cache[2048];
	static size_t nr_cached = 0;

	bool gbk = sys4_text_is_gbk;
	size_t n = sizeof(cache) / sizeof(cache[0]);
	size_t idx = (size_t)(((uintptr_t)literal >> 3) ^ ((uintptr_t)gbk << 7)) & (n - 1);
	for (size_t step = 0; step < n; step++) {
		size_t slot = (idx + step) & (n - 1);
		if (!cache[slot].src) {
			if (nr_cached >= n - 1)
				return "";
			char *enc = utf2sjis(literal, strlen(literal));
			cache[slot].src = literal;
			cache[slot].enc = enc;
			cache[slot].gbk = gbk;
			nr_cached++;
			return enc;
		}
		if (cache[slot].src == literal && cache[slot].gbk == gbk)
			return cache[slot].enc;
	}
	return "";
}

static bool load_skin_with_encoded_suffix(struct parts *parts, struct parts_state *state,
		struct string *base, bool flat, const char *encoded)
{
	struct string *name = string_dup(base);
	string_append_cstr(&name, encoded, strlen(encoded));
	parts_state_reset(state, flat ? PARTS_FLAT : PARTS_CG);
	bool ok = flat ? parts_flat_load(parts, &state->flat, name)
		: parts_cg_set(parts, &state->cg, name);
	free_string(name);
	if (!ok) parts_state_reset(state, PARTS_UNINITIALIZED);
	return ok;
}

static bool load_skin(struct parts *parts, struct parts_state *state,
		struct string *base, bool flat, const char *suffix)
{
	if (load_skin_with_encoded_suffix(parts, state, base, flat,
			parts_utf8_to_cjk_literal(suffix)))
		return true;
	if (sys4_text_is_gbk && suffix && suffix[0]) {
		// Hybrid GBK-AIN / Shift-JIS-PACT titles (e.g. Evenicle 2 Rance CN)
		// store Shift-JIS base asset names in PACT and Shift-JIS skins in AFA.
		sys4_set_text_gbk(false);
		const char *sjis_suffix = parts_utf8_to_cjk_literal(suffix);
		sys4_set_text_gbk(true);
		return load_skin_with_encoded_suffix(parts, state, base, flat, sjis_suffix);
	}
	return false;
}

static void fill_rgba_rect(uint8_t *pixels, int tex_w, int x0, int y0, int rw, int rh,
		uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	for (int y = y0; y < y0 + rh; y++) {
		uint8_t *row = pixels + ((size_t)y * (size_t)tex_w + (size_t)x0) * 4;
		for (int x = 0; x < rw; x++) {
			row[0] = r;
			row[1] = g;
			row[2] = b;
			row[3] = a;
			row += 4;
		}
	}
}

void parts_widget_restore_plain_skin(struct parts *parts, struct parts_state *state,
		int index, bool checked)
{
	struct parts_widget *w = parts->widget;
	int width = state->common.w, height = state->common.h;
	if (!width || !height) return;
	bool depressed = index == PARTS_STATE_CLICKED || (checked && w->button_mode);
	int r = min(255, max(0, w->color[0]));
	int g = min(255, max(0, w->color[1]));
	int b = min(255, max(0, w->color[2]));
	if (index == PARTS_STATE_HOVERED) {
		r = min(255, r + 16); g = min(255, g + 16); b = min(255, b + 16);
	}
	uint8_t *pixels = xmalloc((size_t)width * (size_t)height * 4);
	fill_rgba_rect(pixels, width, 0, 0, width, height, r, g, b, 255);
	uint8_t upper = depressed ? 96 : 240;
	uint8_t lower = depressed ? 240 : 96;
	fill_rgba_rect(pixels, width, 0, 0, width, 1, upper, upper, upper, 255);
	fill_rgba_rect(pixels, width, 0, 0, 1, height, upper, upper, upper, 255);
	fill_rgba_rect(pixels, width, 0, height - 1, width, 1, lower, lower, lower, 255);
	fill_rgba_rect(pixels, width, width - 1, 0, 1, height, lower, lower, lower, 255);
	if (checked && !w->button_mode) {
		for (int x = 3; x < width - 3; x++) {
			int y = x < width / 2 ? height / 2 + x - 3
				: height - 4 - (x - width / 2) * 2;
			int cy = max(2, min(height - 4, y));
			int cw = min(2, width - x);
			int ch = min(2, height - cy);
			if (cw > 0 && ch > 0)
				fill_rgba_rect(pixels, width, x, cy, cw, ch, 0, 0, 0, 255);
		}
	}
	gfx_init_texture_with_pixels(&state->common.texture, width, height, pixels);
	free(pixels);
}

static bool load_skin_any(struct parts *parts, struct parts_state *state,
		struct string *base, bool flat, const char *const suffixes[], int n)
{
	for (int k = 0; k < n; k++) {
		if (suffixes[k] && load_skin(parts, state, base, flat, suffixes[k]))
			return true;
	}
	return false;
}

static Rectangle button_label_area(struct parts_state *skin)
{
	if (skin->type != PARTS_CG || !skin->cg.name || !skin->cg.name->size)
		return (Rectangle){0};
	struct string *name = string_dup(skin->cg.name);
	const char *suffix = parts_utf8_to_cjk_literal("／文字");
	string_append_cstr(&name, suffix, strlen(suffix));
	struct cg *cg = asset_cg_load_by_name(name->text, NULL);
	free_string(name);
	if (!cg)
		return (Rectangle){0};
	Rectangle area = {0};
	int w = cg->metrics.w, h = cg->metrics.h;
	if (cg->metrics.has_alpha && w == skin->common.w && h == skin->common.h) {
		const uint8_t *pixels = cg->pixels;
		int left = w, right = -1, top = h, bottom = -1;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				if (!pixels[((size_t)y * w + x) * 4 + 3])
					continue;
				left = min(left, x);
				right = max(right, x);
				top = min(top, y);
				bottom = max(bottom, y);
			}
		}
		if (right >= left)
			area = (Rectangle){left, top, right - left + 1, bottom - top + 1};
	}
	cg_free(cg);
	return area;
}

static void rebuild_background(struct parts *parts)
{
	struct parts_widget *w = parts->widget;
	if (w->defer_rebuild) {
		w->dirty_bg = true;
		return;
	}
	static const char *const suffixes[4][3] = {
		{ "／一般", "／通常", "／普通" },
		{ "／オン", "／on", "／ON" },
		{ "／DOWN", "／ダウン", "／down" },
		{ "／无效", "／無効", "／无効" }
	};
	static const char *const checked_suffixes[3] = {
		"／检查", "／チェック", "／選択中"
	};
	bool checkbox = parts->component_type == 1;
	for (int checked = 0; checked < (checkbox ? 2 : 1); checked++) {
	bool flat = w->override_flat[checked]->size || (!w->override_cg[checked]->size && w->flat_name->size);
	struct string *override = flat ? w->override_flat[checked] : w->override_cg[checked];
	struct string *base = flat ? w->flat_name : w->cg_name;
	if (override->size) base = override;
	struct string *checked_base = string_dup(base);
	if (checked && !override->size && base->size) {
		for (int cs = 0; cs < 3; cs++) {
			bool matched_checked = false;
			for (int enc_try = 0; enc_try < (sys4_text_is_gbk ? 2 : 1); enc_try++) {
				const char *suffix;
				if (enc_try == 0) {
					suffix = parts_utf8_to_cjk_literal(checked_suffixes[cs]);
				} else {
					sys4_set_text_gbk(false);
					suffix = parts_utf8_to_cjk_literal(checked_suffixes[cs]);
					sys4_set_text_gbk(true);
				}
				struct string *cand = string_dup(base);
				string_append_cstr(&cand, suffix, strlen(suffix));
				struct parts_state probe = {0};
				if (load_skin_any(parts, &probe, cand, flat, suffixes[0], 3)
						|| load_skin(parts, &probe, cand, flat, "")) {
					parts_state_reset(&probe, PARTS_UNINITIALIZED);
					free_string(checked_base);
					checked_base = cand;
					matched_checked = true;
					break;
				}
				free_string(cand);
			}
			if (matched_checked)
				break;
		}
	}
	for (int i = 0; i < 4; i++) {
		struct parts_state *state = checked
			? (i < PARTS_NR_STATES ? &w->checked_states[i] : &w->checked_disabled)
			: (i < PARTS_NR_STATES ? &parts->states[i] : &w->disabled);
		bool loaded = false;
		if (base->size) {
			loaded = load_skin_any(parts, state, checked_base, flat, suffixes[i], 3);
			if (!loaded && i && !(checkbox && i == 3))
				loaded = load_skin_any(parts, state, checked_base, flat, suffixes[0], 3);
			if (!loaded)
				loaded = load_skin(parts, state, checked_base, flat, "");
		}
		if (!base->size) {
			parts_state_reset(state, PARTS_CG);
			parts_set_dims(parts, &state->common, max(0, w->width), max(0, w->height));
			parts_widget_restore_plain_skin(parts, state, i, checked);
		}
	}
	free_string(checked_base);
	}
	if (game_okusama && !checkbox)
		w->label_area = button_label_area(&parts->states[PARTS_STATE_DEFAULT]);
	parts_recalculate_hitbox(parts);
	parts_component_dirty(parts);
	parts_dirty(parts);
}

static void rebuild_label(struct parts *parts)
{
	struct parts_widget *w = parts->widget;
	if (w->defer_rebuild) {
		w->dirty_label = true;
		return;
	}
	struct text_style style = w->label.text.ts;
	int line_space = w->label.text.line_space;
	parts_state_reset(&w->label, PARTS_TEXT);
	w->label.text.ts = style;
	w->label.text.ts.font_size = NULL;
	w->label.text.line_space = line_space;
	if (game_okusama && parts->component_type == 0 && style.face >= 256 && gfx_text_advance_edges)
		w->label.text.ts.font_spacing -= text_style_edge_width(&style);
	parts_text_append(parts, &w->label.text, w->text);
	w->label.text.ts.font_spacing = style.font_spacing;
	parts_recalculate_hitbox(parts);
	parts_component_dirty(parts);
	parts_dirty(parts);
}

void parts_widget_rebuild_label(struct parts *parts)
{
	if (!parts->widget) return;
	rebuild_label(parts);
}

void parts_widget_rebuild(struct parts *parts)
{
	if (!parts->widget) return;
	rebuild_background(parts);
	rebuild_label(parts);
}

static void parts_widget_create_type(struct parts *parts, int type, bool defer)
{
	if (parts->widget && parts->component_type == type) {
		if (defer)
			parts->widget->defer_rebuild = true;
		return;
	}
	parts_widget_free(parts);
	parts_panel_free(parts);
	parts_scrollbar_free(parts);
	parts_message_window_free(parts);
	struct parts_widget *w = parts->widget = xcalloc(1, sizeof(*w));
	w->enabled = true;
	w->width = type == 1 ? 16 : 200;
	w->height = type == 1 ? 16 : 30;
	w->color[0] = w->color[1] = w->color[2] = 200;
	w->text_origin = type == 1 ? 4 : 5;
	w->cg_name = string_ref(&EMPTY_STRING);
	w->flat_name = string_ref(&EMPTY_STRING);
	w->text = string_ref(&EMPTY_STRING);
	for (int i = 0; i < 2; i++) {
		w->override_cg[i] = string_ref(&EMPTY_STRING);
		w->override_flat[i] = string_ref(&EMPTY_STRING);
	}
	parts_state_reset(&w->label, PARTS_TEXT);
	w->label.text.ts.color = type == 1 ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){0, 0, 0, 255};
	parts->component_type = type;
	if (defer) {
		w->defer_rebuild = true;
		w->dirty_bg = true;
		w->dirty_label = true;
	} else {
		parts_widget_rebuild(parts);
	}
}

void parts_widget_create(struct parts *parts) { parts_widget_create_type(parts, 0, false); }
void parts_checkbox_create(struct parts *parts) { parts_widget_create_type(parts, 1, false); }

void parts_widget_create_deferred(struct parts *parts, int type)
{
	parts_widget_create_type(parts, type, true);
}

void parts_widget_end_batch(struct parts *parts)
{
	if (!parts || !parts->widget)
		return;
	struct parts_widget *w = parts->widget;
	w->defer_rebuild = false;
	bool do_bg = w->dirty_bg;
	bool do_label = w->dirty_label;
	w->dirty_bg = false;
	w->dirty_label = false;
	if (do_bg)
		rebuild_background(parts);
	if (do_label)
		rebuild_label(parts);
}

void parts_widget_get_size(struct parts *parts, int *width, int *height)
{
	struct parts_widget *w = parts->widget;
	struct parts_state *state = w->enabled ? &parts->states[parts->state] : &w->disabled;
	*width = state->common.w;
	*height = state->common.h;
	if (parts->component_type == 1 && !w->button_mode && w->text->size) {
		*width += w->label.common.w;
		*height = max(*height, w->label.common.h);
	}
}

struct parts_state *parts_display_state(struct parts *parts)
{
	if (parts->message_box || parts->panel) return &parts->states[0];
	if (parts->component_type == 1 && parts->widget && parts->widget->checked)
		return parts->widget->enabled ? &parts->widget->checked_states[parts->state] : &parts->widget->checked_disabled;
	return parts->widget && !parts->widget->enabled
		? &parts->widget->disabled : &parts->states[parts->state];
}

static struct parts_widget *button(int no)
{
	struct parts *parts = parts_get(no);
	parts_widget_create(parts);
	return parts->widget;
}

void PE_SetButtonSize(int no, int width, int height)
{
	struct parts_widget *w = button(no);
	if (w->width == width && w->height == height) return;
	w->width = width; w->height = height;
	rebuild_background(parts_get(no));
}

void PE_SetButtonEnable(int no, bool enabled)
{
	struct parts_widget *w = button(no);
	if (w->enabled == enabled) return;
	w->enabled = enabled;
	if (!enabled) parts_set_state(parts_get(no), PARTS_STATE_DEFAULT);
	parts_dirty(parts_get(no));
}

bool PE_IsButtonEnable(int no) { return button(no)->enabled; }

void PE_SetButtonColor(int no, int r, int g, int b)
{
	struct parts_widget *w = button(no);
	if (w->color[0] == r && w->color[1] == g && w->color[2] == b) return;
	w->color[0] = r; w->color[1] = g; w->color[2] = b;
	if (!w->cg_name->size && !w->flat_name->size) rebuild_background(parts_get(no));
}

int PE_GetButtonR(int no) { return button(no)->color[0]; }
int PE_GetButtonG(int no) { return button(no)->color[1]; }
int PE_GetButtonB(int no) { return button(no)->color[2]; }

static void widget_set_font(struct parts *parts, int type, int size, int r, int g, int b,
		float bold, int edge_r, int edge_g, int edge_b, float edge)
{
	struct text_style *ts = &parts->widget->label.text.ts;
	SDL_Color color = {r, g, b, 255};
	SDL_Color edge_color = {edge_r, edge_g, edge_b, 255};
	float old_left = ts->edge_left, old_up = ts->edge_up, old_right = ts->edge_right, old_down = ts->edge_down;
	text_style_set_edge_width(ts, edge);
	if (ts->face == type && ts->size == size && !memcmp(&ts->color, &color, sizeof(color))
			&& ts->bold_width == bold && !memcmp(&ts->edge_color, &edge_color, sizeof(edge_color))
			&& ts->edge_left == old_left && ts->edge_up == old_up
			&& ts->edge_right == old_right && ts->edge_down == old_down)
		return;
	ts->face = type; ts->size = size;
	ts->color = color;
	ts->bold_width = bold; ts->weight = bold * 1000;
	ts->edge_color = edge_color;
	rebuild_label(parts);
}

static void widget_get_font(struct parts *parts, int *type, int *size, int *r, int *g, int *b,
		float *bold, int *edge_r, int *edge_g, int *edge_b, float *edge)
{
	struct text_style *ts = &parts->widget->label.text.ts;
	*type = ts->face; *size = ts->size;
	*r = ts->color.r; *g = ts->color.g; *b = ts->color.b;
	*bold = ts->bold_width; *edge = ts->edge_left;
	*edge_r = ts->edge_color.r; *edge_g = ts->edge_color.g; *edge_b = ts->edge_color.b;
}

void PE_SetButtonFontProperty(int no, int type, int size, int r, int g, int b,
		float bold, int edge_r, int edge_g, int edge_b, float edge)
{
	button(no);
	widget_set_font(parts_get(no), type, size, r, g, b, bold, edge_r, edge_g, edge_b, edge);
}

void PE_GetButtonFontProperty(int no, int *type, int *size, int *r, int *g, int *b,
		float *bold, int *edge_r, int *edge_g, int *edge_b, float *edge)
{
	button(no);
	widget_get_font(parts_get(no), type, size, r, g, b, bold, edge_r, edge_g, edge_b, edge);
}

void PE_SetButtonCGName(int no, struct string *name)
{
	struct parts_widget *w = button(no);
	if (!strcmp(w->cg_name->text, name->text)) return;
	set_string(&w->cg_name, name);
	rebuild_background(parts_get(no));
}
struct string *PE_GetButtonCGName(int no) { return string_ref(button(no)->cg_name); }

void PE_SetButtonFlatName(int no, struct string *name)
{
	struct parts_widget *w = button(no);
	if (!strcmp(w->flat_name->text, name->text)) return;
	set_string(&w->flat_name, name);
	rebuild_background(parts_get(no));
}
struct string *PE_GetButtonFlatName(int no) { return string_ref(button(no)->flat_name); }

void PE_SetButtonText(int no, struct string *text)
{
	struct parts_widget *w = button(no);
	if (!strcmp(w->text->text, text->text)) return;
	set_string(&w->text, text);
	rebuild_label(parts_get(no));
}
struct string *PE_GetButtonText(int no) { return string_ref(button(no)->text); }

void PE_SetButtonTextOriginPosMode(int no, int mode)
{
	struct parts_widget *w = button(no);
	if (w->text_origin == mode) return;
	w->text_origin = mode;
	parts_dirty(parts_get(no));
}
int PE_GetButtonTextOriginPosMode(int no) { return button(no)->text_origin; }

void PE_SetButtonCharSpace(int no, int space)
{
	struct parts_widget *w = button(no);
	if (w->label.text.ts.font_spacing == space) return;
	w->label.text.ts.font_spacing = space;
	rebuild_label(parts_get(no));
}
int PE_GetButtonCharSpace(int no) { return button(no)->label.text.ts.font_spacing; }

void PE_SetButtonLineSpace(int no, int space)
{
	struct parts_widget *w = button(no);
	if (w->label.text.line_space == space) return;
	w->label.text.line_space = space;
	rebuild_label(parts_get(no));
}
int PE_GetButtonLineSpace(int no) { return button(no)->label.text.line_space; }

static struct parts_widget *checkbox(int no)
{
	struct parts *parts = parts_get(no);
	parts_checkbox_create(parts);
	return parts->widget;
}

void PE_SetCheckBoxSize(int no, int width, int height)
{
	struct parts_widget *w = checkbox(no);
	if (w->width == width && w->height == height) return;
	w->width = width;
	w->height = height;
	rebuild_background(parts_get(no));
}
int PE_GetCheckBoxButtonWidth(int no) { checkbox(no); return parts_get(no)->states[0].common.w; }
int PE_GetCheckBoxButtonHeight(int no) { checkbox(no); return parts_get(no)->states[0].common.h; }

void PE_SetCheckBoxEnable(int no, bool enabled)
{
	struct parts_widget *w = checkbox(no);
	if (w->enabled == enabled) return;
	w->enabled = enabled;
	struct parts *parts = parts_get(no);
	if (!enabled) parts_set_state(parts, PARTS_STATE_DEFAULT);
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}
bool PE_IsCheckBoxEnable(int no) { return checkbox(no)->enabled; }

static struct parts *find_radio_button_box_parent(int child_no)
{
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (p->component_type != 9)
			continue;
		for (int i = 0; i < p->nr_radio_children; i++) {
			if (p->radio_children[i] == child_no)
				return p;
		}
	}
	return NULL;
}

void parts_widget_check(struct parts *parts, bool checked)
{
	struct parts_widget *w = parts->widget;
	if (!w || !w->enabled || parts->component_type != 1 || w->checked == checked) return;
	struct parts *rbox = find_radio_button_box_parent(parts->no);
	if (rbox) {
		if (!checked)
			return;
		parts_msg_push(rbox, PARTS_MSG_RADIO_BUTTON_BOX, "");
		for (int i = 0; i < rbox->nr_radio_children; i++) {
			int sib_no = rbox->radio_children[i];
			if (sib_no == parts->no)
				continue;
			struct parts *sib = parts_try_get(sib_no);
			if (sib && sib->widget && sib->widget->checked) {
				sib->widget->checked = false;
				parts_msg_push(sib, PARTS_MSG_CHECK_CHANGED, "b", false);
				parts_dirty(sib);
			}
		}
	}
	w->checked = checked;
	parts_msg_push(parts, PARTS_MSG_CHECK_CHANGED, "b", checked);
	parts_dirty(parts);
}
void PE_CheckBoxChecked(int no, bool checked) { checkbox(no); parts_widget_check(parts_get(no), checked); }
bool PE_IsCheckBoxChecked(int no) { return checkbox(no)->checked; }

void PE_SetCheckBoxColor(int no, int r, int g, int b)
{
	struct parts_widget *w = checkbox(no);
	if (w->color[0] == r && w->color[1] == g && w->color[2] == b) return;
	w->color[0] = r;
	w->color[1] = g;
	w->color[2] = b;
	if (!w->cg_name->size && !w->flat_name->size
			&& (!w->override_cg[0]->size || !w->override_cg[1]->size)
			&& (!w->override_flat[0]->size || !w->override_flat[1]->size))
		rebuild_background(parts_get(no));
}
int PE_GetCheckBoxR(int no) { return checkbox(no)->color[0]; }
int PE_GetCheckBoxG(int no) { return checkbox(no)->color[1]; }
int PE_GetCheckBoxB(int no) { return checkbox(no)->color[2]; }

void PE_SetCheckBoxFontProperty(int no, int type, int size, int r, int g, int b,
		float bold, int edge_r, int edge_g, int edge_b, float edge)
{
	checkbox(no);
	widget_set_font(parts_get(no), type, size, r, g, b, bold, edge_r, edge_g, edge_b, edge);
}
void PE_GetCheckBoxFontProperty(int no, int *type, int *size, int *r, int *g, int *b,
		float *bold, int *edge_r, int *edge_g, int *edge_b, float *edge)
{
	checkbox(no);
	widget_get_font(parts_get(no), type, size, r, g, b, bold, edge_r, edge_g, edge_b, edge);
}

#define CHECKBOX_SKIN(property, member) \
void PE_SetCheckBox##property(int no, struct string *name) \
{ \
	struct parts_widget *w = checkbox(no); \
	if (!strcmp(w->member->text, name->text)) return; \
	set_string(&w->member, name); \
	rebuild_background(parts_get(no)); \
} \
struct string *PE_GetCheckBox##property(int no) { return string_ref(checkbox(no)->member); }
CHECKBOX_SKIN(CGName, cg_name)
CHECKBOX_SKIN(FlatName, flat_name)
CHECKBOX_SKIN(OverrideCGName, override_cg[0])
CHECKBOX_SKIN(OverrideCheckedCGName, override_cg[1])
CHECKBOX_SKIN(OverrideFlatName, override_flat[0])
CHECKBOX_SKIN(OverrideCheckedFlatName, override_flat[1])
#undef CHECKBOX_SKIN

void PE_SetCheckBoxText(int no, struct string *text)
{
	struct parts_widget *w = checkbox(no);
	if (!strcmp(w->text->text, text->text)) return;
	set_string(&w->text, text);
	rebuild_label(parts_get(no));
}
struct string *PE_GetCheckBoxText(int no) { return string_ref(checkbox(no)->text); }
void PE_SetCheckBoxTextOriginPosMode(int no, int mode)
{
	struct parts_widget *w = checkbox(no);
	if (w->text_origin == mode) return;
	w->text_origin = mode;
	parts_dirty(parts_get(no));
}
int PE_GetCheckBoxTextOriginPosMode(int no) { return checkbox(no)->text_origin; }
void PE_SetCheckBoxButtonMode(int no, bool enabled)
{
	struct parts_widget *w = checkbox(no);
	if (w->button_mode == enabled) return;
	w->button_mode = enabled;
	struct parts *parts = parts_get(no);
	if (!w->cg_name->size && !w->flat_name->size && !w->override_cg[1]->size && !w->override_flat[1]->size)
		rebuild_background(parts);
	else {
		parts_recalculate_hitbox(parts);
		parts_dirty(parts);
	}
}
bool PE_IsCheckBoxButtonMode(int no) { return checkbox(no)->button_mode; }
void PE_SetCheckBoxCharSpace(int no, int space)
{
	struct parts_widget *w = checkbox(no);
	if (w->label.text.ts.font_spacing == space) return;
	w->label.text.ts.font_spacing = space;
	rebuild_label(parts_get(no));
}
int PE_GetCheckBoxCharSpace(int no) { return checkbox(no)->label.text.ts.font_spacing; }
void PE_SetCheckBoxLineSpace(int no, int space)
{
	struct parts_widget *w = checkbox(no);
	if (w->label.text.line_space == space) return;
	w->label.text.line_space = space;
	rebuild_label(parts_get(no));
}
int PE_GetCheckBoxLineSpace(int no) { return checkbox(no)->label.text.line_space; }

void parts_panel_free(struct parts *parts)
{
	free(parts->panel);
	parts->panel = NULL;
}

static int panel_gradient(int coordinate, int dimension, int before, int after)
{
	int alpha = 255;
	if (before > 0 && coordinate < before)
		alpha = (int64_t)255 * coordinate / before;
	if (after > 0 && coordinate > dimension - (int64_t)after)
		alpha = alpha * (255 - (int64_t)255 * (coordinate - (dimension - (int64_t)after)) / after) / 255;
	return alpha;
}

void parts_panel_rebuild(struct parts *parts)
{
	struct parts_panel *p = parts->panel;
	struct parts_state *state = &parts->states[0];
	parts_state_reset(state, PARTS_CG);
	parts_set_dims(parts, &state->common, p->width, p->height);
	if (p->width > 0 && p->height > 0) {
		if ((size_t)p->width > INT_MAX / 4 / (size_t)p->height)
			VM_ERROR("Parts panel texture is too large");
		SDL_Color color = {min(255, max(0, p->color[0])), min(255, max(0, p->color[1])),
			min(255, max(0, p->color[2])), min(255, max(0, p->color[3]))};
		if (!(p->gradient[0] || p->gradient[1] || p->gradient[2] || p->gradient[3])) {
			gfx_init_texture_rgba(&state->common.texture, p->width, p->height, color);
		} else {
			SDL_Color *pixels = xmalloc((size_t)p->width * p->height * sizeof(*pixels));
			for (int y = 0; y < p->height; y++) {
				int ay = panel_gradient(y, p->height, p->gradient[0], p->gradient[1]);
				for (int x = 0; x < p->width; x++) {
					int ax = panel_gradient(x, p->width, p->gradient[2], p->gradient[3]);
					SDL_Color c = color;
					c.a = color.a * (ay * ax / 255) / 255;
					pixels[(size_t)y * p->width + x] = c;
				}
			}
			gfx_init_texture_with_pixels(&state->common.texture, p->width, p->height, pixels);
			free(pixels);
		}
	}
	parts_component_dirty(parts);
	parts_dirty(parts);
}

void parts_panel_create(struct parts *parts)
{
	if (parts->panel) return;
	parts_widget_free(parts);
	parts_scrollbar_free(parts);
	parts_message_window_free(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++) parts_state_reset(&parts->states[i], PARTS_UNINITIALIZED);
	parts->panel = xcalloc(1, sizeof(*parts->panel));
	parts->panel->width = parts->panel->height = 200;
	parts->panel->color[0] = parts->panel->color[1] = parts->panel->color[2] = 220;
	parts->panel->color[3] = 255;
	parts->component_type = 12;
	parts->state = PARTS_STATE_DEFAULT;
	parts_panel_rebuild(parts);
}

static struct parts_panel *panel(int no)
{
	struct parts *parts = parts_get(no);
	parts_panel_create(parts);
	return parts->panel;
}

void PE_SetPanelSize(int no, int width, int height)
{
	struct parts_panel *p = panel(no);
	if (p->width == width && p->height == height) return;
	p->width = width;
	p->height = height;
	parts_panel_rebuild(parts_get(no));
}

void PE_SetPanelColor(int no, int r, int g, int b, int a)
{
	struct parts_panel *p = panel(no);
	int color[4] = {r, g, b, a};
	if (!memcmp(p->color, color, sizeof(color))) return;
	memcpy(p->color, color, sizeof(color));
	parts_panel_rebuild(parts_get(no));
}
int PE_GetPanelR(int no) { return panel(no)->color[0]; }
int PE_GetPanelG(int no) { return panel(no)->color[1]; }
int PE_GetPanelB(int no) { return panel(no)->color[2]; }
int PE_GetPanelA(int no) { return panel(no)->color[3]; }

#define PANEL_GRADIENT(direction, index) \
void PE_SetPanelAlphaGradation##direction(int no, int size) \
{ \
	struct parts_panel *p = panel(no); \
	if (p->gradient[index] == max(0, size)) return; \
	p->gradient[index] = max(0, size); \
	parts_panel_rebuild(parts_get(no)); \
} \
int PE_GetPanelAlphaGradation##direction(int no) { return panel(no)->gradient[index]; }
PANEL_GRADIENT(Top, 0)
PANEL_GRADIENT(Bottom, 1)
PANEL_GRADIENT(Left, 2)
PANEL_GRADIENT(Right, 3)
#undef PANEL_GRADIENT

void PE_SetComponentCheckBoxShowLinkNumber(int no, int cb_no)
{
	struct parts *parts = parts_get(no);
	int link = max(0, cb_no);
	if (parts->checkbox_show_link == link) return;
	parts->checkbox_show_link = link;
	parts_dirty(parts);
}

int PE_GetComponentCheckBoxShowLinkNumber(int no)
{
	return parts_get(no)->checkbox_show_link;
}

void PE_AddRadioButtonBoxChild(int no, int child_no)
{
	struct parts *p = parts_get(no);
	if (p->component_type != 9)
		PE_SetSpecialComponentType(no, 9);
	bool has_checked = false;
	for (int i = 0; i < p->nr_radio_children; i++) {
		if (p->radio_children[i] == child_no)
			return;
		struct parts *sib = parts_try_get(p->radio_children[i]);
		if (sib && sib->widget && sib->widget->checked)
			has_checked = true;
	}
	struct parts *child = parts_try_get(child_no);
	if (has_checked && child && child->widget && child->widget->checked) {
		child->widget->checked = false;
		parts_dirty(child);
	}
	p->radio_children = xrealloc(p->radio_children, (p->nr_radio_children + 1) * sizeof(int));
	p->radio_children[p->nr_radio_children++] = child_no;
}

int PE_NumofRadioButtonBoxChild(int no)
{
	struct parts *p = parts_try_get(no);
	return p ? p->nr_radio_children : 0;
}

int PE_GetRadioButtonBoxChild(int no, int index)
{
	struct parts *p = parts_try_get(no);
	if (!p || index < 0 || index >= p->nr_radio_children)
		return 0;
	return p->radio_children[index];
}

bool PE_IsRadioButtonBoxExistGUI(int no, int child_no)
{
	struct parts *p = parts_try_get(no);
	if (!p)
		return false;
	for (int i = 0; i < p->nr_radio_children; i++) {
		if (p->radio_children[i] == child_no)
			return true;
	}
	return false;
}

void PE_RemoveRadioButtonBoxChild(int no, int child_no)
{
	struct parts *p = parts_try_get(no);
	if (!p)
		return;
	for (int i = 0; i < p->nr_radio_children; i++) {
		if (p->radio_children[i] == child_no) {
			memmove(&p->radio_children[i], &p->radio_children[i + 1],
					(p->nr_radio_children - i - 1) * sizeof(int));
			p->nr_radio_children--;
			return;
		}
	}
}

void PE_ClearRadioButtonBoxChild(int no)
{
	struct parts *p = parts_try_get(no);
	if (!p)
		return;
	free(p->radio_children);
	p->radio_children = NULL;
	p->nr_radio_children = 0;
}

void parts_text_box_set_display(struct parts *parts, const char *composition)
{
	if (parts->component_type != 4)
		return;
	if (!parts->text_box_label)
		parts->text_box_label = xcalloc(1, sizeof(*parts->text_box_label));
	struct parts_state *label = parts->text_box_label;
	parts_state_reset(label, PARTS_TEXT);
	for (const char *p = parts->text_box_text->text; *p;)
		p = parts_text_append_char(&label->text, p);
	for (const char *p = composition; p && *p;)
		p = parts_text_append_char(&label->text, p);
	parts_text_update_dims(parts, &label->text);
	parts_dirty(parts);
}

void PE_SetTextBoxText(int no, struct string *text)
{
	struct parts *p = parts_get(no);
	set_string(&p->text_box_text, text ? text : &EMPTY_STRING);
	parts_text_box_set_display(p, NULL);
}

struct string *PE_GetTextBoxText(int no)
{
	struct parts *p = parts_get(no);
	return string_ref(p->text_box_text ? p->text_box_text : &EMPTY_STRING);
}

void PE_SetTextBoxReadOnly(int no, bool read_only)
{
	parts_get(no)->text_box_read_only = read_only;
}

bool PE_IsTextBoxReadOnly(int no)
{
	return parts_get(no)->text_box_read_only;
}

void PE_SetTextBoxMaxTextLength(int no, int length)
{
	parts_get(no)->text_box_max_length = length;
}

int PE_GetTextBoxMaxTextLength(int no)
{
	return parts_get(no)->text_box_max_length;
}
