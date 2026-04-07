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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/flat.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "parts.h"
#include "parts_internal.h"
#include "xsystem4.h"

static struct flat_layer_state *flat_layer_state_new(size_t nr_timelines)
{
	struct flat_layer_state *s = xcalloc(1, sizeof(struct flat_layer_state));
	s->suppress_advance = true;
	s->jump_target = -1;
	s->nr_timelines = nr_timelines;
	s->last_script_keys = xmalloc(nr_timelines * sizeof(int));
	for (size_t i = 0; i < nr_timelines; i++)
		s->last_script_keys[i] = -2;  // uninitialized
	s->children = xcalloc(nr_timelines, sizeof(struct flat_layer_state *));
	return s;
}

static void flat_layer_state_free(struct flat_layer_state *s)
{
	if (!s)
		return;
	for (size_t i = 0; i < s->nr_timelines; i++)
		flat_layer_state_free(s->children[i]);
	free(s->children);
	free(s->last_script_keys);
	free(s);
}

void parts_flat_free(struct parts_flat *f)
{
	if (f->flat)
		flat_free(f->flat);
	if (f->name)
		free_string(f->name);
	flat_layer_state_free(f->root_state);
	if (f->textures) {
		for (size_t i = 0; i < f->nr_textures; i++)
			gfx_delete_texture(&f->textures[i]);
		free(f->textures);
	}
}

int parts_flat_find_library(struct flat *fl, const char *name)
{
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		if (strcmp(fl->libraries[i].name->text, name) == 0)
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
			state->jump_target = key->jump_frame;
		} else if (key->is_stop) {
			state->stopped = true;
		}
	}
}

// Advance children regardless of this layer's stopped state.
static bool flat_advance_children(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines);

// Advance a layer by one tick. Returns true if any frame changed.
static bool flat_advance_layer(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines)
{
	int max_end_frame = flat_compute_max_frame(timelines, nr_timelines) - 1;
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

	bool child_changed = flat_advance_children(fl, state, timelines, nr_timelines);
	return state->current_frame != prev_frame || child_changed;
}

static bool flat_advance_children(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines)
{
	bool changed = false;
	for (size_t i = 0; i < nr_timelines; i++) {
		if (timelines[i].type != FLAT_TIMELINE_GRAPHIC)
			continue;

		// Destroy child states when the parent frame leaves the active range.
		int local = state->current_frame - timelines[i].begin_frame;
		if (local < 0 || local >= timelines[i].frame_count) {
			if (state->children[i]) {
				flat_layer_state_free(state->children[i]);
				state->children[i] = NULL;
			}
			continue;
		}

		int lib_idx = parts_flat_find_library(fl, timelines[i].library_name->text);
		if (lib_idx < 0)
			continue;
		struct flat_library *lib = &fl->libraries[lib_idx];
		if (lib->type != FLAT_LIB_TIMELINE)
			continue;

		if (!state->children[i])
			state->children[i] = flat_layer_state_new(lib->timeline.nr_timelines);
		if (flat_advance_layer(fl, state->children[i],
				lib->timeline.timelines, lib->timeline.nr_timelines)) {
			changed = true;
		}
	}
	return changed;
}

bool parts_flat_load(struct parts *parts, struct parts_flat *f, struct string *filename)
{
	if (f->flat) {
		parts_flat_free(f);
		memset(f, 0, sizeof(struct parts_flat));
	}

	if (!filename)
		return false;

	struct archive_data *dfile = asset_get_by_name(ASSET_FLAT, filename->text, NULL);
	if (!dfile)
		return false;

	int error;
	f->flat = flat_open(dfile->data, dfile->size, &error);
	if (!f->flat) {
		archive_free_data(dfile);
		return false;
	}

	f->name = string_dup(filename);
	f->stopped = false;
	f->elapsed = 0;
	f->end_frame = flat_compute_max_frame(
			f->flat->timelines, f->flat->nr_timelines) - 1;
	f->root_state = flat_layer_state_new(f->flat->nr_timelines);

	// Load textures for CG libraries.
	f->nr_textures = f->flat->nr_libraries;
	f->textures = xcalloc(f->nr_textures, sizeof(Texture));
	for (size_t i = 0; i < f->flat->nr_libraries; i++) {
		struct flat_library *lib = &f->flat->libraries[i];
		if (lib->type != FLAT_LIB_CG)
			continue;
		struct cg *cg = cg_load_buffer((uint8_t *)lib->cg.data, lib->cg.size);
		if (!cg) {
			WARNING("flat: failed to load CG for library '%s'", lib->name->text);
			continue;
		}
		gfx_init_texture_with_cg(&f->textures[i], cg);
		cg_free(cg);
	}

	// TODO: The original engine performs per-frame hit testing against
	// the flat's visible sprites (with full transform chain), rather than
	// using a static hitbox. For now, estimate dimensions from CG textures.
	int w = f->flat->hdr.width;
	int h = f->flat->hdr.height;
	if (w <= 0 || h <= 0) {
		w = 0;
		h = 0;
		for (size_t i = 0; i < f->nr_textures; i++) {
			if (f->textures[i].w > w)
				w = f->textures[i].w;
			if (f->textures[i].h > h)
				h = f->textures[i].h;
		}
	}
	if (w > 0 && h > 0)
		parts_set_dims(parts, &f->common, w, h);

	// f->flat contains pointers into dfile->data, but it should be safe to
	// free now since we've loaded all CG data we need.
	archive_free_data(dfile);
	return true;
}

bool parts_flat_update(struct parts_flat *f, int passed_time)
{
	if (!f->flat || f->flat->hdr.fps <= 0)
		return false;

	// Consume pending forward seek (from GoFramePartsFlat).
	if (f->pending_seek_delta > 0) {
		for (int i = 0; i < f->pending_seek_delta; i++) {
			flat_advance_layer(f->flat, f->root_state,
					f->flat->timelines, f->flat->nr_timelines);
		}
		f->pending_seek_delta = 0;
		f->elapsed = 0;
		return true;
	}

	// One advance after load / seek.
	if (f->needs_advance) {
		f->needs_advance = false;
		bool changed = flat_advance_layer(f->flat, f->root_state,
				f->flat->timelines, f->flat->nr_timelines);
		f->elapsed = 0;
		return changed;
	}

	if (f->stopped)
		return false;

	f->elapsed += passed_time;
	int delta = (int)((int64_t)f->elapsed * f->flat->hdr.fps / 1000);
	if (!delta)
		return false;
	f->elapsed -= (unsigned)((int64_t)delta * 1000 / f->flat->hdr.fps);

	bool any_changed = false;
	for (int i = 0; i < delta; i++) {
		if (flat_advance_layer(f->flat, f->root_state,
				f->flat->timelines, f->flat->nr_timelines))
			any_changed = true;
	}
	return any_changed;
}

bool PE_ExistsFlatFile(struct string *filename)
{
	if (!filename)
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

bool PE_IsPartsFlatEnd(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
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
	flat_layer_state_free(f->root_state);
	f->root_state = flat_layer_state_new(f->flat->nr_timelines);
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
		f->pending_seek_delta = 0;
		f->needs_advance = true;
	} else {
		// Forward seek: defer to next update.
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
