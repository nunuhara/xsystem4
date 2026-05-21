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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4.h"

#include "3d_internal.h"

// .mpr parser.

static int resolve_mesh_index(struct model *model, const char *name)
{
	for (int i = 0; i < model->nr_meshes; i++) {
		if (!strcmp(model->meshes[i].name, name))
			return i;
	}
	return -1;
}

static void append_float_key(struct mpr_track_set *ts, int frame, float v)
{
	ts->mul_alpha = xrealloc(ts->mul_alpha, (ts->nr_mul_alpha + 1) * sizeof(struct mpr_float_key));
	ts->mul_alpha[ts->nr_mul_alpha].frame = frame;
	ts->mul_alpha[ts->nr_mul_alpha].v = v;
	ts->nr_mul_alpha++;
}

static void append_vec3_key(struct mpr_vec3_key **arr, int *nr, int frame, float r, float g, float b)
{
	*arr = xrealloc(*arr, (*nr + 1) * sizeof(struct mpr_vec3_key));
	(*arr)[*nr].frame = frame;
	(*arr)[*nr].v[0] = r;
	(*arr)[*nr].v[1] = g;
	(*arr)[*nr].v[2] = b;
	(*nr)++;
}

static void append_int_key(struct mpr_int_key **arr, int *nr, int frame, int v)
{
	*arr = xrealloc(*arr, (*nr + 1) * sizeof(struct mpr_int_key));
	(*arr)[*nr].frame = frame;
	(*arr)[*nr].v = v;
	(*nr)++;
}

// All mpr_*_key structs have `int frame` as their first field.
static int cmp_key_by_frame(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

static void sort_track_set(struct mpr_track_set *ts)
{
	if (ts->nr_mul_alpha)
		qsort(ts->mul_alpha, ts->nr_mul_alpha, sizeof(struct mpr_float_key), cmp_key_by_frame);
	if (ts->nr_mul_diffuse)
		qsort(ts->mul_diffuse, ts->nr_mul_diffuse, sizeof(struct mpr_vec3_key), cmp_key_by_frame);
	if (ts->nr_add_ambient)
		qsort(ts->add_ambient, ts->nr_add_ambient, sizeof(struct mpr_vec3_key), cmp_key_by_frame);
	if (ts->nr_texture_anime)
		qsort(ts->texture_anime, ts->nr_texture_anime, sizeof(struct mpr_int_key), cmp_key_by_frame);
}

struct mpr *mpr_load(uint8_t *data, size_t size, struct model *model)
{
	struct mpr *mpr = xcalloc(1, sizeof(struct mpr));
	mpr->nr_meshes = model->nr_meshes;
	mpr->mesh_tracks = xcalloc(model->nr_meshes, sizeof(struct mpr_track_set *));
	struct mpr_track_set *current = NULL;  // current Mesh-scope track set

	while (size > 0) {
		char line[200];
		uint8_t *nl = memchr(data, '\n', size);
		if (nl)
			nl++;
		else
			nl = data + size;
		int copylen = min(nl - data, sizeof(line) - 1);
		memcpy(line, (char *)data, copylen);
		line[copylen] = '\0';
		size -= nl - data;
		data = nl;

		char s[200];
		int frame, ti;
		float a, r, g, b;
		if (sscanf(line, "Mesh = \"%[^\"]\"", s) == 1) {
			int mi = resolve_mesh_index(model, s);
			if (mi < 0) {
				WARNING("mpr: unknown mesh \"%s\"", s);
				current = NULL;
			} else {
				if (!mpr->mesh_tracks[mi])
					mpr->mesh_tracks[mi] = xcalloc(1, sizeof(struct mpr_track_set));
				current = mpr->mesh_tracks[mi];
			}
		} else if (sscanf(line, "MeshMulAlpha = ( %d , %f )", &frame, &a) == 2) {
			if (current)
				append_float_key(current, frame, a);
			else
				WARNING("mpr: MeshMulAlpha outside Mesh scope");
		} else if (sscanf(line, "MeshMulDiffuse = ( %d , %f , %f , %f )", &frame, &r, &g, &b) == 4) {
			if (current)
				append_vec3_key(&current->mul_diffuse, &current->nr_mul_diffuse, frame, r, g, b);
			else
				WARNING("mpr: MeshMulDiffuse outside Mesh scope");
		} else if (sscanf(line, "MeshAddAmbient = ( %d , %f , %f , %f )", &frame, &r, &g, &b) == 4) {
			if (current)
				append_vec3_key(&current->add_ambient, &current->nr_add_ambient, frame, r, g, b);
			else
				WARNING("mpr: MeshAddAmbient outside Mesh scope");
		} else if (sscanf(line, "MeshTextureAnime = ( %d , %d )", &frame, &ti) == 2) {
			if (current)
				append_int_key(&current->texture_anime, &current->nr_texture_anime, frame, ti);
			else
				WARNING("mpr: MeshTextureAnime outside Mesh scope");
		} else if (sscanf(line, "ObjectMulAlpha = ( %d , %f )", &frame, &a) == 2) {
			append_float_key(&mpr->object, frame, a);
		} else if (sscanf(line, "ObjectMulDiffuse = ( %d , %f , %f , %f )", &frame, &r, &g, &b) == 4) {
			append_vec3_key(&mpr->object.mul_diffuse, &mpr->object.nr_mul_diffuse, frame, r, g, b);
		} else if (sscanf(line, "ObjectAddAmbient = ( %d , %f , %f , %f )", &frame, &r, &g, &b) == 4) {
			append_vec3_key(&mpr->object.add_ambient, &mpr->object.nr_add_ambient, frame, r, g, b);
		} else if (strchr(line, '=')) {
			WARNING("mpr: unknown field: %s", line);
		}
	}

	for (int i = 0; i < mpr->nr_meshes; i++) {
		if (!mpr->mesh_tracks[i])
			continue;
		sort_track_set(mpr->mesh_tracks[i]);
		if (mpr->mesh_tracks[i]->nr_mul_alpha > 0)
			mpr->has_mesh_alpha = true;
	}
	sort_track_set(&mpr->object);
	return mpr;
}

static void destroy_track_set(struct mpr_track_set *ts)
{
	free(ts->mul_alpha);
	free(ts->mul_diffuse);
	free(ts->add_ambient);
	free(ts->texture_anime);
}

void mpr_free(struct mpr *mpr)
{
	if (!mpr)
		return;
	for (int i = 0; i < mpr->nr_meshes; i++) {
		if (mpr->mesh_tracks[i]) {
			destroy_track_set(mpr->mesh_tracks[i]);
			free(mpr->mesh_tracks[i]);
		}
	}
	free(mpr->mesh_tracks);
	destroy_track_set(&mpr->object);
	free(mpr);
}

// Keyframe evaluation.

// Returns the largest index i with keys[i].frame <= frame.
// Returns -1 if frame < keys[0].frame.
#define DEFINE_FIND_KEY(NAME, KEY_TYPE) \
	static int NAME(const KEY_TYPE *keys, int nr, float frame) \
	{ \
		if (frame < (float)keys[0].frame) \
			return -1; \
		int lo = 0, hi = nr - 1; \
		while (lo < hi) { \
			int mid = (lo + hi + 1) / 2; \
			if ((float)keys[mid].frame <= frame) \
				lo = mid; \
			else \
				hi = mid - 1; \
		} \
		return lo; \
	}

DEFINE_FIND_KEY(find_float_key, struct mpr_float_key)
DEFINE_FIND_KEY(find_vec3_key,  struct mpr_vec3_key)
DEFINE_FIND_KEY(find_int_key,   struct mpr_int_key)

static float eval_float(const struct mpr_float_key *keys, int nr, float frame, float fallback)
{
	if (nr == 0)
		return fallback;
	int i = find_float_key(keys, nr, frame);
	if (i < 0)
		return keys[0].v;
	if (i >= nr - 1)
		return keys[nr - 1].v;
	float t = (frame - keys[i].frame) / (float)(keys[i + 1].frame - keys[i].frame);
	return glm_lerp(keys[i].v, keys[i + 1].v, t);
}

static void eval_vec3(const struct mpr_vec3_key *keys, int nr, float frame, const vec3 fallback, vec3 out)
{
	if (nr == 0) {
		glm_vec3_copy((float *)fallback, out);
		return;
	}
	int i = find_vec3_key(keys, nr, frame);
	if (i < 0) {
		glm_vec3_copy((float *)keys[0].v, out);
		return;
	}
	if (i >= nr - 1) {
		glm_vec3_copy((float *)keys[nr - 1].v, out);
		return;
	}
	float t = (frame - keys[i].frame) / (float)(keys[i + 1].frame - keys[i].frame);
	glm_vec3_lerp((float *)keys[i].v, (float *)keys[i + 1].v, t, out);
}

static int eval_int(const struct mpr_int_key *keys, int nr, float frame, int fallback)
{
	if (nr == 0)
		return fallback;
	int i = find_int_key(keys, nr, frame);
	if (i < 0)
		return keys[0].v;
	return keys[i].v;  // step
}

// Combine inst defaults with .mpr Object-scope keyframes.
void mpr_evaluate_object(const struct mpr *mpr, float frame,
		const struct RE_instance *inst, struct mpr_modulation *out)
{
	out->alpha = inst->alpha;
	glm_vec3_copy((float *)inst->ambient, out->ambient);
	glm_vec3_one(out->diffuse);
	if (!mpr)
		return;
	out->alpha *= eval_float(mpr->object.mul_alpha, mpr->object.nr_mul_alpha, frame, 1.f);
	vec3 add;
	eval_vec3(mpr->object.add_ambient, mpr->object.nr_add_ambient, frame, (vec3){0,0,0}, add);
	glm_vec3_add(out->ambient, add, out->ambient);
	eval_vec3(mpr->object.mul_diffuse, mpr->object.nr_mul_diffuse, frame, (vec3){1,1,1}, out->diffuse);
}

// Combine the object-scope values with .mpr Mesh-scope keyframes for one mesh.
void mpr_evaluate_mesh(const struct mpr_track_set *mt, float frame,
		const struct mpr_modulation *obj, struct mpr_modulation *out)
{
	*out = *obj;
	if (!mt)
		return;
	out->alpha *= eval_float(mt->mul_alpha, mt->nr_mul_alpha, frame, 1.f);
	vec3 add;
	eval_vec3(mt->add_ambient, mt->nr_add_ambient, frame, (vec3){0,0,0}, add);
	glm_vec3_add(out->ambient, add, out->ambient);
	vec3 mul;
	eval_vec3(mt->mul_diffuse, mt->nr_mul_diffuse, frame, (vec3){1,1,1}, mul);
	glm_vec3_mul(out->diffuse, mul, out->diffuse);
}

// MeshTextureAnime writes to the shared material slot, so all meshes
// referencing the same material pick up the animation even if .mpr only names
// one of them. Caller supplies the output buffer sized to model->nr_materials.
void mpr_build_mat_tex_index(const struct model *model, const struct RE_instance *inst,
		const struct mpr *mpr, float frame, int *mat_tex_index)
{
	for (int m = 0; m < model->nr_materials; m++)
		mat_tex_index[m] = inst->texture_animation_index;
	if (!mpr)
		return;
	for (int i = 0; i < mpr->nr_meshes; i++) {
		const struct mpr_track_set *t = mpr->mesh_tracks[i];
		if (!t || t->nr_texture_anime == 0)
			continue;
		int mat = model->meshes[i].material;
		mat_tex_index[mat] = eval_int(t->texture_anime, t->nr_texture_anime, frame, mat_tex_index[mat]);
	}
}
