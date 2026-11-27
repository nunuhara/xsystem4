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

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4/mt19937int.h"

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/flat.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "parts.h"
#include "parts_internal.h"
#include "system4/ain.h"
#include "vm.h"
#include "xsystem4.h"

enum emitter_direction_type {
	EMITTER_DIRECTION_RANDOM         = 0,
	EMITTER_DIRECTION_FIXED          = 1,
	EMITTER_DIRECTION_PARENT         = 2,
	EMITTER_DIRECTION_PARENT_REVERSE = 3,
	EMITTER_DIRECTION_DISK           = 4,
	EMITTER_DIRECTION_RADIAL         = 5,
};

enum emitter_end_pos_type {
	EMITTER_END_POS_NONE  = 0,
	EMITTER_END_POS_FIXED = 1,
};

enum emitter_create_pos_type {
	EMITTER_CREATE_POS_NONE   = 0,
	EMITTER_CREATE_POS_SPHERE = 1,
	EMITTER_CREATE_POS_CIRCLE = 2,
	EMITTER_CREATE_POS_RECT   = 3,
};

enum emitter_parent_key_mode {
	EMITTER_PARENT_KEY_SELECTIVE         = 0,
	EMITTER_PARENT_KEY_ANCESTOR_CURRENT  = 1,
	EMITTER_PARENT_KEY_ANCESTOR_AT_BIRTH = 2,
};

static inline float mt_next(struct mt19937 *mt)
{
	return mt19937_genrand(mt) / 4294967296.0f;
}

static inline float mt_next_signed(struct mt19937 *mt)
{
	return mt_next(mt) - 0.5f;
}

// Approximately uniform random unit 3D vector.
static void random_unit_vec3(struct mt19937 *mt, vec3 v)
{
	v[0] = mt_next_signed(mt);
	v[1] = mt_next_signed(mt);
	v[2] = mt_next_signed(mt);
	glm_vec3_normalize(v);
}

struct flat_layer_state *parts_flat_layer_state_new(size_t nr_timelines)
{
	struct flat_layer_state *s = xcalloc(1, sizeof(struct flat_layer_state));
	s->suppress_advance = true;
	s->jump_target = -1;
	s->last_rendered_frame = -1;
	s->nr_timelines = nr_timelines;
	s->last_script_keys = xmalloc(nr_timelines * sizeof(int));
	for (size_t i = 0; i < nr_timelines; i++)
		s->last_script_keys[i] = -2;  // uninitialized
	s->disabled_timelines = xcalloc(nr_timelines, sizeof(bool));
	s->children = xcalloc(nr_timelines, sizeof(struct flat_layer_state *));
	return s;
}

void parts_flat_clear_emitter_births(struct flat_layer_state *s)
{
	while (s->emitter_births) {
		struct flat_emitter_birth *birth = s->emitter_births;
		s->emitter_births = birth->next;
		free(birth);
	}
}

struct flat_emitter_birth *parts_flat_get_emitter_birth(
		struct flat_layer_state *state,
		const struct flat_timeline *timeline, int timeline_idx,
		int frame, unsigned instance, bool create)
{
	for (struct flat_emitter_birth *b = state->emitter_births; b; b = b->next) {
		if ((b->timeline == timeline || (timeline_idx >= 0 && b->timeline_idx == timeline_idx))
				&& b->frame == frame && b->instance == instance) {
			if (timeline)
				b->timeline = timeline;
			if (timeline_idx >= 0)
				b->timeline_idx = timeline_idx;
			return b;
		}
	}
	if (!create)
		return NULL;
	struct flat_emitter_birth *b = xcalloc(1, sizeof(*b));
	b->timeline = timeline;
	b->timeline_idx = timeline_idx;
	b->frame = frame;
	b->instance = instance;
	b->next = state->emitter_births;
	state->emitter_births = b;
	return b;
}

void parts_flat_layer_state_free(struct flat_layer_state *s)
{
	if (!s)
		return;
	parts_flat_clear_emitter_births(s);
	for (size_t i = 0; i < s->nr_timelines; i++)
		parts_flat_layer_state_free(s->children[i]);
	free(s->children);
	free(s->disabled_timelines);
	free(s->last_script_keys);
	free(s);
}

#define PARTS_FLAT_IDLE_CACHE_MAX 64

static struct parts_flat_asset *flat_asset_head = NULL;
static struct parts_flat_asset *flat_asset_tail = NULL;
static int flat_asset_idle_count = 0;

static void flat_asset_unlink(struct parts_flat_asset *a)
{
	if (a->prev)
		a->prev->next = a->next;
	else
		flat_asset_head = a->next;
	if (a->next)
		a->next->prev = a->prev;
	else
		flat_asset_tail = a->prev;
	a->prev = a->next = NULL;
}

static void flat_asset_destroy(struct parts_flat_asset *a);

static void parts_flat_on_gl_reset(void *user)
{
	(void)user;
	struct parts_flat_asset *a = flat_asset_head;
	while (a) {
		struct parts_flat_asset *next = a->next;
		bool any_remap_failed = false;
		for (size_t i = 0; i < a->nr_talt_atlases; i++) {
			if (a->talt_atlases[i].handle &&
					(!gfx_remap_texture(&a->talt_atlases[i]) || !a->talt_atlases[i].handle)) {
				any_remap_failed = true;
			}
		}
		if (a->base_textures) {
			for (size_t i = 0; i < a->nr_libraries; i++) {
				if (a->base_atlas_idx && a->base_atlas_idx[i] >= 0) {
					int aidx = a->base_atlas_idx[i];
					if ((size_t)aidx < a->nr_talt_atlases) {
						a->base_textures[i].handle = a->talt_atlases[aidx].handle;
						a->base_textures[i].ctx_gen = a->talt_atlases[aidx].ctx_gen;
					}
				} else if (a->base_textures[i].handle &&
						(!gfx_remap_texture(&a->base_textures[i]) || !a->base_textures[i].handle)) {
					any_remap_failed = true;
				}
			}
		}
		if (a->ref == 0 && any_remap_failed) {
			flat_asset_unlink(a);
			flat_asset_idle_count--;
			flat_asset_destroy(a);
		}
		a = next;
	}
}

static void flat_asset_push_front(struct parts_flat_asset *a)
{
	gfx_register_reset_callback(parts_flat_on_gl_reset, NULL);
	a->prev = NULL;
	a->next = flat_asset_head;
	if (flat_asset_head)
		flat_asset_head->prev = a;
	else
		flat_asset_tail = a;
	flat_asset_head = a;
}

static void flat_asset_destroy(struct parts_flat_asset *a)
{
	if (a->base_textures) {
		for (size_t i = 0; i < a->nr_libraries; i++) {
			if (!a->base_atlas_idx || a->base_atlas_idx[i] < 0)
				gfx_delete_texture(&a->base_textures[i]);
		}
		free(a->base_textures);
	}
	free(a->base_rects);
	free(a->base_atlas_idx);
	if (a->talt_atlases) {
		for (size_t i = 0; i < a->nr_talt_atlases; i++)
			gfx_delete_texture(&a->talt_atlases[i]);
		free(a->talt_atlases);
	}
	if (a->stop_motion_frames) {
		for (size_t i = 0; i < a->nr_libraries; i++)
			free(a->stop_motion_frames[i].lib_indices);
		free(a->stop_motion_frames);
	}
	free(a->root_timeline_lib_idx);
	if (a->lib_timeline_lib_idx) {
		for (size_t i = 0; i < a->nr_libraries; i++)
			free(a->lib_timeline_lib_idx[i]);
		free(a->lib_timeline_lib_idx);
	}
	free(a->emitter_cg_lib_idx);
	if (a->flat)
		flat_free(a->flat);
	free(a->name);
	free(a);
}

static void flat_asset_evict_idle(int max_idle)
{
	struct parts_flat_asset *a = flat_asset_tail;
	while (a && flat_asset_idle_count > max_idle) {
		struct parts_flat_asset *prev = a->prev;
		if (a->ref == 0) {
			flat_asset_unlink(a);
			flat_asset_idle_count--;
			flat_asset_destroy(a);
		}
		a = prev;
	}
}

void parts_flat_cache_clear(void)
{
	flat_asset_evict_idle(0);
}

static void flat_asset_unref(struct parts_flat_asset *a)
{
	if (!a)
		return;
	assert(a->ref > 0);
	a->ref--;
	if (a->ref == 0) {
		flat_asset_idle_count++;
		flat_asset_evict_idle(PARTS_FLAT_IDLE_CACHE_MAX);
	}
}

void parts_flat_free(struct parts_flat *f)
{
	if (f->name)
		free_string(f->name);
	parts_flat_layer_state_free(f->root_state);
	if (f->textures) {
		for (size_t i = 0; i < f->nr_libraries; i++) {
			if (!f->asset || (f->owns_texture && f->owns_texture[i]))
				gfx_delete_texture(&f->textures[i]);
		}
		free(f->textures);
	}
	free(f->owns_texture);
	if (f->proxy_cg_names) {
		for (size_t i = 0; i < f->nr_libraries; i++) {
			if (f->proxy_cg_names[i])
				free_string(f->proxy_cg_names[i]);
		}
		free(f->proxy_cg_names);
	}
	if (f->asset) {
		flat_asset_unref(f->asset);
	} else {
		if (f->flat)
			flat_free(f->flat);
		if (f->stop_motion_frames) {
			for (size_t i = 0; i < f->nr_libraries; i++)
				free(f->stop_motion_frames[i].lib_indices);
			free(f->stop_motion_frames);
		}
	}
}

int parts_flat_find_library(struct flat *fl, const char *name)
{
	size_t len = strlen(name);
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		struct string *lib_name = fl->libraries[i].name;
		if ((size_t)lib_name->size == len && memcmp(lib_name->text, name, len) == 0)
			return (int)i;
	}
	return -1;
}

static int flat_compute_max_frame(struct flat_timeline *timelines, size_t nr_timelines)
{
	int max_frame = 0;
	for (size_t i = 0; i < nr_timelines; i++) {
		int end = timelines[i].begin_frame + timelines[i].frame_count;
		if (end > max_frame)
			max_frame = end;
	}
	return max_frame;
}

// Process script timelines for the current frame.
static void flat_process_layer_scripts(struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines)
{
	if (state->stopped)
		return;
	for (size_t i = 0; i < nr_timelines; i++) {
		if (timelines[i].type != FLAT_TIMELINE_SCRIPT)
			continue;

		// Find last key where frame_index <= current_frame
		int matched = -1;
		for (size_t j = 0; j < timelines[i].script.count; j++) {
			if ((int)timelines[i].script.keys[j].frame_index <= state->current_frame)
				matched = (int)j;
			else
				break;
		}

		// Only trigger when matched key differs
		if (state->last_script_keys[i] == matched)
			continue;
		state->last_script_keys[i] = matched;
		if (matched < 0)
			continue;

		struct flat_script_key *key = &timelines[i].script.keys[matched];
		if (key->has_jump) {
			if (key->jump_frame >= 0 && key->jump_frame < state->current_frame)
				state->has_looped = true;
			state->jump_target = key->jump_frame;
		} else if (key->is_stop) {
			state->stopped = true;
		}
	}
}

// Advance children regardless of this layer's stopped state.
static bool flat_advance_children(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		struct flat_key_stack *key_stack);

// Advance a layer by one tick. Returns true if any frame changed.
static bool flat_advance_layer(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		struct flat_key_stack *key_stack)
{
	int max_end_frame = max(0, flat_compute_max_frame(timelines, nr_timelines) - 1);
	int prev_frame = state->current_frame;

	do {
		int next_frame;
		if (state->jump_target != -1) {
			next_frame = state->jump_target;
			state->jump_target = -1;
		} else {
			int increment = (state->stopped || state->suppress_advance) ? 0 : 1;
			next_frame = state->current_frame + increment;
		}

		state->suppress_advance = false;
		state->current_frame = min(max(0, next_frame), max_end_frame);

		flat_process_layer_scripts(state, timelines, nr_timelines);
	} while (state->jump_target != -1);

	if (state->current_frame < prev_frame)
		parts_flat_clear_emitter_births(state);

	bool child_changed = flat_advance_children(fl, state, timelines, nr_timelines, key_stack);
	return state->current_frame != prev_frame || child_changed;
}

static bool flat_advance_children(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		struct flat_key_stack *key_stack)
{
	bool changed = false;
	for (size_t i = 0; i < nr_timelines; i++) {
		if (timelines[i].type != FLAT_TIMELINE_GRAPHIC)
			continue;

		// Destroy child states when the parent frame leaves the active range.
		int local = state->current_frame - timelines[i].begin_frame;
		if (local < 0 || local >= timelines[i].frame_count) {
			if (state->children[i]) {
				parts_flat_layer_state_free(state->children[i]);
				state->children[i] = NULL;
			}
			for (struct flat_emitter_birth **at = &state->emitter_births; *at;) {
				struct flat_emitter_birth *b = *at;
				if (b->timeline == &timelines[i] || b->timeline_idx == (int)i) {
					*at = b->next;
					free(b);
				} else {
					at = &b->next;
				}
			}
			continue;
		}

		int lib_idx = (key_stack && key_stack->timeline_lib_idx)
				? key_stack->timeline_lib_idx[i]
				: parts_flat_find_library(fl, timelines[i].library_name->text);
		if (lib_idx < 0)
			continue;
		struct flat_library *lib = &fl->libraries[lib_idx];
		if (lib->type == FLAT_LIB_EMITTER) {
			struct flat_emitter *em = &lib->emitter;
			int active_frames = max(1, timelines[i].frame_count - em->particle_lifetime + 1);
			int min_birth = max(0, local - em->particle_lifetime + 1);
			for (struct flat_emitter_birth **at = &state->emitter_births; *at;) {
				struct flat_emitter_birth *b = *at;
				if ((b->timeline == &timelines[i] || b->timeline_idx == (int)i)
						&& (b->frame < min_birth || b->frame > local)) {
					*at = b->next;
					free(b);
				} else {
					at = &b->next;
				}
			}
			if (local < active_frames && key_stack->count > 0) {
				unsigned key_cnt = parts_flat_key_count(&timelines[i], local);
				for (unsigned inst = 0; inst < key_cnt; inst++) {
					struct flat_emitter_birth *b = parts_flat_get_emitter_birth(
							state, &timelines[i], (int)i, local, inst, true);
					b->ancestor_count = key_stack->count;
					for (int d = 0; d < key_stack->count; d++)
						b->ancestor_keys[d] = *key_stack->keys[d];
					b->has_ancestor_keys = true;
				}
			}
			continue;
		}
		if (lib->type != FLAT_LIB_TIMELINE)
			continue;

		if (!state->children[i])
			state->children[i] = parts_flat_layer_state_new(lib->timeline.nr_timelines);
		const struct flat_key_data_graphic *key = parts_flat_key(&timelines[i], local, 0);
		bool pushed = false;
		if (key && key_stack->count < FLAT_MAX_ANCESTOR_DEPTH) {
			int d = key_stack->count++;
			key_stack->keys[d] = key;
			key_stack->timelines[d] = &timelines[i];
			key_stack->locals[d] = local;
			key_stack->instances[d] = 0;
			pushed = true;
		}
		const int *saved_tl_lib_idx = key_stack ? key_stack->timeline_lib_idx : NULL;
		if (key_stack) {
			key_stack->timeline_lib_idx = (key_stack->asset && key_stack->asset->lib_timeline_lib_idx
					&& (size_t)lib_idx < key_stack->asset->nr_libraries)
					? key_stack->asset->lib_timeline_lib_idx[lib_idx] : NULL;
		}
		if (flat_advance_layer(fl, state->children[i],
				lib->timeline.timelines, lib->timeline.nr_timelines, key_stack)) {
			changed = true;
		}
		if (key_stack)
			key_stack->timeline_lib_idx = saved_tl_lib_idx;
		if (pushed)
			key_stack->count--;
	}
	return changed;
}

// Build the CG list for a STOP_MOTION library: frame 0 is the base
// library named in `sm->library_name`; subsequent frames are numbered
// variants `${stem}_NN.${ext}`, terminated by the first missing entry.
static void build_stop_motion_frames(struct flat *flat,
		struct flat_stop_motion_frames *stop_motion_frames, int sm_lib_idx)
{
	struct flat_stop_motion *sm = &flat->libraries[sm_lib_idx].stop_motion;
	const char *base = sm->library_name->text;

	int base_idx = parts_flat_find_library(flat, base);
	if (base_idx < 0)
		return;

	const char *dot = strrchr(base, '.');
	if (!dot) {
		WARNING("flat: invalid STOP_MOTION library name '%s'", base);
		return;
	}

	int *indices = xmalloc(sizeof(int));
	indices[0] = base_idx;
	int count = 1;

	char name_buf[512];
	for (int i = 1; ; i++) {
		snprintf(name_buf, sizeof(name_buf), "%.*s_%02d.%s",
				(int)(dot - base), base, i, dot + 1);
		int idx = parts_flat_find_library(flat, name_buf);
		if (idx < 0)
			break;
		indices = xrealloc(indices, (count + 1) * sizeof(int));
		indices[count++] = idx;
	}

	stop_motion_frames[sm_lib_idx].lib_indices = indices;
	stop_motion_frames[sm_lib_idx].count = count;
}

static int stop_motion_frame_index(int loop_type, int span, int local, int total)
{
	if (total <= 1)
		return 0;
	int idx = (span > 0) ? local / span : 0;
	switch (loop_type) {
	case 0:  // stop at last frame
		return min(idx, total - 1);
	case 1:  // loop
		return idx % total;
	case 2: {  // ping-pong
		int period = total * 2 - 2;
		idx = idx % period;
		return idx < total ? idx : period - idx;
	}
	default:
		return 0;
	}
}

int parts_flat_stop_motion_get_cg_lib(struct parts_flat *f, int sm_lib_idx, int local)
{
	if (sm_lib_idx < 0 || (size_t)sm_lib_idx >= f->nr_libraries)
		return -1;
	struct flat_stop_motion_frames *frames = &f->stop_motion_frames[sm_lib_idx];
	if (frames->count <= 0)
		return -1;
	struct flat_stop_motion *sm = &f->flat->libraries[sm_lib_idx].stop_motion;
	int frame = stop_motion_frame_index(sm->loop_type, sm->span, local, frames->count);
	return frames->lib_indices[frame];
}

// TALT stores named rectangles in shared images instead of individual LIBL CGs.
// Upload each TALT atlas once and reference its handle + UV sub-rect per library
// entry, while preserving each entry's logical sub-rect dimensions and opacity.
static void parts_flat_load_atlases(struct parts_flat_asset *a, size_t library_index)
{
	struct flat *flat = a->flat;
	a->nr_talt_atlases = flat->nr_talt_entries;
	if (a->nr_talt_atlases > 0)
		a->talt_atlases = xcalloc(a->nr_talt_atlases, sizeof(Texture));

	for (size_t i = 0; i < flat->nr_talt_entries; i++) {
		struct talt_entry *entry = &flat->talt_entries[i];
		if (entry->nr_meta == 0)
			continue;
		struct cg *atlas = NULL;
		if (entry->off <= flat->data_size && entry->size >= 16
				&& entry->size <= flat->data_size - entry->off)
			atlas = cg_load_buffer(flat->data + entry->off, entry->size);
		if (!atlas)
			WARNING("flat: failed to load TALT image %zu", i);
		else
			gfx_init_texture_with_cg(&a->talt_atlases[i], atlas);
		for (size_t j = 0; j < entry->nr_meta; j++, library_index++) {
			struct talt_metadata *meta = &entry->metadata[j];
			struct flat_library *lib = &flat->libraries[library_index];
			lib->type = FLAT_LIB_CG;
			lib->name = string_ref(&EMPTY_STRING);
			if (meta->unknown1_off > flat->data_size
					|| meta->unknown1_size > flat->data_size - meta->unknown1_off) {
				WARNING("flat: invalid TALT name in image %zu", i);
				continue;
			}
			free_string(lib->name);
			lib->name = make_string((char *)flat->data + meta->unknown1_off, meta->unknown1_size);
			if (flat->elna.present) {
				for (size_t k = 0; k < (size_t)lib->name->size; k++)
					lib->name->text[k] ^= 0x55;
			}
			if (!atlas)
				continue;
			uint32_t x = meta->unknown2, y = meta->unknown3;
			uint32_t w = meta->unknown4, h = meta->unknown5;
			if (!w || !h || x > (uint32_t)atlas->metrics.w || y > (uint32_t)atlas->metrics.h
					|| w > (uint32_t)atlas->metrics.w - x || h > (uint32_t)atlas->metrics.h - y) {
				WARNING("flat: invalid TALT rectangle for '%s'", display_sjis0(lib->name->text));
				continue;
			}
			bool sub_opaque = !atlas->metrics.has_alpha;
			if (!sub_opaque && atlas->pixels
					&& (int)w >= config.view_width && (int)h >= config.view_height) {
				sub_opaque = true;
				for (uint32_t row = 0; row < h && sub_opaque; row++) {
					const uint8_t *src_row = (const uint8_t *)atlas->pixels
							+ ((size_t)(y + row) * atlas->metrics.w + x) * 4;
					for (uint32_t col = 0; col < w; col++) {
						if (src_row[col * 4 + 3] != 255) {
							sub_opaque = false;
							break;
						}
					}
				}
			}
			a->base_textures[library_index] = (Texture){
				.handle = a->talt_atlases[i].handle,
				.w = (int)w,
				.h = (int)h,
				.opaque = sub_opaque,
				.ctx_gen = a->talt_atlases[i].ctx_gen,
			};
			a->base_rects[library_index] = (Rectangle){ (int)x, (int)y, (int)w, (int)h };
			a->base_atlas_idx[library_index] = (int)i;
		}
		cg_free(atlas);
	}
}

static uint32_t flat_str_hash(const char *text, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint8_t)text[i];
		h *= 16777619u;
	}
	return h;
}

static int flat_lib_hash_find(const struct flat *fl, const int *buckets, size_t mask,
		const struct string *name)
{
	if (!name || !buckets)
		return -1;
	size_t len = (size_t)name->size;
	size_t slot = flat_str_hash(name->text, len) & mask;
	for (;;) {
		int idx = buckets[slot];
		if (idx < 0)
			return -1;
		const struct string *cand = fl->libraries[idx].name;
		if (cand && (size_t)cand->size == len && memcmp(cand->text, name->text, len) == 0)
			return idx;
		slot = (slot + 1) & mask;
	}
}

static int *flat_resolve_timeline_array(const struct flat *fl,
		const struct flat_timeline *timelines, size_t nr_timelines,
		const int *buckets, size_t mask)
{
	if (nr_timelines == 0)
		return NULL;
	int *out = xmalloc(nr_timelines * sizeof(int));
	for (size_t i = 0; i < nr_timelines; i++) {
		if (timelines[i].type == FLAT_TIMELINE_GRAPHIC && timelines[i].library_name)
			out[i] = flat_lib_hash_find(fl, buckets, mask, timelines[i].library_name);
		else
			out[i] = -1;
	}
	return out;
}

static void flat_build_lib_index_cache(struct parts_flat_asset *a)
{
	struct flat *fl = a->flat;
	int *buckets = NULL;
	size_t mask = 0;
	if (fl->nr_libraries > 0) {
		size_t cap = 16;
		while (cap < fl->nr_libraries * 2)
			cap <<= 1;
		mask = cap - 1;
		buckets = xmalloc(cap * sizeof(int));
		for (size_t i = 0; i < cap; i++)
			buckets[i] = -1;
		for (size_t i = 0; i < fl->nr_libraries; i++) {
			const struct string *name = fl->libraries[i].name;
			if (!name)
				continue;
			size_t len = (size_t)name->size;
			size_t slot = flat_str_hash(name->text, len) & mask;
			for (;;) {
				int existing = buckets[slot];
				if (existing < 0) {
					buckets[slot] = (int)i;
					break;
				}
				const struct string *cand = fl->libraries[existing].name;
				if (cand && (size_t)cand->size == len
						&& memcmp(cand->text, name->text, len) == 0)
					break; // preserve first-match semantics of parts_flat_find_library
				slot = (slot + 1) & mask;
			}
		}
	}

	a->root_timeline_lib_idx = flat_resolve_timeline_array(
			fl, fl->timelines, fl->nr_timelines, buckets, mask);
	if (a->nr_libraries > 0) {
		a->lib_timeline_lib_idx = xcalloc(a->nr_libraries, sizeof(int *));
		a->emitter_cg_lib_idx = xmalloc(a->nr_libraries * sizeof(int));
		for (size_t i = 0; i < a->nr_libraries; i++) {
			a->emitter_cg_lib_idx[i] = -1;
			if (fl->libraries[i].type == FLAT_LIB_TIMELINE) {
				a->lib_timeline_lib_idx[i] = flat_resolve_timeline_array(
						fl,
						fl->libraries[i].timeline.timelines,
						fl->libraries[i].timeline.nr_timelines,
						buckets, mask);
			} else if (fl->libraries[i].type == FLAT_LIB_EMITTER) {
				a->emitter_cg_lib_idx[i] = flat_lib_hash_find(
						fl, buckets, mask, fl->libraries[i].emitter.library_name);
			}
		}
	}
	free(buckets);
}

static struct parts_flat_asset *flat_asset_load(struct string *filename)
{
	size_t len = (size_t)filename->size;
	struct archive_data *dfile = asset_get_by_name(ASSET_FLAT, filename->text, NULL);
	if (!dfile)
		return NULL;

	int error;
	struct flat *fl = flat_open(dfile->data, dfile->size, &error);
	if (!fl) {
		archive_free_data(dfile);
		return NULL;
	}

	struct parts_flat_asset *a = xcalloc(1, sizeof(*a));
	a->name = xstrdup(filename->text);
	a->name_len = len;
	a->ref = 1;
	a->flat = fl;
	a->end_frame = max(0, flat_compute_max_frame(fl->timelines, fl->nr_timelines) - 1);

	size_t nr_libl = fl->nr_libraries;
	a->nr_libraries = nr_libl;
	for (size_t i = 0; i < fl->nr_talt_entries; i++)
		a->nr_libraries += fl->talt_entries[i].nr_meta;
	if (a->nr_libraries != nr_libl) {
		fl->libraries = xrealloc(fl->libraries,
				a->nr_libraries * sizeof(struct flat_library));
		memset(fl->libraries + nr_libl, 0,
				(a->nr_libraries - nr_libl) * sizeof(struct flat_library));
		fl->nr_libraries = a->nr_libraries;
	}

	a->base_textures = xcalloc(a->nr_libraries, sizeof(Texture));
	a->base_rects = xcalloc(a->nr_libraries, sizeof(Rectangle));
	a->base_atlas_idx = xmalloc(a->nr_libraries * sizeof(int));
	for (size_t i = 0; i < a->nr_libraries; i++)
		a->base_atlas_idx[i] = -1;
	a->stop_motion_frames = xcalloc(a->nr_libraries, sizeof(struct flat_stop_motion_frames));
	for (size_t i = 0; i < nr_libl; i++) {
		struct flat_library *lib = &fl->libraries[i];
		if (lib->type != FLAT_LIB_CG)
			continue;
		struct cg *cg = cg_load_buffer((uint8_t *)lib->cg.data, lib->cg.size);
		if (!cg) {
			WARNING("flat: failed to load CG for library '%s'", lib->name->text);
			continue;
		}
		gfx_init_texture_with_cg(&a->base_textures[i], cg);
		a->base_rects[i] = (Rectangle){ 0, 0, a->base_textures[i].w, a->base_textures[i].h };
		cg_free(cg);
		// Clear pointer into dfile->data before archive_free_data(dfile).
		lib->cg.data = NULL;
		lib->cg.size = 0;
	}
	parts_flat_load_atlases(a, nr_libl);
	flat_build_lib_index_cache(a);
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		if (fl->libraries[i].type == FLAT_LIB_STOP_MOTION)
			build_stop_motion_frames(fl, a->stop_motion_frames, (int)i);
	}
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		if (fl->libraries[i].type != FLAT_LIB_EMITTER)
			continue;
		struct flat_emitter *em = &fl->libraries[i].emitter;
		if (em->end_pos_type < 0 || em->end_pos_type > 1)
			WARNING("flat: emitter '%s': unsupported end_pos_type %d",
					display_sjis0(em->library_name->text), em->end_pos_type);
		if (em->pos_track_mode < 0 || em->pos_track_mode > 2)
			WARNING("flat: emitter '%s': unsupported pos_track_mode %d",
					display_sjis0(em->library_name->text), em->pos_track_mode);
	}

	int w = fl->hdr.width;
	int h = fl->hdr.height;
	if (ain->version < 12 && (w <= 0 || h <= 0)) {
		w = 0;
		h = 0;
		for (size_t i = 0; i < a->nr_libraries; i++) {
			if (a->base_textures[i].w > w)
				w = a->base_textures[i].w;
			if (a->base_textures[i].h > h)
				h = a->base_textures[i].h;
		}
	}
	a->width = max(0, w);
	a->height = max(0, h);

	archive_free_data(dfile);
	return a;
}

static struct parts_flat_asset *flat_asset_acquire(struct string *filename)
{
	size_t len = (size_t)filename->size;
	for (struct parts_flat_asset *a = flat_asset_head; a; a = a->next) {
		if (a->name_len == len && memcmp(a->name, filename->text, len) == 0) {
			if (a->ref == 0)
				flat_asset_idle_count--;
			a->ref++;
			if (a != flat_asset_head) {
				flat_asset_unlink(a);
				flat_asset_push_front(a);
			}
			return a;
		}
	}
	struct parts_flat_asset *a = flat_asset_load(filename);
	if (a)
		flat_asset_push_front(a);
	return a;
}

void parts_flat_recover_widget_skin(struct parts_flat *f, bool suspended)
{
	if (f->common.texture.handle)
		gfx_remap_texture(&f->common.texture);
	struct parts_flat_asset *source = NULL;
	bool tried_load = false;
	for (size_t i = 0; f->textures && i < f->nr_libraries; i++) {
		Texture *t = &f->textures[i];
		if (t->handle && gfx_remap_texture(t))
			continue;
		if (suspended || !f->asset || t->w <= 0)
			continue;
		struct string *proxy = f->proxy_cg_names ? f->proxy_cg_names[i] : NULL;
		if (f->owns_texture && f->owns_texture[i]) {
			if (proxy) {
				int no;
				struct cg *cg = asset_cg_load_by_name(proxy->text, &no);
				if (cg) {
					gfx_init_texture_with_cg(t, cg);
					cg_free(cg);
				}
			}
			continue;
		}
		int index = proxy ? parts_flat_find_library(f->flat, proxy->text) : (int)i;
		if (index < 0 || (size_t)index >= f->asset->nr_libraries)
			continue;
		Texture *base = &f->asset->base_textures[index];
		int aidx = f->asset->base_atlas_idx ? f->asset->base_atlas_idx[index] : -1;
		if (aidx >= 0 && (size_t)aidx < f->asset->nr_talt_atlases) {
			Texture *atlas = &f->asset->talt_atlases[aidx];
			if (!gfx_remap_texture(atlas)) {
				if (!tried_load) {
					source = flat_asset_load(f->name);
					tried_load = true;
				}
				if (source && (size_t)aidx < source->nr_talt_atlases) {
					*atlas = source->talt_atlases[aidx];
					source->talt_atlases[aidx].handle = 0;
				}
			}
			for (size_t k = 0; k < f->asset->nr_libraries; k++) {
				if (f->asset->base_atlas_idx[k] == aidx) {
					f->asset->base_textures[k].handle = atlas->handle;
					f->asset->base_textures[k].ctx_gen = atlas->ctx_gen;
				}
			}
		} else if (!gfx_remap_texture(base)) {
			if (!tried_load) {
				source = flat_asset_load(f->name);
				tried_load = true;
			}
			if (source && (size_t)index < source->nr_libraries) {
				*base = source->base_textures[index];
				source->base_textures[index].handle = 0;
			}
		}
		*t = *base;
	}
	if (source)
		flat_asset_destroy(source);
}

bool parts_flat_load(struct parts *parts, struct parts_flat *f, struct string *filename)
{
	if (f->flat) {
		float fps_rate = f->fps_rate;
		parts_flat_free(f);
		memset(f, 0, sizeof(struct parts_flat));
		f->fps_rate = fps_rate;
		parts_set_dims(parts, &f->common, 0, 0);
		parts_dirty(parts);
	}

	if (!filename || !filename->size || !filename->text[0])
		return false;

	struct parts_flat_asset *asset = flat_asset_acquire(filename);
	if (!asset)
		return false;

	f->asset = asset;
	f->flat = asset->flat;
	f->name = string_dup(filename);
	f->stopped = false;
	f->elapsed = 0;
	f->end_frame = asset->end_frame;
	f->root_state = parts_flat_layer_state_new(f->flat->nr_timelines);
	f->nr_libraries = asset->nr_libraries;
	f->textures = xcalloc(f->nr_libraries, sizeof(Texture));
	if (f->nr_libraries > 0)
		memcpy(f->textures, asset->base_textures, f->nr_libraries * sizeof(Texture));
	f->owns_texture = xcalloc(f->nr_libraries, sizeof(bool));
	f->proxy_cg_names = xcalloc(f->nr_libraries, sizeof(struct string *));
	f->stop_motion_frames = asset->stop_motion_frames;

	parts_set_dims(parts, &f->common, asset->width, asset->height);
	f->needs_advance = true;
	parts_frame_did_heavy_load = true;
	return true;
}

bool parts_flat_update(struct parts_flat *f, int passed_time)
{
	if (!f->flat || f->flat->hdr.fps <= 0)
		return false;

	// Consume pending forward seek (from GoFramePartsFlat).
	if (f->pending_seek_delta > 0) {
		for (int i = 0; i < f->pending_seek_delta; i++) {
			struct flat_key_stack key_stack = {
				.asset = f->asset,
				.timeline_lib_idx = f->asset ? f->asset->root_timeline_lib_idx : NULL,
			};
			flat_advance_layer(f->flat, f->root_state,
					f->flat->timelines, f->flat->nr_timelines, &key_stack);
		}
		f->pending_seek_delta = 0;
		f->elapsed = 0;
		return true;
	}

	// One advance after load / seek.
	if (f->needs_advance) {
		f->needs_advance = false;
		struct flat_key_stack key_stack = {
			.asset = f->asset,
			.timeline_lib_idx = f->asset ? f->asset->root_timeline_lib_idx : NULL,
		};
		bool changed = flat_advance_layer(f->flat, f->root_state,
				f->flat->timelines, f->flat->nr_timelines, &key_stack);
		f->elapsed = 0;
		return changed;
	}

	if (f->stopped || passed_time <= 0 || !isfinite(f->fps_rate) || f->fps_rate == 0)
		return false;

	f->elapsed += passed_time * (double)f->fps_rate;
	// Negative rates in v12 advance one frame per update, rather than reversing.
	double frames = f->elapsed * f->flat->hdr.fps / 1000.0;
	int delta = f->fps_rate < 0 ? 1 : (int)min(frames, (double)INT_MAX);
	if (!delta)
		return false;
	f->elapsed = f->fps_rate < 0 ? 0 : f->elapsed - (double)delta * 1000.0 / f->flat->hdr.fps;

	bool any_changed = false;
	for (int i = 0; i < delta; i++) {
		struct flat_key_stack key_stack = {
			.asset = f->asset,
			.timeline_lib_idx = f->asset ? f->asset->root_timeline_lib_idx : NULL,
		};
		if (flat_advance_layer(f->flat, f->root_state,
				f->flat->timelines, f->flat->nr_timelines, &key_stack))
			any_changed = true;
	}
	return any_changed;
}

unsigned parts_flat_key_count(const struct flat_timeline *tl, int frame)
{
	if (frame < 0 || frame >= tl->frame_count) return 0;
	if (tl->graphic.frames) return tl->graphic.frames[frame].count;
	return (tl->graphic.keys && (unsigned)frame < tl->graphic.count) ? 1 : 0;
}

const struct flat_key_data_graphic *parts_flat_key(const struct flat_timeline *tl, int frame, unsigned instance)
{
	if (instance >= parts_flat_key_count(tl, frame)) return NULL;
	if (tl->graphic.frames) {
		if (!tl->graphic.frames[frame].keys)
			return NULL;
		return &tl->graphic.frames[frame].keys[instance];
	}
	return tl->graphic.keys ? &tl->graphic.keys[frame] : NULL;
}

// Build a layer matrix from a single key's properties.
// The sequence is: T(pos) * Rz * Rx * Ry * S(scale) * T(-origin) * Reverse.
// Components can be selectively enabled; pos is resolved by the caller.
void parts_flat_build_layer_matrix(const struct flat_key_data_graphic *key,
		vec2 pos,
		bool use_rotation, bool use_scale, bool use_origin,
		bool reverse_lr, bool reverse_tb,
		mat4 out)
{
	glm_mat4_identity(out);
	glm_translate(out, (vec3){ pos[0], pos[1], 0 });
	// The original engine projects 3D-rotated sprites through a proper
	// perspective, but we use an orthographic approximation for simplicity.
	if (use_rotation) {
		if (key->angle_z != 0) glm_rotate_z(out, glm_rad(key->angle_z), out);
		if (key->angle_x != 0) glm_rotate_x(out, glm_rad(-key->angle_x), out);
		if (key->angle_y != 0) glm_rotate_y(out, glm_rad(key->angle_y), out);
	}
	if (use_scale)
		glm_scale(out, (vec3){ key->scale_x, key->scale_y, 1.0f });
	if (use_origin)
		glm_translate(out, (vec3){ -(float)key->origin_x, -(float)key->origin_y, 0 });
	if (reverse_lr || reverse_tb) {
		glm_scale(out, (vec3){
				reverse_lr ? -1.0f : 1.0f,
				reverse_tb ? -1.0f : 1.0f,
				1.0f });
	}
}

static bool flat_layer_hittest(struct parts_flat *f,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		const int *timeline_lib_idx,
		mat4 parent_m, float fx, float fy, int depth)
{
	if (depth >= FLAT_MAX_ANCESTOR_DEPTH)
		return false;
	int current_frame = state ? state->current_frame : 0;
	for (size_t i = nr_timelines; i-- > 0;) {
		if (state && state->disabled_timelines && i < state->nr_timelines && state->disabled_timelines[i])
			continue;
		struct flat_timeline *tl = &timelines[i];
		if (tl->type != FLAT_TIMELINE_GRAPHIC)
			continue;
		int local = current_frame - tl->begin_frame;
		if (local < 0 || local >= tl->frame_count)
			continue;

		int lib_idx = timeline_lib_idx
				? timeline_lib_idx[i]
				: parts_flat_find_library(f->flat, tl->library_name->text);
		if (lib_idx < 0 || (size_t)lib_idx >= f->flat->nr_libraries)
			continue;
		struct flat_library *lib = &f->flat->libraries[lib_idx];
		if (lib->type == FLAT_LIB_EMITTER || lib->type == FLAT_LIB_MEMORY)
			continue;

		for (unsigned instance = 0; instance < parts_flat_key_count(tl, local); instance++) {
			const struct flat_key_data_graphic *key = parts_flat_key(tl, local, instance);
			if (!key || key->alpha <= 0)
				continue;
			mat4 layer_m, m;
			vec2 pos = { key->pos_x, key->pos_y };
			parts_flat_build_layer_matrix(key, pos,
					true, true, true,
					key->reverse_lr, key->reverse_tb,
					layer_m);
			glm_mat4_mul(parent_m, layer_m, m);

			if (lib->type == FLAT_LIB_TIMELINE) {
				struct flat_layer_state *child = (state && i < state->nr_timelines)
						? state->children[i] : NULL;
				const int *child_lib_idx = (f->asset && f->asset->lib_timeline_lib_idx
						&& (size_t)lib_idx < f->asset->nr_libraries)
						? f->asset->lib_timeline_lib_idx[lib_idx] : NULL;
				if (flat_layer_hittest(f, child,
						lib->timeline.timelines, lib->timeline.nr_timelines,
						child_lib_idx, m, fx, fy, depth + 1))
					return true;
				continue;
			}

			int cg_idx = lib_idx;
			if (lib->type == FLAT_LIB_STOP_MOTION) {
				cg_idx = parts_flat_stop_motion_get_cg_lib(f, lib_idx, local);
				if (cg_idx < 0 || (size_t)cg_idx >= f->nr_libraries)
					continue;
			} else if (lib->type != FLAT_LIB_CG) {
				continue;
			}

			Texture *tex = &f->textures[cg_idx];
			int width = key->area_width > 0 ? key->area_width : tex->w;
			int height = key->area_height > 0 ? key->area_height : tex->h;
			if (width <= 0 || height <= 0)
				continue;

			if (key->align >= 1 && key->align <= 9) {
				int anchor = key->align - 1;
				glm_translate(m, (vec3){-(anchor % 3) * width / 2.0f,
						-(anchor / 3) * height / 2.0f, 0});
			}

			float det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
			if (!isfinite(det) || fabsf(det) < 1e-12f)
				continue;
			float dx = fx - m[3][0], dy = fy - m[3][1];
			float lx = (m[1][1] * dx - m[1][0] * dy) / det;
			float ly = (m[0][0] * dy - m[0][1] * dx) / det;
			if (lx >= 0.0f && ly >= 0.0f && lx < (float)width && ly < (float)height)
				return true;
		}
	}
	return false;
}

bool parts_flat_hittest(struct parts_flat *f, float x, float y)
{
	if (!f->flat || !f->root_state)
		return false;
	if (f->common.surface_area.w || f->common.surface_area.h) {
		Rectangle hb = f->common.hitbox;
		if (!(x >= hb.x && y >= hb.y && x < hb.x + hb.w && y < hb.y + hb.h))
			return false;
	}
	float fx = x - f->common.origin_offset.x;
	float fy = y - f->common.origin_offset.y;
	mat4 root;
	glm_mat4_identity(root);
	return flat_layer_hittest(f, f->root_state,
			f->flat->timelines, f->flat->nr_timelines,
			f->asset ? f->asset->root_timeline_lib_idx : NULL,
			root, fx, fy, 0);
}

// Build the emitter's particle base matrix from the recorded ancestor key
// chain.
//
// SELECTIVE mode: each ancestor contributes a fresh key with defaults
// (scale=1, rotation=0, origin=0, reverse=false), overriding only the
// properties listed in the emitter's inherit_* flags.
//
// ANCESTOR_CURRENT / ANCESTOR_AT_BIRTH modes: the keys from the entire
// parent view chain are used as-is. inherit_* flags are ignored; all
// ancestor properties affect the particle base matrix.
void parts_flat_build_emitter_base_matrix(const struct flat_emitter *em,
		const struct flat_key_stack *stack, mat4 out)
{
	bool connected = em->parent_key_mode != EMITTER_PARENT_KEY_SELECTIVE;
	glm_mat4_identity(out);
	for (int i = 0; i < stack->count; i++) {
		const struct flat_key_data_graphic *key = stack->keys[i];
		vec2 pos = { key->pos_x, key->pos_y };

		mat4 layer_m;
		parts_flat_build_layer_matrix(key, pos,
				connected || em->inherit_rotation,
				connected || em->inherit_scale,
				connected,
				(connected || em->inherit_reverse_lr) && key->reverse_lr,
				(connected || em->inherit_reverse_tb) && key->reverse_tb,
				layer_m);

		glm_mat4_mul(out, layer_m, out);
	}
}

// Returns how many particles should be born on birth_frame, distributing
// create_count particles evenly across active_frames.
static int emitter_get_birth_count(int birth_frame, int create_count,
		int frame_count, int particle_lifetime)
{
	int active_frames = frame_count - particle_lifetime + 1;
	if (active_frames <= 1)
		return create_count;
	if (create_count == 1)
		return birth_frame == 0 ? 1 : 0;

	if (create_count >= active_frames) {
		// Dense case: distribute uniformly so each frame gets floor or ceil of
		// the average rate.
		float rate = (float)create_count / active_frames;
		return (int)((birth_frame + 1) * rate) - (int)(birth_frame * rate);
	}

	// Sparse case: pin one particle to frame 0, then space the remaining
	// create_count-1 particles evenly from frame 1 to active_frames-1 (so
	// frame active_frames-1 also always gets one particle).
	if (birth_frame == 0)
		return 1;
	float rate = (float)(create_count - 1) / (active_frames - 1);
	return (int)(birth_frame * rate) - (int)((birth_frame - 1) * rate);
}

static float emitter_fade_alpha(int age, struct flat_emitter *em)
{
	if (em->fade_in_frame > 0 && age < em->fade_in_frame)
		return (float)age / em->fade_in_frame;
	if (em->fade_out_frame > 0 && age > em->particle_lifetime - em->fade_out_frame)
		return (float)(em->particle_lifetime - age) / em->fade_out_frame;
	return 1.0f;
}

static float emitter_gravity_displacement(int age, struct flat_emitter *em, int fps)
{
	if (!em->is_fall)
		return 0;
	const float G = 9.8f;
	float t = (float)age / fps;
	if (em->width != 0 && em->air_resistance != 0) {
		float k = em->air_resistance / em->width;
		float exp_term = 1 - expf(-k * t);
		return (t - exp_term / k) * em->width * G / em->air_resistance;
	}
	return 0.5f * G * t * t;
}

// Lerp from `begin` to `end` at `t`, with per-endpoint random jitter scaled
// by begin_rand / end_rand. When `sync` is true, both endpoints share the
// same jitter draw (but the second draw is still consumed, to keep the RNG
// sequence stable across the sync flag).
static float lerp_with_jitter(struct mt19937 *mt,
		float begin, float begin_rand, float end, float end_rand,
		bool sync, float t)
{
	float r1 = mt_next_signed(mt) * begin_rand;
	float r2 = mt_next_signed(mt) * end_rand;
	float b = begin + r1;
	float e = end + (sync ? r1 : r2);
	return b + (e - b) * t;
}

static void emitter_interpolate_scale(float t, struct flat_emitter *em,
		struct mt19937 *mt, vec2 out)
{
	float overall = lerp_with_jitter(mt, em->begin_scale, em->begin_scale_rand,
			em->end_scale, em->end_scale_rand, em->sync_scale_rand, t);
	float x = lerp_with_jitter(mt, em->begin_x_scale, em->begin_x_scale_rand,
			em->end_x_scale, em->end_x_scale_rand, em->sync_scale_rand, t);
	float y = lerp_with_jitter(mt, em->begin_y_scale, em->begin_y_scale_rand,
			em->end_y_scale, em->end_y_scale_rand, em->sync_scale_rand, t);
	out[0] = x * overall;
	out[1] = y * overall;
}

static void emitter_interpolate_rotation(float t, struct flat_emitter *em,
		struct mt19937 *mt, vec3 out)
{
	out[0] = lerp_with_jitter(mt, em->begin_x_angle, em->begin_x_angle_rand,
			em->end_x_angle, em->end_x_angle_rand, em->sync_rotation_rand, t);
	out[1] = lerp_with_jitter(mt, em->begin_y_angle, em->begin_y_angle_rand,
			em->end_y_angle, em->end_y_angle_rand, em->sync_rotation_rand, t);
	out[2] = lerp_with_jitter(mt, em->begin_z_angle, em->begin_z_angle_rand,
			em->end_z_angle, em->end_z_angle_rand, em->sync_rotation_rand, t);
}

static void emitter_calc_create_position(struct flat_emitter *em,
		struct mt19937 *mt, vec3 out)
{
	switch (em->create_pos_type) {
	case EMITTER_CREATE_POS_RECT:
		out[0] = mt_next_signed(mt) * em->create_pos_length;
		out[1] = mt_next_signed(mt) * em->create_pos_length2;
		out[2] = 0.0f;
		return;
	case EMITTER_CREATE_POS_SPHERE:
		random_unit_vec3(mt, out);
		break;
	case EMITTER_CREATE_POS_CIRCLE:
		out[0] = mt_next_signed(mt);
		out[1] = mt_next_signed(mt);
		out[2] = 0.0f;
		glm_vec3_normalize(out);
		break;
	default:
		glm_vec3_zero(out);
		break;
	}
	float dist = mt_next(mt) * (em->create_pos_length - em->create_pos_length2)
			+ em->create_pos_length2;
	glm_vec3_scale(out, dist, out);
}

// 3D rejection sampling:
// Picks a random unit vector c with dot(v, c) > cos(rand * angle/2), i.e.
// constrained to within `rand * angle/2` of the input direction.
static void randomize_direction_within_cone(vec3 v, float angle_deg, struct mt19937 *mt)
{
	glm_vec3_normalize(v);
	float theta = mt_next(mt) * glm_rad(angle_deg) * 0.5f;
	if (!(theta > 0))
		return;
	float cos_t = cosf(theta);
	for (int i = 0; i < 1000; i++) {
		vec3 c;
		random_unit_vec3(mt, c);
		if (glm_vec3_dot(v, c) > cos_t) {
			glm_vec3_copy(c, v);
			return;
		}
	}
}

static void emitter_calc_direction(struct flat_emitter *em,
		struct mt19937 *mt, vec2 parent_vel, vec3 create_pos, vec2 out)
{
	vec3 dir;
	switch (em->direction_type) {
	case EMITTER_DIRECTION_RANDOM:
		random_unit_vec3(mt, dir);
		break;
	case EMITTER_DIRECTION_PARENT:
		dir[0] = parent_vel[0];
		dir[1] = parent_vel[1];
		dir[2] = 0;
		break;
	case EMITTER_DIRECTION_PARENT_REVERSE:
		dir[0] = -parent_vel[0];
		dir[1] = -parent_vel[1];
		dir[2] = 0;
		break;
	case EMITTER_DIRECTION_DISK: {
		// direction_x/y/z is the normal of the emission disk. Pick a
		// random unit vector on that disk; randomize_direction_within_cone
		// below thickens it into a band of half-width direction_angle/2.
		vec3 normal = { em->direction_x, em->direction_y, em->direction_z };
		if (glm_vec3_norm2(normal) < 1e-12f)
			glm_vec3_copy((vec3){ 0, 0, 1 }, normal);
		glm_vec3_ortho(normal, dir);
		float phi = mt_next(mt) * 2 * GLM_PIf;
		glm_vec3_rotate(dir, phi, normal);
		break;
	}
	case EMITTER_DIRECTION_RADIAL:
		glm_vec3_copy(create_pos, dir);
		break;
	case EMITTER_DIRECTION_FIXED:
	default:
		glm_vec3_copy((vec3){ em->direction_x, em->direction_y, em->direction_z }, dir);
		break;
	}
	if (em->direction_type != EMITTER_DIRECTION_RADIAL)
		randomize_direction_within_cone(dir, em->direction_angle, mt);
	// Project to 2D by dropping Z without renormalizing in 2D, so
	// directions tilted out of the XY plane translate to slower
	// on-screen motion.
	glm_vec3_normalize(dir);
	out[0] = dir[0];
	out[1] = dir[1];
}

static float emitter_calc_displacement(int age, struct flat_emitter *em,
		int fps, float move_rand_factor)
{
	float t = (float)age / fps;
	float accel = em->acceleration * move_rand_factor;
	float speed = em->speed * move_rand_factor;
	float move_length = em->move_length * move_rand_factor;
	float curve = em->move_curve * move_rand_factor;

	if (accel < 0 && speed != 0) {
		float t_max = fabsf(speed / accel);
		if (t > t_max)
			t = t_max;
	}

	float displacement = 0.5f * accel * t * t + speed * t;

	if (move_length != 0) {
		float norm_t = em->particle_lifetime > 0 ? (float)age / em->particle_lifetime : 0;
		if (curve > 1.0f)
			displacement += powf(norm_t, curve) * move_length;
		else if (curve < -1.0f)
			displacement += (1 - powf(1 - norm_t, -curve)) * move_length;
		else
			displacement += move_length * norm_t;
	}
	return displacement;
}

static void eval_quadratic_bezier_arclen(vec2 p0, vec2 p1, vec2 p2, int step_idx, vec2 out)
{
	if (step_idx > 1024) {
		glm_vec2_zero(out);
		return;
	}
	if (step_idx <= 0) {
		glm_vec2_copy(p0, out);
		return;
	}

	// 64-segment forward-differenced cumulative arc-length table over the
	// same [0, 1023/1024] parameter domain as the 1024-step reference.
	enum { BEZIER_ARCLEN_STEPS = 64 };
	const float s_max = 1023.0f / 1024.0f;
	const float h = s_max / (float)BEZIER_ARCLEN_STEPS;
	float ax = p1[0] - p0[0], ay = p1[1] - p0[1];
	float cx = p0[0] - 2.0f * p1[0] + p2[0];
	float cy = p0[1] - 2.0f * p1[1] + p2[1];
	float dx = 2.0f * h * ax + h * h * cx;
	float dy = 2.0f * h * ay + h * h * cy;
	float ddx = 2.0f * h * h * cx;
	float ddy = 2.0f * h * h * cy;

	float cum_len[BEZIER_ARCLEN_STEPS + 1];
	cum_len[0] = 0.0f;
	float total_len = 0.0f;
	for (int k = 0; k < BEZIER_ARCLEN_STEPS; k++) {
		total_len += sqrtf(dx * dx + dy * dy);
		cum_len[k + 1] = total_len;
		dx += ddx;
		dy += ddy;
	}

	if (total_len == 0.0f) {
		glm_vec2_copy(p0, out);
		return;
	}

	float s;
	if (step_idx == 1024) {
		s = s_max;
	} else {
		float target_len = total_len * ((float)step_idx * (1.0f / 1024.0f));
		int lo = 0, hi = BEZIER_ARCLEN_STEPS;
		while (lo + 1 < hi) {
			int mid = (lo + hi) >> 1;
			if (cum_len[mid] < target_len)
				lo = mid;
			else
				hi = mid;
		}
		float seg_len = cum_len[lo + 1] - cum_len[lo];
		float frac = seg_len > 0.0f ? (target_len - cum_len[lo]) / seg_len : 0.0f;
		s = ((float)lo + frac) * h;
	}

	float om = 1.0f - s;
	float b0 = om * om;
	float b1 = 2.0f * s * om;
	float b2 = s * s;
	out[0] = b0 * p0[0] + b1 * p1[0] + b2 * p2[0];
	out[1] = b0 * p0[1] + b1 * p1[1] + b2 * p2[1];
}

static void emitter_calc_trajectory(int age, struct flat_emitter *em,
		vec2 dir, vec3 create_pos, vec2 track_pos,
		int fps, float pixels_per_meter, float move_rand_factor, vec2 out)
{
	if (em->end_pos_type == EMITTER_END_POS_FIXED && pixels_per_meter != 0.0f) {
		float end_disp = emitter_calc_displacement(em->particle_lifetime, em, fps, move_rand_factor);
		vec2 create_px = {
			create_pos[0] * pixels_per_meter,
			create_pos[1] * pixels_per_meter,
		};
		vec2 p0 = {
			track_pos[0] + create_px[0],
			track_pos[1] + create_px[1],
		};
		vec2 p1 = {
			p0[0] + dir[0] * end_disp * pixels_per_meter,
			p0[1] + dir[1] * end_disp * pixels_per_meter,
		};
		vec2 p2 = { em->end_pos_x, em->end_pos_y };
		if (em->pos_track_mode == 1) {
			p2[0] += track_pos[0];
			p2[1] += track_pos[1];
		}

		float u = em->particle_lifetime > 0
				? max(0.0f, min(1.0f, (float)age / em->particle_lifetime))
				: 0.0f;
		float accel = em->acceleration * move_rand_factor;
		float norm_s;
		if (accel > 0.0f) {
			norm_s = u * u;
		} else if (accel < 0.0f) {
			float om = 1.0f - u;
			norm_s = 1.0f - om * om;
		} else {
			norm_s = u;
		}
		int step_idx = (int)(norm_s * 1024.0f);
		vec2 b_pos;
		eval_quadratic_bezier_arclen(p0, p1, p2, step_idx, b_pos);
		out[0] = (b_pos[0] - create_px[0] - track_pos[0]) / pixels_per_meter;
		out[1] = (b_pos[1] - create_px[1] - track_pos[1]) / pixels_per_meter;
		return;
	}

	float displacement = emitter_calc_displacement(age, em, fps, move_rand_factor);
	glm_vec2_scale(dir, displacement, out);
}

// Resolve the per-frame emitter layer properties (pos, alpha, colors, reverse
// flags, draw_filter) into `out`, applying the emitter's inherit_* flags.
void parts_flat_emitter_resolve_layer(
		const struct flat_emitter *em,
		const struct flat_key_data_graphic *key,
		float parts_alpha, float layer_alpha,
		struct flat_emitter_layer_effective *out)
{
	bool connected = em->parent_key_mode != EMITTER_PARENT_KEY_SELECTIVE;
	out->use_origin = connected;
	out->use_scale = connected || em->inherit_scale;
	out->use_rotation = connected || em->inherit_rotation;
	out->pos[0] = key->pos_x;
	out->pos[1] = key->pos_y;
	out->reverse_lr = (connected || em->inherit_reverse_lr) && key->reverse_lr;
	out->reverse_tb = (connected || em->inherit_reverse_tb) && key->reverse_tb;
	out->alpha = (connected || em->inherit_alpha) ? layer_alpha : parts_alpha;
	if (connected || em->inherit_add_color) {
		out->add_color[0] = key->add_r / 255.0f;
		out->add_color[1] = key->add_g / 255.0f;
		out->add_color[2] = key->add_b / 255.0f;
	} else {
		glm_vec3_zero(out->add_color);
	}
	if (connected || em->inherit_mul_color) {
		out->mul_color[0] = key->mul_r / 255.0f;
		out->mul_color[1] = key->mul_g / 255.0f;
		out->mul_color[2] = key->mul_b / 255.0f;
	} else {
		glm_vec3_one(out->mul_color);
	}
	int draw_filter = (connected || em->inherit_draw_filter) ? key->draw_filter
			: PARTS_DRAW_FILTER_NORMAL;
	// Emitter's own draw_filter overrides the inherited one.
	out->draw_filter = em->draw_filter != PARTS_DRAW_FILTER_NORMAL
			? em->draw_filter : draw_filter;
}

// Compute the alignment origin offset for particle_align (1-9 numpad layout).
bool parts_flat_emitter_get_align_offset(struct parts_flat *f, int emitter_lib_idx, vec2 out)
{
	struct flat_emitter *em = &f->flat->libraries[emitter_lib_idx].emitter;
	if (em->particle_lifetime <= 0 || em->create_count <= 0)
		return false;
	if (f->flat->hdr.fps <= 0)
		return false;

	int cg_lib_idx = (f->asset && f->asset->emitter_cg_lib_idx
			&& emitter_lib_idx >= 0 && (size_t)emitter_lib_idx < f->asset->nr_libraries)
			? f->asset->emitter_cg_lib_idx[emitter_lib_idx]
			: parts_flat_find_library(f->flat, em->library_name->text);
	if (cg_lib_idx < 0 || (size_t)cg_lib_idx >= f->flat->nr_libraries)
		return false;
	if (f->flat->libraries[cg_lib_idx].type == FLAT_LIB_STOP_MOTION) {
		cg_lib_idx = parts_flat_stop_motion_get_cg_lib(f, cg_lib_idx, 0);
		if (cg_lib_idx < 0)
			return false;
	}

	Texture *tex = &f->textures[cg_lib_idx];
	if (!tex->handle)
		return false;

	int align = em->particle_align;
	if (align < 1 || align > 9) align = 5;
	int col = (align - 1) % 3;
	int row = (align - 1) / 3;
	out[0] = tex->w * col / 2.0f;
	out[1] = tex->h * row / 2.0f;
	return true;
}

// Enumerate the particles spawned on `birth_frame` for this emitter, computing
// each particle's pose at the given `age` (in frames since its birth) and
// invoking `fn` with the result. The RNG is seeded from the emitter's
// rand_seed and birth_frame, so the same birth_frame always yields the same
// particles regardless of `age`.
void parts_flat_foreach_emitter_particle(struct parts_flat *f, int emitter_lib_idx,
		const struct flat_timeline *timeline, unsigned instance,
		int birth_frame, int age, int frame_count,
		flat_emitter_particle_fn fn, void *ud)
{
	struct flat *fl = f->flat;
	struct flat_emitter *em = &fl->libraries[emitter_lib_idx].emitter;

	int count = emitter_get_birth_count(birth_frame, em->create_count,
			frame_count, em->particle_lifetime);
	if (count == 0)
		return;

	float fade_alpha = emitter_fade_alpha(age, em);
	if (fade_alpha <= 0.f)
		return;

	// Resolve the particle CG.
	int lib_idx = (f->asset && f->asset->emitter_cg_lib_idx
			&& emitter_lib_idx >= 0 && (size_t)emitter_lib_idx < f->asset->nr_libraries)
			? f->asset->emitter_cg_lib_idx[emitter_lib_idx]
			: parts_flat_find_library(fl, em->library_name->text);
	if (lib_idx < 0)
		return;
	if (fl->libraries[lib_idx].type == FLAT_LIB_STOP_MOTION) {
		lib_idx = parts_flat_stop_motion_get_cg_lib(f, lib_idx, age);
		if (lib_idx < 0)
			return;
	}

	// Parent velocity at birth_frame (only consumed when direction_type
	// is PARENT or PARENT_REVERSE). Backward difference at frame > 0,
	// forward difference at frame 0, zero otherwise.
	vec2 parent_vel = { 0, 0 };
	if (em->direction_type == EMITTER_DIRECTION_PARENT
			|| em->direction_type == EMITTER_DIRECTION_PARENT_REVERSE) {
		const struct flat_key_data_graphic *cur = parts_flat_key(timeline, birth_frame, instance);
		if (cur) {
			if (birth_frame > 0) {
				const struct flat_key_data_graphic *prev = parts_flat_key(timeline, birth_frame - 1, instance);
				if (prev) {
					parent_vel[0] = cur->pos_x - prev->pos_x;
					parent_vel[1] = cur->pos_y - prev->pos_y;
				}
			} else if (birth_frame + 1 < frame_count) {
				const struct flat_key_data_graphic *next = parts_flat_key(timeline, birth_frame + 1, instance);
				if (next) {
					parent_vel[0] = next->pos_x - cur->pos_x;
					parent_vel[1] = next->pos_y - cur->pos_y;
				}
			}
		}
	}

	vec2 track_pos = { 0, 0 };
	if (em->end_pos_type == EMITTER_END_POS_FIXED) {
		if (em->pos_track_mode == 1) {
			const struct flat_key_data_graphic *cur = parts_flat_key(timeline, birth_frame + age, instance);
			if (cur) {
				track_pos[0] = cur->pos_x;
				track_pos[1] = cur->pos_y;
			}
		} else if (em->pos_track_mode == 2) {
			const struct flat_key_data_graphic *birth = parts_flat_key(timeline, birth_frame, instance);
			if (birth) {
				track_pos[0] = birth->pos_x;
				track_pos[1] = birth->pos_y;
			}
		}
	}

	struct mt19937 mt;
	mt19937_init(&mt, (uint32_t)em->rand_seed * (uint32_t)(birth_frame + 1));

	int fps = fl->hdr.fps;
	float pixels_per_meter = fl->hdr.meter != 0.0f ? (float)fl->hdr.game_view_width / fl->hdr.meter : 0.0f;
	float t = em->particle_lifetime > 0 ? (float)age / em->particle_lifetime : 0;
	float gravity_y = emitter_gravity_displacement(age, em, fps);

	for (int i = 0; i < count; i++) {
		struct flat_emitter_particle p;
		emitter_interpolate_scale(t, em, &mt, p.scale);
		vec3 create_pos;
		emitter_calc_create_position(em, &mt, create_pos);
		emitter_interpolate_rotation(t, em, &mt, p.rot);
		vec2 dir;
		emitter_calc_direction(em, &mt, parent_vel, create_pos, dir);
		float move_rand_factor = 1.0f - (mt_next(&mt) - 0.5f) * em->move_rand * 0.01f;
		vec2 traj;
		emitter_calc_trajectory(age, em, dir, create_pos, track_pos,
				fps, pixels_per_meter, move_rand_factor, traj);

		if (em->align_to_direction && (dir[0] != 0 || dir[1] != 0))
			p.rot[2] += glm_deg(atan2f(dir[1], dir[0])) + 90;

		p.pos[0] = (create_pos[0] + traj[0]) * pixels_per_meter;
		p.pos[1] = (create_pos[1] + traj[1] + gravity_y) * pixels_per_meter;
		p.fade_alpha = fade_alpha;
		p.cg_lib_idx = lib_idx;

		fn(&p, ud);
	}
}

bool PE_ExistsFlatFile(struct string *filename)
{
	if (!filename || !filename->size || !filename->text[0])
		return false;
	return asset_exists_by_name(ASSET_FLAT, filename->text, NULL);
}

bool PE_SetPartsFlat(int parts_no, struct string *filename, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (parts_flat_load(parts, f, filename)) {
		f->needs_advance = true;
		parts_dirty(parts);
		return true;
	}
	return false;
}

struct string *PE_GetPartsFlatName(int no, int state)
{
	struct parts *p = parts_try_get(no);
	if (!parts_state_valid(--state) || !p || p->states[state].type != PARTS_FLAT)
		return string_ref(&EMPTY_STRING);
	struct parts_flat *f = &p->states[state].flat;
	return string_ref(f->name ? f->name : &EMPTY_STRING);
}

bool PE_IsPartsFlatStop(int no, int state)
{
	struct parts *p = parts_try_get(no);
	if (!parts_state_valid(--state) || !p || p->states[state].type != PARTS_FLAT)
		return false;
	struct parts_flat *f = &p->states[state].flat;
	return f->stopped || (f->root_state && f->root_state->stopped);
}

bool PE_IsPartsFlatEnd(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return true;
	// Ixseal waits for the opening segment of its location banner to finish
	// before it can signal the looping FLAT to play the closing segment.
	if (game_ixseal && f->root_state->has_looped)
		return true;
	return f->root_state->current_frame == f->end_frame;
}

int PE_GetPartsFlatCurrentFrameNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return 0;
	return f->root_state->current_frame + f->pending_seek_delta;
}

bool PE_BackPartsFlatBeginFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat)
		return false;
	f->pending_seek_delta = 0;
	parts_flat_layer_state_free(f->root_state);
	f->root_state = parts_flat_layer_state_new(f->flat->nr_timelines);
	f->needs_advance = true;
	f->elapsed = 0;
	parts_dirty(parts);
	return true;
}

bool PE_StepPartsFlatFinalFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return false;

	// Directly set to end frame.
	f->root_state->current_frame = f->end_frame;
	f->root_state->stopped = false;
	f->root_state->suppress_advance = true;
	f->pending_seek_delta = 0;
	f->needs_advance = true;
	f->elapsed = 0;
	parts_dirty(parts);
	return true;
}

bool PE_SetPartsFlatSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	parts_set_surface_area(parts, &f->common, x, y, w, h);
	return true;
}

bool PE_SetPartsFlatAndStop(int parts_no, struct string *filename, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	bool result = parts_flat_load(parts, f, filename);
	f->stopped = true;
	parts_dirty(parts);
	return result;
}

bool PE_StopPartsFlat(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat)
		return false;
	f->stopped = true;
	return true;
}

bool PE_StartPartsFlat(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat)
		return false;
	f->stopped = false;
	return true;
}

bool PE_GoFramePartsFlat(int parts_no, int frame_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return false;

	int current = f->root_state->current_frame;
	if (frame_no < current) {
		// Backward seek: directly set current_frame.
		f->root_state->current_frame = min(max(0, frame_no), f->end_frame);
		f->root_state->stopped = false;
		f->root_state->suppress_advance = true;
		f->root_state->has_looped = false;
		f->pending_seek_delta = 0;
		f->needs_advance = true;
	} else {
		// Forward seek: defer to next update.
		f->root_state->stopped = false;
		f->pending_seek_delta = frame_no - current;
	}
	f->elapsed = 0;
	parts_dirty(parts);
	return true;
}

int PE_GetPartsFlatEndFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	return f->flat ? f->end_frame : -1;
}

void PE_SetFlatFPSRate(int parts_no, float rate, int state)
{
	if (!parts_state_valid(--state)) return;
	parts_get_flat(parts_get(parts_no), state)->fps_rate = rate;
}

float PE_GetFlatFPSRate(int parts_no, int state)
{
	if (!parts_state_valid(--state)) return 1.0f;
	return parts_get_flat(parts_get(parts_no), state)->fps_rate;
}

int PE_GetFlatFPS(int parts_no, int state)
{
	if (!parts_state_valid(--state)) return 0;
	struct parts_flat *flat = parts_get_flat(parts_get(parts_no), state);
	return flat->flat ? flat->flat->hdr.fps : 0;
}

static int flat_find_script_label_in_timelines(struct flat_timeline *timelines,
		size_t nr_timelines, const char *label)
{
	for (size_t i = 0; i < nr_timelines; i++) {
		if (timelines[i].type != FLAT_TIMELINE_SCRIPT)
			continue;
		for (size_t j = 0; j < timelines[i].script.count; j++) {
			struct flat_script_key *key = &timelines[i].script.keys[j];
			if (key->text && !strcmp(key->text->text, label))
				return (int)key->frame_index;
		}
	}
	return -1;
}

int PE_GetFlatFrame(int parts_no, struct string *label_name, int state)
{
	if (!parts_state_valid(--state) || !label_name)
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return -1;
	struct flat *fl = parts->states[state].flat.flat;
	if (!fl)
		return -1;
	int frame = flat_find_script_label_in_timelines(
			fl->timelines, fl->nr_timelines, label_name->text);
	if (frame >= 0)
		return frame;
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		if (fl->libraries[i].type != FLAT_LIB_TIMELINE)
			continue;
		frame = flat_find_script_label_in_timelines(
				fl->libraries[i].timeline.timelines,
				fl->libraries[i].timeline.nr_timelines,
				label_name->text);
		if (frame >= 0)
			return frame;
	}
	return -1;
}

void PE_JumpFlatLabel(int parts_no, struct string *label_name, int state)
{
	int frame = PE_GetFlatFrame(parts_no, label_name, state);
	if (frame >= 0)
		PE_GoFramePartsFlat(parts_no, frame, state);
}

int PE_GetFlatBeginSeekFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return 0;
	return parts->states[state].flat.begin_seek_frame;
}

void PE_ChangeFlatCG(int parts_no, struct string *old_name, struct string *new_name, int state)
{
	if (!parts_state_valid(--state) || !old_name || !new_name)
		return;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return;
	struct parts_flat *f = &parts->states[state].flat;
	if (!f->flat || !f->textures)
		return;
	int lib_idx = parts_flat_find_library(f->flat, old_name->text);
	if (lib_idx < 0 || (size_t)lib_idx >= f->nr_libraries)
		return;
	if (f->flat->libraries[lib_idx].type != FLAT_LIB_CG)
		return;

	int unused_no;
	struct cg *cg = asset_cg_load_by_name(new_name->text, &unused_no);
	if (cg) {
		if (!f->asset || (f->owns_texture && f->owns_texture[lib_idx]))
			gfx_delete_texture(&f->textures[lib_idx]);
		gfx_init_texture_with_cg(&f->textures[lib_idx], cg);
		cg_free(cg);
		if (f->owns_texture)
			f->owns_texture[lib_idx] = true;
	} else {
		int new_lib_idx = parts_flat_find_library(f->flat, new_name->text);
		if (new_lib_idx < 0 || (size_t)new_lib_idx >= f->nr_libraries
				|| f->flat->libraries[new_lib_idx].type != FLAT_LIB_CG
				|| !f->asset || !f->asset->base_textures[new_lib_idx].handle) {
			return;
		}
		if (f->owns_texture && f->owns_texture[lib_idx]) {
			gfx_delete_texture(&f->textures[lib_idx]);
			f->owns_texture[lib_idx] = false;
		}
		f->textures[lib_idx] = f->asset->base_textures[new_lib_idx];
	}
	if (f->proxy_cg_names) {
		if (f->proxy_cg_names[lib_idx])
			free_string(f->proxy_cg_names[lib_idx]);
		f->proxy_cg_names[lib_idx] = string_dup(new_name);
	}
	parts_dirty(parts);
}

bool PE_SetPartsFlatThread(int parts_no, struct string *filename, int state)
{
	return PE_SetPartsFlat(parts_no, filename, state);
}

void PE_GetFlatSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	struct parts *parts = parts_try_get(parts_no);
	Rectangle area = (parts_state_valid(--state) && parts && parts->states[state].type == PARTS_FLAT)
			? parts->states[state].flat.common.surface_area : (Rectangle){0};
	*x = area.x;
	*y = area.y;
	*w = area.w;
	*h = area.h;
}

int PE_NumofFlatCG(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT || !parts->states[state].flat.flat)
		return 0;
	struct flat *fl = parts->states[state].flat.flat;
	int count = 0;
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		if (fl->libraries[i].type == FLAT_LIB_CG)
			count++;
	}
	return count;
}

struct string *PE_GetFlatCGName(int parts_no, int index, int state)
{
	if (!parts_state_valid(--state) || index < 0)
		return string_ref(&EMPTY_STRING);
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT || !parts->states[state].flat.flat)
		return string_ref(&EMPTY_STRING);
	struct flat *fl = parts->states[state].flat.flat;
	int cur = 0;
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		if (fl->libraries[i].type == FLAT_LIB_CG) {
			if (cur == index)
				return string_ref(fl->libraries[i].name ? fl->libraries[i].name : &EMPTY_STRING);
			cur++;
		}
	}
	return string_ref(&EMPTY_STRING);
}

struct string *PE_GetProxyFlatCGName(int parts_no, struct string *old_name, int state)
{
	if (!parts_state_valid(--state) || !old_name)
		return string_ref(&EMPTY_STRING);
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return string_ref(&EMPTY_STRING);
	struct parts_flat *f = &parts->states[state].flat;
	if (!f->flat || !f->proxy_cg_names)
		return string_ref(&EMPTY_STRING);
	int lib_idx = parts_flat_find_library(f->flat, old_name->text);
	if (lib_idx < 0 || (size_t)lib_idx >= f->nr_libraries || !f->proxy_cg_names[lib_idx])
		return string_ref(&EMPTY_STRING);
	return string_ref(f->proxy_cg_names[lib_idx]);
}

static void flat_restore_cg_index(struct parts_flat *f, int lib_idx)
{
	if (lib_idx < 0 || (size_t)lib_idx >= f->nr_libraries)
		return;
	if (f->asset) {
		if (f->owns_texture && f->owns_texture[lib_idx]) {
			gfx_delete_texture(&f->textures[lib_idx]);
			f->owns_texture[lib_idx] = false;
		}
		f->textures[lib_idx] = f->asset->base_textures[lib_idx];
	}
	if (f->proxy_cg_names && f->proxy_cg_names[lib_idx]) {
		free_string(f->proxy_cg_names[lib_idx]);
		f->proxy_cg_names[lib_idx] = NULL;
	}
}

void PE_ReturnChangeFlatCG(int parts_no, struct string *old_name, int state)
{
	if (!parts_state_valid(--state) || !old_name)
		return;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return;
	struct parts_flat *f = &parts->states[state].flat;
	if (!f->flat)
		return;
	int lib_idx = parts_flat_find_library(f->flat, old_name->text);
	if (lib_idx >= 0) {
		flat_restore_cg_index(f, lib_idx);
		parts_dirty(parts);
	}
}

void PE_ClearChangeFlatCG(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return;
	struct parts_flat *f = &parts->states[state].flat;
	if (!f->flat || !f->proxy_cg_names)
		return;
	for (size_t i = 0; i < f->nr_libraries; i++) {
		if (f->proxy_cg_names[i])
			flat_restore_cg_index(f, (int)i);
	}
	parts_dirty(parts);
}

int PE_NumofFlatSound(int parts_no, int state)
{
	(void)parts_no;
	(void)state;
	return 0;
}

struct string *PE_GetFlatSoundName(int parts_no, int index, int state)
{
	(void)parts_no;
	(void)index;
	(void)state;
	return string_ref(&EMPTY_STRING);
}

void PE_ChangeFlatSound(int parts_no, struct string *old_name, struct string *new_name, int state)
{
	(void)parts_no;
	(void)old_name;
	(void)new_name;
	(void)state;
}

struct string *PE_GetProxyFlatSound(int parts_no, struct string *old_name, int state)
{
	(void)parts_no;
	(void)old_name;
	(void)state;
	return string_ref(&EMPTY_STRING);
}

void PE_ReturnChangeFlatSound(int parts_no, struct string *old_name, int state)
{
	(void)parts_no;
	(void)old_name;
	(void)state;
}

void PE_ClearChangeFlatSound(int parts_no, int state)
{
	(void)parts_no;
	(void)state;
}

static void flat_set_enable_layer_recursive(struct parts_flat *f, struct flat_layer_state *s,
		struct flat_timeline *timelines, size_t nr_timelines,
		const int *timeline_lib_idx,
		const char *name, bool enable, int depth)
{
	struct flat *fl = f ? f->flat : NULL;
	if (!fl || !s || !timelines || depth > FLAT_MAX_ANCESTOR_DEPTH)
		return;
	size_t n = min(s->nr_timelines, nr_timelines);
	for (size_t i = 0; i < n; i++) {
		if (timelines[i].name && !strcmp(timelines[i].name->text, name))
			s->disabled_timelines[i] = !enable;
		if (timelines[i].type == FLAT_TIMELINE_GRAPHIC && s->children && s->children[i]
				&& timelines[i].library_name) {
			int lib_idx = timeline_lib_idx
					? timeline_lib_idx[i]
					: parts_flat_find_library(fl, timelines[i].library_name->text);
			if (lib_idx >= 0 && fl->libraries[lib_idx].type == FLAT_LIB_TIMELINE) {
				const int *child_lib_idx = (f->asset && f->asset->lib_timeline_lib_idx
						&& (size_t)lib_idx < f->asset->nr_libraries)
						? f->asset->lib_timeline_lib_idx[lib_idx] : NULL;
				flat_set_enable_layer_recursive(f, s->children[i],
						fl->libraries[lib_idx].timeline.timelines,
						fl->libraries[lib_idx].timeline.nr_timelines,
						child_lib_idx, name, enable, depth + 1);
			}
		}
	}
}

void PE_SetFlatEnableLayer(int parts_no, struct string *name, bool enable, int state)
{
	if (!parts_state_valid(--state) || !name)
		return;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return;
	struct parts_flat *f = &parts->states[state].flat;
	if (!f->flat || !f->root_state)
		return;
	flat_set_enable_layer_recursive(f, f->root_state, f->flat->timelines,
			f->flat->nr_timelines,
			f->asset ? f->asset->root_timeline_lib_idx : NULL,
			name->text, enable, 0);
	parts_dirty(parts);
}

static bool flat_is_enable_layer_recursive(struct parts_flat *f, struct flat_layer_state *s,
		struct flat_timeline *timelines, size_t nr_timelines,
		const int *timeline_lib_idx,
		const char *name, bool *found, int depth)
{
	struct flat *fl = f ? f->flat : NULL;
	if (!fl || !s || !timelines || depth > FLAT_MAX_ANCESTOR_DEPTH)
		return true;
	size_t n = min(s->nr_timelines, nr_timelines);
	for (size_t i = 0; i < n; i++) {
		if (timelines[i].name && !strcmp(timelines[i].name->text, name)) {
			*found = true;
			return !s->disabled_timelines[i];
		}
		if (timelines[i].type == FLAT_TIMELINE_GRAPHIC && s->children && s->children[i]
				&& timelines[i].library_name) {
			int lib_idx = timeline_lib_idx
					? timeline_lib_idx[i]
					: parts_flat_find_library(fl, timelines[i].library_name->text);
			if (lib_idx >= 0 && fl->libraries[lib_idx].type == FLAT_LIB_TIMELINE) {
				const int *child_lib_idx = (f->asset && f->asset->lib_timeline_lib_idx
						&& (size_t)lib_idx < f->asset->nr_libraries)
						? f->asset->lib_timeline_lib_idx[lib_idx] : NULL;
				bool res = flat_is_enable_layer_recursive(f, s->children[i],
						fl->libraries[lib_idx].timeline.timelines,
						fl->libraries[lib_idx].timeline.nr_timelines,
						child_lib_idx, name, found, depth + 1);
				if (*found)
					return res;
			}
		}
	}
	return true;
}

bool PE_IsFlatEnableLayer(int parts_no, struct string *name, int state)
{
	if (!parts_state_valid(--state) || !name)
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT)
		return false;
	struct parts_flat *f = &parts->states[state].flat;
	if (!f->flat || !f->root_state)
		return false;
	bool found = false;
	return flat_is_enable_layer_recursive(f, f->root_state, f->flat->timelines,
			f->flat->nr_timelines,
			f->asset ? f->asset->root_timeline_lib_idx : NULL,
			name->text, &found, 0);
}

int PE_GetFlatGameViewWidth(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT || !parts->states[state].flat.flat)
		return 0;
	return parts->states[state].flat.flat->hdr.game_view_width;
}

int PE_GetFlatGameViewHeight(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT || !parts->states[state].flat.flat)
		return 0;
	return parts->states[state].flat.flat->hdr.game_view_height;
}

struct string *PE_GetPartsFlatDataInfo(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return string_ref(&EMPTY_STRING);
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_FLAT || !parts->states[state].flat.flat)
		return string_ref(&EMPTY_STRING);
	struct parts_flat *f = &parts->states[state].flat;
	return string_ref(f->name ? f->name : &EMPTY_STRING);
}
