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
	parts->delegate_index = -1;
	parts->on_cursor_sound = -1;
	parts->on_click_sound = -1;
	parts->origin_mode = 1;
	parts->z = 1;
	parts->show = true;
	parts->alpha = 255;
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
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	return parts;
}

static void parts_list_insert(struct parts *parts)
{
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (p->z >= parts->z) {
			TAILQ_INSERT_BEFORE(p, parts, parts_list_entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&parts_list, parts, parts_list_entry);
	parts_engine_dirty();
}

static void parts_set_parent(struct parts *child, struct parts *parent)
{
	if (child->parent >= 0) {
		struct parts *old_parent = parts_get(child->parent);
		TAILQ_REMOVE(&old_parent->children, child, child_list_entry);
	}
	if (parent) {
		TAILQ_INSERT_TAIL(&parent->children, child, child_list_entry);
		child->parent = parent->no;
	} else {
		child->parent = -1;
	}
}

struct parts *parts_try_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;
	return NULL;
}

struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	slot->value = parts;
	parts_list_insert(parts);
	return parts;
}

bool parts_exists(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static void parts_list_remove(struct parts *parts)
{
	TAILQ_REMOVE(&parts_list, parts, parts_list_entry);
}

static void parts_state_free(struct parts_state *state)
{
	switch (state->type) {
	case PARTS_UNINITIALIZED:
		break;
	case PARTS_CG:
		gfx_delete_texture(&state->common.texture);
		if (state->cg.name)
			free_string(state->cg.name);
		break;
	case PARTS_TEXT:
		gfx_delete_texture(&state->common.texture);
		free(state->text.lines);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		free(state->anim.frames);
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->common.texture);
		for (int i = 0; i < 12; i++) {
			gfx_delete_texture(&state->num.cg[i]);
		}
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->common.texture);
		gfx_delete_texture(&state->gauge.cg);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		gfx_delete_texture(&state->common.texture);
		while (!TAILQ_EMPTY(&state->cproc.ops)) {
			struct parts_cp_op *op = TAILQ_FIRST(&state->cproc.ops);
			TAILQ_REMOVE(&state->cproc.ops, op, entry);
			parts_cp_op_free(op);
		}
		break;
	}
	memset(state, 0, sizeof(struct parts_state));
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
	} else if (type == PARTS_CONSTRUCTION_PROCESS) {
		TAILQ_INIT(&state->cproc.ops);
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

struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CONSTRUCTION_PROCESS) {
		parts_state_reset(&parts->states[state], PARTS_CONSTRUCTION_PROCESS);
	}
	return &parts->states[state].cproc;
}

static Point calculate_offset(int mode, int w, int h)
{
	switch (mode) {
	case 1:  return (Point) {    0, 0    };
	case 2:  return (Point) { -w/2, 0    };
	case 3:  return (Point) {   -w, -h/2 };
	case 4:  return (Point) {    0, -h/2 };
	case 5:  return (Point) { -w/2, -h/2 };
	case 6:  return (Point) {   -w, -h/2 };
	case 7:  return (Point) {    0, -h   };
	case 8:  return (Point) { -w/2, -h   };
	case 9:  return (Point) {   -w, -h   };
	default: return (Point) { mode, (3*h)/4 }; // why...
	}
}

/*
 * Should be called when:
 *   - position (parts->pos) changes
 *   - width or height changes
 *   - origin mode changes
 */
static void parts_common_recalculate_hitbox(struct parts *parts, struct parts_common *common)
{
	common->origin_offset = calculate_offset(parts->origin_mode, common->w, common->h);

	if (common->surface_area.w || common->surface_area.h) {
		Rectangle r = { 0, 0, common->w, common->h };
		SDL_IntersectRect(&r, &common->surface_area, &common->hitbox);
		common->origin_offset.x -= common->surface_area.x;
		common->origin_offset.y -= common->surface_area.y;
		common->hitbox.x += parts->pos.x + common->origin_offset.x;
		common->hitbox.y += parts->pos.y + common->origin_offset.y;
	} else {
		common->hitbox = (Rectangle) {
			.x = parts->pos.x + common->origin_offset.x,
			.y = parts->pos.y + common->origin_offset.y,
			.w = common->w,
			.h = common->h,
		};
	}
}

static void parts_recalculate_hitbox(struct parts *parts)
{
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_common_recalculate_hitbox(parts, &parts->states[i].common);
	}
}

void parts_set_pos(struct parts *parts, Point pos)
{
	parts->pos.x = pos.x;
	parts->pos.y = pos.y;
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h)
{
	common->w = w;
	common->h = h;
	parts_common_recalculate_hitbox(parts, common);
}

void parts_set_origin_mode(struct parts *parts, int origin_mode)
{
	parts->origin_mode = origin_mode;
	parts_recalculate_hitbox(parts);
}

void parts_set_scale_x(struct parts *parts, float mag)
{
	parts->scale.x = mag;
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

void parts_set_scale_y(struct parts *parts, float mag)
{
	parts->scale.y = mag;
	parts_recalculate_hitbox(parts);
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
		s->common.texture.alpha_mod = parts->alpha;
		if (s->type == PARTS_ANIMATION) {
			for (unsigned i = 0; i < s->anim.nr_frames; i++) {
				s->anim.frames[i].alpha_mod = parts->alpha;
			}
		}
	}
	parts_dirty(parts);
}

static bool parts_set_cg(struct parts *parts, struct cg *cg, int cg_no, struct string *name, int state)
{
	if (!cg)
		return false;
	struct parts_cg *parts_cg = parts_get_cg(parts, state);
	gfx_delete_texture(&parts_cg->common.texture);
	gfx_init_texture_with_cg(&parts_cg->common.texture, cg);
	parts_set_dims(parts, &parts_cg->common, cg->metrics.w, cg->metrics.h);
	parts_cg->no = cg_no;
	if (parts_cg->name)
		free_string(parts_cg->name);
	parts_cg->name = name;
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
	return parts_set_cg(parts, asset_cg_load(cg_no), cg_no, NULL, state);
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
	return parts_set_cg(parts, cg, no, string_dup(cg_name), state);
}

void parts_set_hgauge_rate(struct parts *parts, float rate, int state)
{
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (!g->common.texture.handle) {
		WARNING("HGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.w;
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, pixels, g->cg.h);
	gfx_fill_amap(&g->common.texture, pixels, 0, g->cg.w - pixels, g->cg.h, 0);
	parts_dirty(parts);
}

void parts_set_vgauge_rate(struct parts *parts, float rate, int state)
{
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (!g->common.texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.h;
	gfx_copy_with_alpha_map(&g->common.texture, 0, pixels, &g->cg, 0, pixels, g->cg.w, g->cg.h - pixels);
	gfx_fill_amap(&g->common.texture, 0, 0, g->cg.w, pixels, 0);
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
	gfx_delete_texture(&num->common.texture);
	gfx_init_texture_rgba(&num->common.texture, w, h, (SDL_Color){0, 0, 0, 255});

	int x = 0;
	for (int i = nr_chars-1; i>= 0; i--) {
		Texture *ch = &num->cg[chars[i]];
		if (!ch->handle)
			continue;
		gfx_copy_with_alpha_map(&num->common.texture, x, 0, ch, 0, 0, ch->w, ch->h);
		x += ch->w + num->space;
	}

	parts_set_dims(parts, &num->common, w, h);

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

void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h)
{
	if (x < 0 || y < 0)
		return;
	common->surface_area = (Rectangle) { x, y, w, h };
	parts_common_recalculate_hitbox(parts, common);
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
		anim->common.texture = anim->frames[anim->current_frame];
		parts_dirty(parts);
	} else {
		anim->elapsed = elapsed;
	}
}

static void parts_update_animation(int passed_time)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
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

	// break parent/child relationships
	while (!TAILQ_EMPTY(&parts->children)) {
		struct parts *child = TAILQ_FIRST(&parts->children);
		parts_set_parent(child, NULL);
	}
	parts_set_parent(parts, NULL);

	parts_list_remove(parts);
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
	parts_debug_init();
	gfx_init();
	gfx_font_init();
	audio_init();
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

void PE_SetDelegateIndex(int parts_no, int delegate_index)
{
	parts_get(parts_no)->delegate_index = delegate_index;
}

int PE_GetDelegateIndex(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->delegate_index : -1;
}

bool PE_SetPartsCG(int parts_no, struct string *cg_name, possibly_unused int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;
	return parts_set_cg_by_name(parts_get(parts_no), cg_name, state);
}

bool PE_SetPartsCG_by_index(int parts_no, int cg_no, possibly_unused int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;
	return parts_set_cg_by_index(parts_get(parts_no), cg_no, state);
}

void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts_cg *cg = parts_get_cg(parts_get(parts_no), state);
	if (cg->name) {
		if (*cg_name)
			free_string(*cg_name);
		*cg_name = string_dup(cg->name);
	}
}

bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_cg *cg = parts_get_cg(parts, state);
	parts_set_surface_area(parts, &cg->common, x, y, w, h);
	return true;
}

int PE_GetPartsCGNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		return false;
	}

	return parts_get_cg(parts_get(parts_no), state)->no;
}

static bool set_loop_cg(int parts_no, int start_no, int nr_frames, int frame_time, int state,
		struct cg *(*load_cg)(int no, void *data), void *data)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	int w = 0, h = 0;
	struct parts *parts = parts_get(parts_no);
	Texture *frames = xcalloc(nr_frames, sizeof(Texture));
	for (int i = 0; i < nr_frames; i++) {
		struct cg *cg = load_cg(start_no + i, data);
		if (!cg) {
			for (int j = 0; j < i; j++) {
				gfx_delete_texture(&frames[j]);
			}
			free(frames);
			return false;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		w = max(w, cg->metrics.w);
		h = max(h, cg->metrics.h);
		cg_free(cg);
	}

	struct parts_animation *anim = parts_get_animation(parts, state);
	parts_set_dims(parts, &anim->common, w, h);
	free(anim->frames);
	anim->frames = frames;
	anim->nr_frames = nr_frames;
	anim->frame_time = frame_time;
	anim->elapsed = 0;
	anim->current_frame = 0;
	anim->common.texture = frames[0];
	return true;

}

static struct cg *load_loop_cg_by_index(int no, void *_)
{
	return asset_cg_load(no);
}

bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state)
{
	return set_loop_cg(parts_no, cg_no, nr_frames, frame_time, state,
			load_loop_cg_by_index, NULL);
}

static struct cg *load_loop_cg_by_name(int no, void *data)
{
	int unused_no;
	struct string *cg_name = string_format((struct string*)data, (union vm_value){.i=no});
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &unused_no);
	free_string(cg_name);
	return cg;
}

bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames, int frame_time, int state)
{
	return set_loop_cg(parts_no, start_no, nr_frames, frame_time, state,
			load_loop_cg_by_name, cg_name);
}

bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	parts_set_surface_area(parts, &anim->common, x, y, w, h);
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
	gfx_init_texture_with_cg(&g->common.texture, cg);

	gfx_init_texture_rgba(&g->common.texture, g->cg.w, g->cg.h, (SDL_Color){0,0,0,255});
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, g->cg.w, g->cg.h);

	parts_set_dims(parts, &g->common, g->cg.w, g->cg.h);

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

bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
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

bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
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

bool PE_SetNumeralSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	parts_set_surface_area(parts, &n->common, x, y, w, h);
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

	parts_list_remove(parts);
	parts_list_insert(parts);
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

void PE_SetAddColor(int parts_no, int r, int g, int b)
{
	//NOTICE("PE_SetAddColor(%d, %d, %d, %d)", parts_no, r, g, b);
}

void PE_SetMultiplyColor(int parts_no, int r, int g, int b)
{
	//NOTICE("PE_SetMultiplyColor(%d, %d, %d, %d)", parts_no, r, g, b);
}

int PE_GetPartsX(int parts_no)
{
	return parts_get(parts_no)->pos.x;
}

int PE_GetPartsY(int parts_no)
{
	return parts_get(parts_no)->pos.y;
}

int PE_GetPartsWidth(int parts_no, possibly_unused int state)
{
	return parts_get_width(parts_get(parts_no));
}

int PE_GetPartsHeight(int parts_no, possibly_unused int state)
{
	return parts_get_height(parts_get(parts_no));
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
	parts_set_origin_mode(parts, origin_pos_mode);
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

	parts_set_parent(parts, parts_get(parent_parts_no));
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

void PE_SetPartsPixelDecide(int parts_no, bool pixel_decide)
{
	//NOTICE("PE_SetPartsPixelDecide(%d, %s)", parts_no, pixel_decide ? "true" : "false");
}

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

bool PE_SaveWithoutHideParts(struct page **buffer)
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
