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
#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "audio.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "parts.h"
#include "parts_internal.h"

struct parts_list parts_list = TAILQ_HEAD_INITIALIZER(parts_list);
static struct hash_table *parts_table = NULL;

static void parts_init(struct parts *parts)
{
	memset(parts, 0, sizeof(struct parts));
	parts->on_cursor_sound = -1;
	parts->on_click_sound = -1;
	parts->origin_mode = 1;
	parts->z = 1;
	parts->show = true;
	parts->parent = -1;
	parts->linked_to = -1;
	parts->linked_from = -1;
	parts->scale.x = 1.0;
	parts->scale.y = 1.0;
	parts->rotation.x = 0.0;
	parts->rotation.y = 0.0;
	parts->rotation.z = 0.0;
	TAILQ_INIT(&parts->children);
	TAILQ_INIT(&parts->motion);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		TAILQ_INIT(&parts->states[i].construction_process);
	}
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	return parts;
}

static void parts_list_insert(struct parts_list *list, struct parts *parts)
{
	struct parts *p;
	TAILQ_FOREACH(p, list, parts_list_entry) {
		if (p->z >= parts->z) {
			TAILQ_INSERT_BEFORE(p, parts, parts_list_entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(list, parts, parts_list_entry);
	parts_engine_dirty();
}

struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	slot->value = parts;
	parts_list_insert(&parts_list, parts);
	return parts;
}

bool parts_exists(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static struct parts_list *parts_get_list(struct parts *parts)
{
	if (parts->parent < 0)
		return &parts_list;
	return &parts_get(parts->parent)->children;
}

static void parts_list_remove(struct parts_list *list, struct parts *parts)
{
	TAILQ_REMOVE(list, parts, parts_list_entry);
}

static void parts_state_free(struct parts_state *state)
{
	switch (state->type) {
	case PARTS_CG:
		gfx_delete_texture(&state->cg.texture);
		break;
	case PARTS_TEXT:
		free(state->text.lines);
		gfx_delete_texture(&state->text.texture);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->num.texture);
		for (int i = 0; i < 12; i++) {
			gfx_delete_texture(&state->num.cg[i]);
		}
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->gauge.texture);
		gfx_delete_texture(&state->gauge.cg);
		break;
	}
	while (!TAILQ_EMPTY(&state->construction_process)) {
		struct parts_cp_op *op = TAILQ_FIRST(&state->construction_process);
		TAILQ_REMOVE(&state->construction_process, op, entry);
		parts_cp_op_free(op);
	}
	memset(state, 0, sizeof(struct parts_state));
	TAILQ_INIT(&state->construction_process);
}

static struct text_style default_text_style = {
	.face = FONT_GOTHIC,
	.size = 16.0f,
	.bold_width = 0.0f,
	.weight = FW_NORMAL,
	.edge_left = 0.0f,
	.edge_up = 0.0f,
	.edge_right = 0.0f,
	.edge_down = 0.0f,
	.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	.edge_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
	.scale_x = 1.0f,
	.space_scale_x = 1.0f,
	.font_spacing = 0.0f,
	.font_size = NULL
};

static void parts_state_reset(struct parts_state *state, enum parts_type type)
{
	parts_state_free(state);
	state->type = type;
	if (type == PARTS_TEXT) {
		state->text.ts = default_text_style;
	}
}

struct parts_cg *parts_get_cg(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CG) {
		parts_state_reset(&parts->states[state], PARTS_CG);
	}
	return &parts->states[state].cg;
}

struct parts_text *parts_get_text(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_TEXT) {
		parts_state_reset(&parts->states[state], PARTS_TEXT);
	}
	return &parts->states[state].text;
}

struct parts_animation *parts_get_animation(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_ANIMATION) {
		parts_state_reset(&parts->states[state], PARTS_ANIMATION);
	}
	return &parts->states[state].anim;
}

struct parts_numeral *parts_get_numeral(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_NUMERAL) {
		parts_state_reset(&parts->states[state], PARTS_NUMERAL);
	}
	return &parts->states[state].num;
}

struct parts_gauge *parts_get_hgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_HGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_HGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_gauge *parts_get_vgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_VGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_VGAUGE);
	}
	return &parts->states[state].gauge;
}

void parts_recalculate_offset(struct parts *parts)
{
	const int mode = parts->origin_mode;
	const int w = parts->rect.w;
	const int h = parts->rect.h;

	int x, y;
	switch (mode) {
	case 1: x = 0;    y = 0;    break;
	case 2: x = -w/2; y = 0;    break;
	case 3: x = -w;   y = -h/2; break;
	case 4: x = 0;    y = -h/2; break;
	case 5: x = -w/2; y = -h/2; break;
	case 6: x = -w;   y = -h/2; break;
	case 7: x = 0;    y = -h;   break;
	case 8: x = -w/2; y = -h;   break;
	case 9: x = -w;   y = -h;   break;
	default:
		// why...
		x = mode;
		y = (3*h)/4;
		break;
	}

	parts->offset.x = x;
	parts->offset.y = y;
}

void parts_recalculate_pos(struct parts *parts)
{
	parts_recalculate_offset(parts);
	parts->pos = (Rectangle) {
		.x = parts->rect.x + parts->offset.x,
		.y = parts->rect.y + parts->offset.y,
		.w = parts->rect.w,
		.h = parts->rect.h,
	};
}

void parts_set_pos(struct parts *parts, Point pos)
{
	parts->rect.x = pos.x;
	parts->rect.y = pos.y;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

void parts_set_scale_x(struct parts *parts, float mag)
{
	parts->scale.x = mag;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

void parts_set_scale_y(struct parts *parts, float mag)
{
	parts->scale.y = mag;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

void parts_set_rotation_z(struct parts *parts, float rot)
{
	parts->rotation.z = rot;
	parts_dirty(parts);
}

void parts_set_alpha(struct parts *parts, int alpha)
{
	parts->alpha = max(0, min(255, alpha));
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		struct parts_state *s = &parts->states[i];
		switch (s->type) {
		case PARTS_CG:
			s->cg.texture.alpha_mod = parts->alpha;
			break;
		case PARTS_TEXT:
			s->text.texture.alpha_mod = parts->alpha;
			break;
		case PARTS_ANIMATION:
			for (unsigned i = 0; i < s->anim.nr_frames; i++) {
				s->anim.frames[i].alpha_mod = parts->alpha;
			}
			break;
		case PARTS_NUMERAL:
			s->num.texture.alpha_mod = parts->alpha;
			break;
		case PARTS_HGAUGE:
		case PARTS_VGAUGE:
			s->gauge.texture.alpha_mod = parts->alpha;
			break;
		}
	}
	parts_dirty(parts);
}

void parts_set_cg_dims(struct parts *parts, struct cg *cg)
{
	if (parts->rect.w && parts->rect.w != cg->metrics.w)
		WARNING("Width of parts CGs differ: %d / %d", parts->rect.w, cg->metrics.w);
	if (parts->rect.h && parts->rect.h != cg->metrics.h)
		WARNING("Heights of parts CGs differ: %d / %d", parts->rect.h, cg->metrics.h);
	parts->rect.w = cg->metrics.w;
	parts->rect.h = cg->metrics.h;
}

static bool parts_set_cg(struct parts *parts, struct cg *cg, int cg_no, int state)
{
	if (!cg)
		return false;
	struct parts_cg *parts_cg = parts_get_cg(parts, state);
	gfx_delete_texture(&parts_cg->texture);
	gfx_init_texture_with_cg(&parts_cg->texture, cg);
	parts_cg->no = cg_no;
	parts_set_cg_dims(parts, cg);
	parts_recalculate_pos(parts);
	parts_dirty(parts);
	cg_free(cg);
	return true;
}

bool parts_set_cg_by_index(struct parts *parts, int cg_no, int state)
{
	if (!cg_no) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}
	return parts_set_cg(parts, asset_cg_load(cg_no), cg_no, state);
}

bool parts_set_cg_by_name(struct parts *parts, struct string *cg_name, int state)
{
	if (!cg_name) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}
	int no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &no);
	return parts_set_cg(parts, cg, no, state);
}

void parts_set_hgauge_rate(struct parts *parts, float rate, int state)
{
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (!g->texture.handle) {
		WARNING("HGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.w;
	gfx_copy_with_alpha_map(&g->texture, 0, 0, &g->cg, 0, 0, pixels, g->cg.h);
	gfx_fill_amap(&g->texture, pixels, 0, g->cg.w - pixels, g->cg.h, 0);
	parts_dirty(parts);
}

void parts_set_vgauge_rate(struct parts *parts, float rate, int state)
{
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (!g->texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.h;
	gfx_copy_with_alpha_map(&g->texture, 0, pixels, &g->cg, 0, pixels, g->cg.w, g->cg.h - pixels);
	gfx_fill_amap(&g->texture, 0, 0, g->cg.w, pixels, 0);
	parts_dirty(parts);
}

bool parts_set_number(struct parts *parts, int n, int state)
{
	struct parts_numeral *num = parts_get_numeral(parts, state);
	bool negative = n < 0;
	if (negative)
		n *= -1;

	// extract digits
	int digits;
	uint8_t d[32];
	for (digits = 0; n; digits++) {
		d[digits] = n % 10;
		n /= 10;
	}

	// encode number as texture indices
	int nr_chars = 0;
	uint8_t chars[32];
	if (negative) {
		chars[nr_chars++] = 10;
	}
	for (int i = 0; i < digits; i++) {
		if (num->show_comma && i > 0 && i % 3 == 0) {
			chars[nr_chars++] = 11;
		}
		chars[nr_chars++] = d[i];
	}

	// load any uninitialized textures
	for (int i = 0; i < nr_chars; i++) {
		if (num->cg[chars[i]].handle)
			continue;
		struct cg *cg = asset_cg_load(num->cg_no + chars[i]);
		if (!cg) {
			WARNING("Failed to load numeral cg: %d", num->cg_no + chars[i]);
			continue;
		}
		gfx_init_texture_with_cg(&num->cg[chars[i]], cg);
		cg_free(cg);
	}

	// determine output dimensions
	int w = 0, h = 0;
	for (int i = nr_chars-1; i >= 0; i--) {
		if (!num->cg[chars[i]].handle)
			continue;
		w += num->cg[chars[i]].w;
		h = max(h, num->cg[chars[i]].h);
	}
	w += (nr_chars-1) * num->space;

	// copy chars to texture
	gfx_delete_texture(&num->texture);
	gfx_init_texture_rgba(&num->texture, w, h, (SDL_Color){0, 0, 0, 255});

	int x = 0;
	for (int i = nr_chars-1; i>= 0; i--) {
		Texture *ch = &num->cg[chars[i]];
		if (!ch->handle)
			continue;
		gfx_copy_with_alpha_map(&num->texture, x, 0, ch, 0, 0, ch->w, ch->h);
		x += ch->w + num->space;
	}

	parts->rect.w = w;
	parts->rect.h = h;

	parts_dirty(parts);
	return true;
}

void parts_set_state(struct parts *parts, enum parts_state_type state)
{
	if (parts->state != state) {
		parts->state = state;
		parts_dirty(parts);
	}
}

static void parts_update_loop(struct parts *parts, int passed_time)
{
	if (parts->states[parts->state].type != PARTS_ANIMATION)
		return;

	struct parts_animation *anim = &parts->states[parts->state].anim;
	if (passed_time <= 0 || !anim->nr_frames)
		return;

	const unsigned elapsed = anim->elapsed + passed_time;
	const unsigned frame_diff = elapsed / anim->frame_time;
	const unsigned remainder = elapsed % anim->frame_time;

	if (frame_diff > 0) {
		anim->elapsed = remainder;
		anim->current_frame = (anim->current_frame + frame_diff) % anim->nr_frames;
		anim->texture = anim->frames[anim->current_frame];
		parts_dirty(parts);
	} else {
		anim->elapsed = elapsed;
	}
}

static void parts_update_animation(int passed_time)
{
	struct parts *parts;
	TAILQ_FOREACH(parts, &parts_list, parts_list_entry) {
		parts_update_loop(parts, passed_time);
	}
}

void parts_release(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (!slot->value)
		return;

	struct parts *parts = slot->value;
	parts_clear_motion(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_state_free(&parts->states[i]);
	}

	while (!TAILQ_EMPTY(&parts->children)) {
		struct parts *child = TAILQ_FIRST(&parts->children);
		child->parent = -1;
		parts_list_remove(&parts->children, child);
		parts_list_insert(&parts_list, child);
	}

	parts_list_remove(parts_get_list(parts), parts);
	free(parts);
	slot->value = NULL;
	parts_engine_dirty();
}

void parts_release_all(void)
{
	while (!TAILQ_EMPTY(&parts_list)) {
		struct parts *parts = TAILQ_FIRST(&parts_list);
		parts_release(parts->no);
	}
}

bool PE_Init(void)
{
	parts_table = ht_create(1024);
	parts_render_init();
	return true;
}

void PE_Update(int passed_time, possibly_unused bool message_window_show)
{
	audio_update();
	parts_update_animation(passed_time);
	PE_UpdateInputState(passed_time);
	parts_engine_dirty();
}

void PE_UpdateParts(int passed_time, possibly_unused bool is_skip, possibly_unused bool message_window_show)
{
	audio_update();
	parts_update_animation(passed_time);
	parts_engine_dirty();
}

bool PE_SetPartsCG(int parts_no, struct string *cg_name, possibly_unused int sprite_deform, int state)
{
	state--;
	if (state < 0 || state > 2) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}
	return parts_set_cg_by_name(parts_get(parts_no), cg_name, state);
}

bool PE_SetPartsCG_by_index(int parts_no, int cg_no, possibly_unused int sprite_deform, int state)
{
	state--;
	if (state < 0 || state > 2) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}
	return parts_set_cg_by_index(parts_get(parts_no), cg_no, state);
}

int PE_GetPartsCGNumber(int parts_no, int state)
{
	state--;
	if (state < 0 || state > 2) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}

	return parts_get_cg(parts_get(parts_no), state)->no;
}

bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state)
{
	state--;
	if (state < 0 || state > 2) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
	}

	struct parts *parts = parts_get(parts_no);
	Texture *frames = xcalloc(nr_frames, sizeof(Texture));
	for (int i = 0; i < nr_frames; i++) {
		struct cg *cg = asset_cg_load(cg_no + i);
		if (!cg) {
			for (int j = 0; j < i; j++) {
				gfx_delete_texture(&frames[j]);
			}
			free(frames);
			return false;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		parts_set_cg_dims(parts, cg);
		cg_free(cg);
	}

	struct parts_animation *anim = parts_get_animation(parts, state);
	anim->frames = frames; // free previous value?
	anim->nr_frames = nr_frames;
	anim->frame_time = frame_time;
	anim->elapsed = 0;
	anim->current_frame = 0;
	anim->texture = frames[0];
	return true;
}

static bool set_gauge_cg(int parts_no, struct cg *cg, int state, bool vert)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = vert ? parts_get_vgauge(parts, state) : parts_get_hgauge(parts, state);
	gfx_init_texture_with_cg(&g->cg, cg);
	gfx_init_texture_with_cg(&g->texture, cg);

	gfx_init_texture_rgba(&g->texture, g->cg.w, g->cg.h, (SDL_Color){0,0,0,255});
	gfx_copy_with_alpha_map(&g->texture, 0, 0, &g->cg, 0, 0, g->cg.w, g->cg.h);

	parts->rect.w = g->cg.w;
	parts->rect.h = g->cg.h;

	parts_dirty(parts);
	return true;
}

bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state)
{
	struct cg *cg = asset_cg_load_by_name(cg_name->text, NULL);
	if (!cg)
		return false;
	bool r = set_gauge_cg(parts_no, cg, state, false);
	cg_free(cg);
	return r;
}

bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	bool r = set_gauge_cg(parts_no, cg, state, false);
	cg_free(cg);
	return r;
}

bool PE_SetHGaugeRate(int parts_no, int numerator, int denominator, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_set_hgauge_rate(parts_get(parts_no), (float)numerator/(float)denominator, state);
	return true;
}

bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state)
{
	struct cg *cg = asset_cg_load_by_name(cg_name->text, NULL);
	if (!cg)
		return false;
	bool r = set_gauge_cg(parts_no, cg, state, true);
	cg_free(cg);
	return r;
}

bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	bool r = set_gauge_cg(parts_no, cg, state, true);
	cg_free(cg);
	return r;
}

bool PE_SetVGaugeRate(int parts_no, int numerator, int denominator, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_set_vgauge_rate(parts_get(parts_no), (float)numerator/(float)denominator, state);
	return true;
}

bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	for (int i = 0; i < 12; i++) {
		gfx_delete_texture(&n->cg[i]);
	}
	// NOTE: textures are loaded lazily since not every numeral font loaded
	//       in this manner is complete (some lack dash or comma glyphs)
	n->cg_no = cg_no;
	return true;
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no, int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8, int w9, int w_minus, int w_comma, int state)
{
	// TODO? Create a generic numeral-font object that can be shared between parts.
	//       In the current implementation, each number stores a complete copy of
	//       its numeral font.

	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	Texture t = {0};
	gfx_init_texture_with_cg(&t, cg);
	cg_free(cg);

	int x = 0;
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	for (int i = 0; i < 12; i++) {
		gfx_delete_texture(&n->cg[i]);
		if (w[i] <= 0)
			continue;

		gfx_init_texture_blank(&n->cg[i], w[i], t.h);
		gfx_copy_with_alpha_map(&n->cg[i], 0, 0, &t, x, 0, w[i], t.h);
		x += w[i];
	}

	gfx_delete_texture(&t);
	return true;
}

bool PE_SetNumeralNumber(int parts_no, int n, int state)
{
	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	return parts_set_number(parts_get(parts_no), n, state);
}

bool PE_SetNumeralShowComma(int parts_no, bool show_comma, int state)
{
	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	parts_get_numeral(parts_get(parts_no), state)->show_comma = show_comma;
	return true;
}

bool PE_SetNumeralSpace(int parts_no, int space, int state)
{
	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	parts_get_numeral(parts_get(parts_no), state)->space = space;
	return true;
}

bool PE_SetPartsRectangleDetectionSize(int PartsNumber, int Width, int Height, int State);
bool PE_SetPartsFlash(int PartsNumber, struct string *pIFlashFileName, int State);
bool PE_IsPartsFlashEnd(int PartsNumber, int State);
int PE_GetPartsFlashCurrentFrameNumber(int PartsNumber, int State);
bool PE_BackPartsFlashBeginFrame(int PartsNumber, int State);
bool PE_StepPartsFlashFinalFrame(int PartsNumber, int State);

void PE_ReleaseParts(int parts_no)
{
	parts_release(parts_no);
}

void PE_ReleaseAllParts(void)
{
	parts_release_all();
}

void PE_ReleaseAllPartsWithoutSystem(void)
{
	// FIXME: what's the difference?
	parts_release_all();
}

void PE_SetPos(int parts_no, int x, int y)
{
	parts_set_pos(parts_get(parts_no), (Point){ x, y });
}

/*
 * NOTE: If a ChipmunkSpriteEngine sprite is at Z=0, it's drawn behind;
 *       otherwise it's drawn in front, regardless of the parts Z-value.
 *       This is implemented by giving GoatGUIEngine sprites a Z-value of
 *       0 and a secondary Z-value of 1 + the value given here.
 */
void PE_SetZ(int parts_no, int z)
{
	struct parts *parts = parts_get(parts_no);
	parts->z = z;

	struct parts_list *list = parts_get_list(parts);
	parts_list_remove(list, parts);
	parts_list_insert(list, parts);
}

void PE_SetShow(int parts_no, bool show)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->show != show) {
		parts->show = show;
		parts_dirty(parts);
	}
}

void PE_SetAlpha(int parts_no, int alpha)
{
	parts_set_alpha(parts_get(parts_no), alpha);
}

void PE_SetPartsDrawFilter(int PartsNumber, int DrawFilter);
void PE_SetAddColor(int PartsNumber, int nR, int nG, int nB);
void PE_SetMultiplyColor(int PartsNumber, int nR, int nG, int nB);

int PE_GetPartsX(int parts_no)
{
	return parts_get(parts_no)->rect.x;
}

int PE_GetPartsY(int parts_no)
{
	return parts_get(parts_no)->rect.y;
}

int PE_GetPartsWidth(int parts_no, possibly_unused int state)
{
	// FIXME: use state
	return parts_get(parts_no)->rect.w;
}

int PE_GetPartsHeight(int parts_no, possibly_unused int state)
{
	// FIXME: use state
	return parts_get(parts_no)->rect.h;
}

int PE_GetPartsZ(int parts_no)
{
	return parts_get(parts_no)->z;
}

bool PE_GetPartsShow(int parts_no)
{
	return parts_get(parts_no)->show;
}

int PE_GetPartsAlpha(int parts_no)
{
	return parts_get(parts_no)->alpha;
}

void PE_GetAddColor(int PartsNumber, int *nR, int *nG, int *nB);
void PE_GetMultiplyColor(int PartsNumber, int *nR, int *nG, int *nB);

void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode)
{
	struct parts *parts = parts_get(parts_no);
	parts->origin_mode = origin_pos_mode;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

int PE_GetPartsOriginPosMode(int parts_no)
{
	return parts_get(parts_no)->origin_mode;
}

void PE_SetParentPartsNumber(int parts_no, int parent_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->parent == parent_parts_no)
		return;

	parts_list_remove(parts_get_list(parts), parts);
	parts_list_insert(&parts_get(parent_parts_no)->children, parts);
	parts->parent = parent_parts_no;
}

bool PE_SetPartsGroupNumber(possibly_unused int PartsNumber, possibly_unused int GroupNumber)
{
	// TODO
	return true;
}

void PE_SetPartsMessageWindowShowLink(possibly_unused int parts_no, possibly_unused bool message_window_show_link)
{
	// TODO
}

bool PE_GetPartsMessageWindowShowLink(int PartsNumber);

void PE_SetPartsMagX(int parts_no, float scale_x)
{
	parts_set_scale_x(parts_get(parts_no), scale_x);
}

void PE_SetPartsMagY(int parts_no, float scale_y)
{
	parts_set_scale_y(parts_get(parts_no), scale_y);
}

void PE_SetPartsRotateX(int parts_no, float rot_x);
void PE_SetPartsRotateY(int parts_no, float rot_y);

void PE_SetPartsRotateZ(int parts_no, float rot_z)
{
	parts_set_rotation_z(parts_get(parts_no), rot_z);
}

void PE_SetPartsAlphaClipperPartsNumber(int PartsNumber, int AlphaClipperPartsNumber);
void PE_SetPartsPixelDecide(int PartsNumber, bool PixelDecide);
bool PE_SetThumbnailReductionSize(int ReductionSize);
bool PE_SetThumbnailMode(bool Mode);

bool PE_Save(struct page **buffer)
{
	// TODO
	if (*buffer) {
		delete_page_vars(*buffer);
		free_page(*buffer);
	}
	*buffer = NULL;
	return true;
}

bool PE_Load(possibly_unused struct page **buffer)
{
	// TODO
	return true;
}

int PE_GetFreeNumber(void)
{
	// NOTE: per GUIEngine.dll (Rance 01)
	static int first_free = 1000001000;
	while (PE_IsExist(first_free)) {
		first_free++;
	}
	return first_free;
}

bool PE_IsExist(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

bool PE_SetPartsFlashAndStop(int PartsNumber, struct string *pIFlashFileName, int State);
bool PE_StopPartsFlash(int PartsNumber, int State);
bool PE_StartPartsFlash(int PartsNumber, int State);
bool PE_GoFramePartsFlash(int PartsNumber, int FrameNumber, int State);
int PE_GetPartsFlashEndFrame(int PartsNumber, int State);
