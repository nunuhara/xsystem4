/* Native AIN v12 message windows, GPL-2.0-or-later. */
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "parts.h"
#include "parts_message_window.h"
#include "vm.h"
#include "parts_internal.h"

static void set_string(struct string **out, struct string *value)
{
	struct string *copy = string_dup(value);
	free_string(*out);
	*out = copy;
}

void parts_message_window_free(struct parts *parts)
{
	struct parts_message_window *w = parts->message_box;
	if (!w) return;
	parts_state_reset(&w->label, PARTS_UNINITIALIZED);
	parts_state_reset(&w->key_wait, PARTS_UNINITIALIZED);
	free_string(w->cg_name);
	free_string(w->flat_name);
	free_string(w->text);
	free_string(w->function);
	free_string(w->key_cg_name);
	free_string(w->key_flat_name);
	free(w);
	parts->message_box = NULL;
}

void parts_message_window_create(struct parts *parts)
{
	if (parts->message_box) return;
	parts_panel_free(parts);
	parts_scrollbar_free(parts);
	parts_widget_free(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++)
		parts_state_reset(&parts->states[i], PARTS_UNINITIALIZED);
	struct parts_message_window *w = parts->message_box = xcalloc(1, sizeof(*w));
	w->inactive_color[0] = w->inactive_color[1] = w->inactive_color[2] = 255;
	w->text_origin = 1;
	w->text_speed = 23;
	w->text_area = (Rectangle){10, 15, 0, 0};
	w->speed_rate = 1.;
	w->message = -1;
	w->cg_name = string_ref(&EMPTY_STRING);
	w->flat_name = string_ref(&EMPTY_STRING);
	w->text = string_ref(&EMPTY_STRING);
	w->function = string_ref(&EMPTY_STRING);
	w->key_cg_name = string_ref(&EMPTY_STRING);
	w->key_flat_name = string_ref(&EMPTY_STRING);
	parts_state_reset(&w->label, PARTS_TEXT);
	w->label.text.ts.size = 26;
	w->label.text.line_space = 3;
	parts->component_type = 10;
	parts->state = PARTS_STATE_DEFAULT;
	parts_component_dirty(parts);
}

static struct parts_message_window *window(int no)
{
	struct parts *parts = parts_get(no);
	parts_message_window_create(parts);
	return parts->message_box;
}

static void rebuild_background(struct parts *parts)
{
	struct parts_message_window *w = parts->message_box;
	struct parts_state *state = &parts->states[0];
	bool flat = w->flat_name->size != 0;
	parts_state_reset(state, flat ? PARTS_FLAT : PARTS_CG);
	if (flat) parts_flat_load(parts, &state->flat, w->flat_name);
	else if (w->cg_name->size) parts_cg_set(parts, &state->cg, w->cg_name);
	parts_component_dirty(parts);
	parts_dirty(parts);
}

static void rebuild_key_wait(struct parts *parts)
{
	struct parts_message_window *w = parts->message_box;
	if (w->key_flat_name->size) {
		parts_state_reset(&w->key_wait, PARTS_FLAT);
		parts_flat_load(parts, &w->key_wait.flat, w->key_flat_name);
	} else if (w->key_cg_name->size && w->key_count > 0) {
		parts_state_reset(&w->key_wait, PARTS_ANIMATION);
		parts_animation_set_cg(parts, &w->key_wait.anim, w->key_cg_name,
				w->key_start, w->key_count, max(1, w->key_time));
	} else {
		parts_state_reset(&w->key_wait, PARTS_CG);
		if (w->key_cg_name->size) parts_cg_set(parts, &w->key_wait.cg, w->key_cg_name);
	}
	parts_dirty(parts);
}

struct message_font_frame {
	struct text_style style;
	int line_space;
};

static void font_tag(struct parts_text *text, char *tag)
{
	struct text_style *ts = &text->ts;
	while (*tag) {
		while (isspace((unsigned char)*tag)) tag++;
		char *key = tag;
		while (*tag && *tag != '=' && !isspace((unsigned char)*tag)) tag++;
		if (*tag != '=') break;
		*tag++ = 0;
		char *end;
		double value = strtod(tag, &end);
		if (end == tag || !isfinite(value)) break;
		tag = end;
		// The script constructs these attributes separately, and can nest them.
		if (!strcmp(key, "type")) ts->face = value;
		else if (!strcmp(key, "size")) ts->size = min(512., max(1., value));
		else if (!strcmp(key, "r")) ts->color.r = min(255., max(0., value));
		else if (!strcmp(key, "g")) ts->color.g = min(255., max(0., value));
		else if (!strcmp(key, "b")) ts->color.b = min(255., max(0., value));
		else if (!strcmp(key, "bold")) {
			ts->bold_width = min(32., max(0., value)); ts->weight = ts->bold_width * 1000;
		} else if (!strcmp(key, "edge")) text_style_set_edge_width(ts, min(32., max(0., value)));
		else if (!strcmp(key, "er")) ts->edge_color.r = min(255., max(0., value));
		else if (!strcmp(key, "eg")) ts->edge_color.g = min(255., max(0., value));
		else if (!strcmp(key, "eb")) ts->edge_color.b = min(255., max(0., value));
		else if (!strcmp(key, "tracking")) ts->font_spacing = min(4096., max(-4096., value));
		else if (!strcmp(key, "leading")) text->line_space = min(4096., max(-4096., value));
	}
	ts->font_size = NULL;
}

void parts_message_window_rebuild_text(struct parts *parts)
{
	struct parts_message_window *w = parts->message_box;
	struct text_style base = w->label.text.ts;
	struct text_style ruby_base = w->label.text.ruby_ts;
	int ruby_char_space = w->label.text.ruby_char_space;
	int line_space = w->label.text.line_space;
	parts_state_reset(&w->label, PARTS_TEXT);
	struct parts_text *t = &w->label.text;
	t->ts = base;
	t->ts.font_size = NULL;
	t->ruby_ts = ruby_base;
	t->ruby_ts.font_size = NULL;
	t->ruby_char_space = ruby_char_space;
	t->line_space = line_space;
	struct message_font_frame *fonts = NULL;
	double *times = NULL, time = floor(max(0, w->text_speed) * w->speed_rate);
	int nr_fonts = 0, nr_times = 0;
	double cursor = 0;
	w->finish_time = 0;
	const char *src = w->text->text;
	while (*src) {
		if (src[0] == '$' && src[1] == '{') {
			const char *end = strchr(src + 2, '}');
			if (end) {
				char *tag = xmalloc(end - src - 1);
				memcpy(tag, src + 2, end - src - 2);
				tag[end - src - 2] = 0;
				bool handled = true;
				if (!strncmp(tag, "font ", 5)) {
					fonts = xrealloc(fonts, (nr_fonts + 1) * sizeof(*fonts));
					fonts[nr_fonts++] = (struct message_font_frame){t->ts, t->line_space};
					font_tag(t, tag + 5);
				} else if (!strcmp(tag, "/font")) {
					if (nr_fonts) {
						t->ts = fonts[--nr_fonts].style;
						t->line_space = fonts[nr_fonts].line_space;
					}
				} else if (!strncmp(tag, "time ", 5)) {
					times = xrealloc(times, (nr_times + 1) * sizeof(*times));
					times[nr_times++] = time;
					double value = strtod(tag + 5, NULL);
					time = isfinite(value) ? floor(min(INT_MAX, max(0., value)) * w->speed_rate) : 0;
				} else if (!strcmp(tag, "/time")) {
					if (nr_times) time = times[--nr_times];
				} else handled = false;
				free(tag);
				if (handled) { src = end + 1; continue; }
			}
		}
		if (*src == '\r') { src++; continue; }
		if (*src == '\n') { src = parts_text_append_char(t, src); continue; }
		// A zero area dimension means unbounded, as in the game's PACTs.
		if (w->text_area.w > 0 && t->nr_lines && t->lines[t->nr_lines - 1].nr_chars) {
			char ch[3] = {src[0], 0, 0};
			if (SJIS_2BYTE(src[0]) && src[1]) ch[1] = src[1];
			struct text_style eff_ts = parts_get_effective_text_style(&t->ts);
			if (t->lines[t->nr_lines - 1].width + gfx_size_char(&eff_ts, ch) > w->text_area.w)
				parts_text_append_char(t, "\n");
		}
		src = parts_text_append_char(t, src);
		struct parts_text_line *line = &t->lines[t->nr_lines - 1];
		line->line_space = t->line_space;
		struct parts_text_char *ch = &line->chars[line->nr_chars - 1];
		ch->fade_start = cursor;
		ch->fade_time = time > 0 ? 200. : 0;
		w->finish_time = max(w->finish_time, cursor + ch->fade_time);
		cursor += time;
	}
	free(fonts);
	free(times);
	t->ts = base;
	t->ts.font_size = NULL;
	t->line_space = line_space;
	parts_text_update_dims(parts, t);
	if (parts_get_modifier_font_scale() > 100 && w->text_area.h > 0 && t->nr_lines > 1) {
		int avail_h = w->text_area.h;
		int bg_h = parts->states[0].common.h - w->text_area.y - 4;
		if (bg_h > avail_h)
			avail_h = bg_h;
		if (t->common.h > avail_h) {
			int sum_h = 0;
			for (unsigned i = 0; i < t->nr_lines; i++)
				sum_h += t->lines[i].height;
			int gaps = (int)t->nr_lines - 1;
			int target_space = (avail_h - sum_h) / gaps;
			int min_space = min(-2, line_space);
			if (target_space < min_space)
				target_space = min_space;
			for (unsigned i = 0; i + 1 < t->nr_lines; i++) {
				if (t->lines[i].line_space > target_space)
					t->lines[i].line_space = target_space;
			}
			parts_text_update_dims(parts, t);
		}
	}
	parts_dirty(parts);
}

bool parts_message_window_update(struct parts *parts, int passed_time)
{
	struct parts_message_window *w = parts->message_box;
	bool dirty = false;
	if (!w->fixed) {
		w->elapsed += min(max(0, passed_time), INT_MAX - w->elapsed);
		w->fixed = w->elapsed >= w->finish_time;
		dirty = true;
	}
	if (w->key_show) dirty |= parts_state_update(&w->key_wait, passed_time);
	return dirty;
}

void PE_SetMessageWindowActive(int no, bool active)
{
	window(no)->active = active;
	parts_dirty(parts_get(no));
}

void PE_SetMessageWindowInactiveMultipleColor(int no, int r, int g, int b)
{
	struct parts_message_window *w = window(no);
	w->inactive_color[0] = r; w->inactive_color[1] = g; w->inactive_color[2] = b;
	parts_dirty(parts_get(no));
}
int PE_GetMessageWindowInactiveMultipleColorR(int no) { return window(no)->inactive_color[0]; }
int PE_GetMessageWindowInactiveMultipleColorG(int no) { return window(no)->inactive_color[1]; }
int PE_GetMessageWindowInactiveMultipleColorB(int no) { return window(no)->inactive_color[2]; }

void PE_SetMessageWindowCGName(int no, struct string *name)
{
	set_string(&window(no)->cg_name, name);
	rebuild_background(parts_get(no));
}
struct string *PE_GetMessageWindowCGName(int no) { return string_ref(window(no)->cg_name); }
void PE_SetMessageWindowFlatName(int no, struct string *name)
{
	set_string(&window(no)->flat_name, name);
	rebuild_background(parts_get(no));
}
struct string *PE_GetMessageWindowFlatName(int no) { return string_ref(window(no)->flat_name); }
void PE_SetMessageWindowFlatShowWaitFrameNumber(int no, int frame) { window(no)->flat_wait_frame = frame; }
int PE_GetMessageWindowFlatShowWaitFrameNumber(int no) { return window(no)->flat_wait_frame; }
bool PE_IsOverMessageWindowFlatShowWaitFrame(int no)
{
	struct parts_message_window *w = window(no);
	struct parts_state *s = &parts_get(no)->states[0];
	return s->type != PARTS_FLAT || !s->flat.flat
		|| PE_GetPartsFlatCurrentFrameNumber(no, 1) >= w->flat_wait_frame;
}
bool PE_BackMessageWindowFlatBeginFrame(int no)
{
	window(no);
	return parts_get(no)->states[0].type != PARTS_FLAT || PE_BackPartsFlatBeginFrame(no, 1);
}
bool PE_StepMessageWindowFlatFinalFrame(int no)
{
	window(no);
	return parts_get(no)->states[0].type == PARTS_FLAT && PE_StepPartsFlatFinalFrame(no, 1);
}

void PE_SetKeyWaitCGName(int no, struct string *name, int start, int count, int time)
{
	struct parts_message_window *w = window(no);
	set_string(&w->key_cg_name, name);
	w->key_start = start; w->key_count = count; w->key_time = time;
	rebuild_key_wait(parts_get(no));
}
void PE_GetKeyWaitCGName(int no, struct string **name, int *start, int *count, int *time)
{
	struct parts_message_window *w = window(no);
	set_string(name, w->key_cg_name);
	*start = w->key_start; *count = w->key_count; *time = w->key_time;
}
void PE_SetKeyWaitFlatName(int no, struct string *name)
{
	set_string(&window(no)->key_flat_name, name);
	rebuild_key_wait(parts_get(no));
}
struct string *PE_GetKeyWaitFlatName(int no) { return string_ref(window(no)->key_flat_name); }
void PE_SetKeyWaitPos(int no, int x, int y, int z)
{
	struct parts_message_window *w = window(no);
	w->key_x = x; w->key_y = y; w->key_z = z;
	parts_dirty(parts_get(no));
}
int PE_GetKeyWaitPosX(int no) { return window(no)->key_x; }
int PE_GetKeyWaitPosY(int no) { return window(no)->key_y; }
int PE_GetKeyWaitPosZ(int no) { return window(no)->key_z; }
void PE_SetKeyWaitShow(int no, bool show)
{
	window(no)->key_show = show;
	parts_dirty(parts_get(no));
}
bool PE_IsKeyWaitShow(int no) { return window(no)->key_show; }

void PE_SetMessageWindowText(int no, struct string *text, int message,
		struct string *function, int version, int step)
{
	struct parts_message_window *w = window(no);
	w->message = message; w->version = version; w->step = step;
	set_string(&w->function, function);
	// Reassigning identical text preserves an already completed fade.
	if (!strcmp(w->text->text, text->text)) return;
	set_string(&w->text, text);
	w->elapsed = 0;
	w->fixed = false;
	w->speed_rate = 1.;
	int fn = ain_get_function(ain, "message::detail::GetMessageSpeedRate");
	if (fn >= 0 && ain->functions[fn].nr_args == 0 && ain->functions[fn].return_type.data == AIN_FLOAT) {
		vm_call(fn, -1);
		float rate = stack_pop().f;
		if (isfinite(rate)) w->speed_rate = min(1000.f, max(0.f, rate));
	}
	parts_message_window_rebuild_text(parts_get(no));
}
struct string *PE_GetMessageWindowText(int no) { return string_ref(window(no)->text); }
void PE_FixMessageWindowText(int no)
{
	window(no)->fixed = true;
	parts_dirty(parts_get(no));
}
bool PE_IsFixedMessageWindowText(int no) { return window(no)->fixed; }
void PE_SetMessageWindowTextArea(int no, int x, int y, int width, int height)
{
	window(no)->text_area = (Rectangle){x, y, width, height};
	parts_message_window_rebuild_text(parts_get(no));
}
void PE_GetMessageWindowTextArea(int no, int *x, int *y, int *width, int *height)
{
	Rectangle a = window(no)->text_area;
	*x = a.x; *y = a.y; *width = a.w; *height = a.h;
}
void PE_SetMessageWindowTextOriginPosMode(int no, int mode)
{
	window(no)->text_origin = mode;
	parts_dirty(parts_get(no));
}
int PE_GetMessageWindowTextOriginPosMode(int no) { return window(no)->text_origin; }
void PE_SetMessageWindowTextFont(int no, int face, int size, int r, int g, int b,
		float bold, int edge_r, int edge_g, int edge_b, float edge)
{
	struct text_style *ts = &window(no)->label.text.ts;
	ts->face = face; ts->size = size;
	ts->color = (SDL_Color){r, g, b, 255};
	ts->bold_width = bold; ts->weight = bold * 1000;
	ts->edge_color = (SDL_Color){edge_r, edge_g, edge_b, 255};
	text_style_set_edge_width(ts, edge);
	parts_message_window_rebuild_text(parts_get(no));
}
void PE_GetMessageWindowTextFont(int no, int *face, int *size, int *r, int *g, int *b,
		float *bold, int *edge_r, int *edge_g, int *edge_b, float *edge)
{
	struct text_style *ts = &window(no)->label.text.ts;
	*face = ts->face; *size = ts->size;
	*r = ts->color.r; *g = ts->color.g; *b = ts->color.b;
	*bold = ts->bold_width; *edge = ts->edge_left;
	*edge_r = ts->edge_color.r; *edge_g = ts->edge_color.g; *edge_b = ts->edge_color.b;
}
void PE_SetMessageWindowTextSpeed(int no, int speed)
{
	window(no)->text_speed = speed;
	parts_message_window_rebuild_text(parts_get(no));
}
int PE_GetMessageWindowTextSpeed(int no) { return window(no)->text_speed; }
void PE_SetMessageWindowTextSpace(int no, int letter, int line)
{
	struct parts_message_window *w = window(no);
	w->label.text.ts.font_spacing = letter;
	w->label.text.line_space = line;
	parts_message_window_rebuild_text(parts_get(no));
}
void PE_GetMessageWindowTextSpace(int no, int *letter, int *line)
{
	struct parts_message_window *w = window(no);
	*letter = w->label.text.ts.font_spacing; *line = w->label.text.line_space;
}
void PE_SetMessageWindowRubyFont(int no, int face, int size, int r, int g, int b,
		float bold, int edge_r, int edge_g, int edge_b, float edge)
{
	struct text_style *ts = &window(no)->label.text.ruby_ts;
	ts->face = face; ts->size = size;
	ts->color = (SDL_Color){r, g, b, 255};
	ts->bold_width = bold; ts->weight = bold * 1000;
	ts->edge_color = (SDL_Color){edge_r, edge_g, edge_b, 255};
	text_style_set_edge_width(ts, edge);
	ts->font_size = NULL;
	parts_message_window_rebuild_text(parts_get(no));
}
void PE_GetMessageWindowRubyFont(int no, int *face, int *size, int *r, int *g, int *b,
		float *bold, int *edge_r, int *edge_g, int *edge_b, float *edge)
{
	struct text_style *ts = &window(no)->label.text.ruby_ts;
	*face = ts->face; *size = ts->size;
	*r = ts->color.r; *g = ts->color.g; *b = ts->color.b;
	*bold = ts->bold_width; *edge = ts->edge_left;
	*edge_r = ts->edge_color.r; *edge_g = ts->edge_color.g; *edge_b = ts->edge_color.b;
}
void PE_SetMessageWindowRubyCharSpace(int no, int space)
{
	window(no)->label.text.ruby_char_space = space;
	parts_message_window_rebuild_text(parts_get(no));
}
int PE_GetMessageWindowRubyCharSpace(int no) { return window(no)->label.text.ruby_char_space; }
void PE_SetMessageWindowRubyLineSpace(int no, int space)
{
	window(no)->ruby_line_space = space;
	parts_message_window_rebuild_text(parts_get(no));
}
int PE_GetMessageWindowRubyLineSpace(int no) { return window(no)->ruby_line_space; }
