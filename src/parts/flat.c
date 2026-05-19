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
#include "xsystem4.h"

enum emitter_direction_type {
	EMITTER_DIRECTION_RANDOM         = 0,
	EMITTER_DIRECTION_FIXED          = 1,
	EMITTER_DIRECTION_PARENT         = 2,
	EMITTER_DIRECTION_PARENT_REVERSE = 3,
	EMITTER_DIRECTION_DISK           = 4,
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
		for (size_t i = 0; i < f->nr_libraries; i++)
			gfx_delete_texture(&f->textures[i]);
		free(f->textures);
	}
	if (f->stop_motion_frames) {
		for (size_t i = 0; i < f->nr_libraries; i++)
			free(f->stop_motion_frames[i].lib_indices);
		free(f->stop_motion_frames);
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

// Build the CG list for a STOP_MOTION library: frame 0 is the base
// library named in `sm->library_name`; subsequent frames are numbered
// variants `${stem}_NN.${ext}`, terminated by the first missing entry.
static void build_stop_motion_frames(struct parts_flat *f, int sm_lib_idx)
{
	struct flat_stop_motion *sm = &f->flat->libraries[sm_lib_idx].stop_motion;
	const char *base = sm->library_name->text;

	int base_idx = parts_flat_find_library(f->flat, base);
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
		int idx = parts_flat_find_library(f->flat, name_buf);
		if (idx < 0)
			break;
		indices = xrealloc(indices, (count + 1) * sizeof(int));
		indices[count++] = idx;
	}

	f->stop_motion_frames[sm_lib_idx].lib_indices = indices;
	f->stop_motion_frames[sm_lib_idx].count = count;
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
	f->nr_libraries = f->flat->nr_libraries;
	f->textures = xcalloc(f->nr_libraries, sizeof(Texture));
	f->stop_motion_frames = xcalloc(f->nr_libraries,
			sizeof(struct flat_stop_motion_frames));
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
	for (size_t i = 0; i < f->flat->nr_libraries; i++) {
		if (f->flat->libraries[i].type == FLAT_LIB_STOP_MOTION)
			build_stop_motion_frames(f, (int)i);
	}
	for (size_t i = 0; i < f->flat->nr_libraries; i++) {
		if (f->flat->libraries[i].type != FLAT_LIB_EMITTER)
			continue;
		struct flat_emitter *em = &f->flat->libraries[i].emitter;
		if (em->end_pos_type != 0)
			WARNING("flat: emitter '%s': unsupported end_pos_type %d",
					display_sjis0(em->library_name->text), em->end_pos_type);
		if (em->pos_track_mode == 0)
			WARNING("flat: emitter '%s': unsupported pos_track_mode %d",
					display_sjis0(em->library_name->text), em->pos_track_mode);
	}

	// TODO: The original engine performs per-frame hit testing against
	// the flat's visible sprites (with full transform chain), rather than
	// using a static hitbox. For now, estimate dimensions from CG textures.
	int w = f->flat->hdr.width;
	int h = f->flat->hdr.height;
	if (w <= 0 || h <= 0) {
		w = 0;
		h = 0;
		for (size_t i = 0; i < f->nr_libraries; i++) {
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
		struct mt19937 *mt, vec2 out)
{
	switch (em->create_pos_type) {
	case EMITTER_CREATE_POS_RECT:
		out[0] = mt_next_signed(mt) * em->create_pos_length;
		out[1] = mt_next_signed(mt) * em->create_pos_length2;
		return;
	case EMITTER_CREATE_POS_SPHERE: {
		vec3 v;
		random_unit_vec3(mt, v);
		out[0] = v[0];
		out[1] = v[1];
		break;
	}
	case EMITTER_CREATE_POS_CIRCLE:
		out[0] = mt_next_signed(mt);
		out[1] = mt_next_signed(mt);
		glm_vec2_normalize(out);
		break;
	default:
		glm_vec2_zero(out);
		break;
	}
	float dist = mt_next(mt) * (em->create_pos_length - em->create_pos_length2)
			+ em->create_pos_length2;
	glm_vec2_scale(out, dist, out);
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
		struct mt19937 *mt, vec2 parent_vel, vec2 out)
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
	case EMITTER_DIRECTION_FIXED:
	default:
		glm_vec3_copy((vec3){ em->direction_x, em->direction_y, em->direction_z }, dir);
		break;
	}
	randomize_direction_within_cone(dir, em->direction_angle, mt);
	// Project to 2D by dropping Z without renormalizing in 2D, so
	// directions tilted out of the XY plane translate to slower
	// on-screen motion.
	glm_vec3_normalize(dir);
	out[0] = dir[0];
	out[1] = dir[1];
}

static void emitter_calc_trajectory(int age, struct flat_emitter *em,
		vec2 dir, int fps, float move_rand_factor, vec2 out)
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

	int cg_lib_idx = parts_flat_find_library(f->flat, em->library_name->text);
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
		const struct flat_key_data_graphic *keys,
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
	int lib_idx = parts_flat_find_library(fl, em->library_name->text);
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
		const struct flat_key_data_graphic *cur = &keys[birth_frame];
		if (birth_frame > 0) {
			const struct flat_key_data_graphic *prev = &keys[birth_frame - 1];
			parent_vel[0] = cur->pos_x - prev->pos_x;
			parent_vel[1] = cur->pos_y - prev->pos_y;
		} else if (birth_frame + 1 < frame_count) {
			const struct flat_key_data_graphic *next = &keys[birth_frame + 1];
			parent_vel[0] = next->pos_x - cur->pos_x;
			parent_vel[1] = next->pos_y - cur->pos_y;
		}
	}

	struct mt19937 mt;
	mt19937_init(&mt, em->rand_seed * (birth_frame + 1));

	int fps = fl->hdr.fps;
	float pixels_per_meter = (float)fl->hdr.game_view_width / fl->hdr.meter;
	float t = em->particle_lifetime > 0 ? (float)age / em->particle_lifetime : 0;
	float gravity_y = emitter_gravity_displacement(age, em, fps);

	for (int i = 0; i < count; i++) {
		struct flat_emitter_particle p;
		emitter_interpolate_scale(t, em, &mt, p.scale);
		vec2 create_pos;
		emitter_calc_create_position(em, &mt, create_pos);
		emitter_interpolate_rotation(t, em, &mt, p.rot);
		vec2 dir;
		emitter_calc_direction(em, &mt, parent_vel, dir);
		float move_rand_factor = 1.0f - (mt_next(&mt) - 0.5f) * em->move_rand * 0.01f;
		vec2 traj;
		emitter_calc_trajectory(age, em, dir, fps, move_rand_factor, traj);

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
