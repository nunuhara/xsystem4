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

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/ex.h"
#include "system4/string.h"

#include "3d_internal.h"

// .mpr parser.

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

static struct mpr_track_set *ensure_mesh_track_at(struct mpr *mpr, int mi)
{
	if (mi < 0 || mi >= mpr->nr_meshes)
		return NULL;
	if (!mpr->mesh_tracks[mi])
		mpr->mesh_tracks[mi] = xcalloc(1, sizeof(struct mpr_track_set));
	return mpr->mesh_tracks[mi];
}

static bool mpr_finalize(struct mpr *mpr)
{
	bool has_any = mpr->object.nr_mul_alpha || mpr->object.nr_mul_diffuse ||
	               mpr->object.nr_add_ambient || mpr->object.nr_texture_anime;
	for (int i = 0; i < mpr->nr_meshes; i++) {
		if (!mpr->mesh_tracks[i])
			continue;
		sort_track_set(mpr->mesh_tracks[i]);
		if (mpr->mesh_tracks[i]->nr_mul_alpha > 0)
			mpr->has_mesh_alpha = true;
		has_any = true;
	}
	sort_track_set(&mpr->object);
	return has_any;
}

struct mpr *mpr_load(uint8_t *data, size_t size, struct model *model)
{
	struct mpr *mpr = xcalloc(1, sizeof(struct mpr));
	mpr->nr_meshes = model->nr_meshes;
	mpr->mesh_tracks = xcalloc(model->nr_meshes, sizeof(struct mpr_track_set *));
	int *matched = model->nr_meshes > 0 ? xmalloc(model->nr_meshes * sizeof(int)) : NULL;
	int nr_matched = 0;

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
			nr_matched = 0;
			for (int i = 0; i < model->nr_meshes; i++) {
				if (model->meshes[i].name && !strcmp(model->meshes[i].name, s)) {
					ensure_mesh_track_at(mpr, i);
					matched[nr_matched++] = i;
				}
			}
			if (nr_matched == 0)
				WARNING("mpr: unknown mesh \"%s\"", s);
		} else if (sscanf(line, "MeshMulAlpha = ( %d , %f )", &frame, &a) == 2) {
			if (nr_matched > 0) {
				for (int k = 0; k < nr_matched; k++)
					append_float_key(mpr->mesh_tracks[matched[k]], frame, a);
			} else {
				WARNING("mpr: MeshMulAlpha outside Mesh scope");
			}
		} else if (sscanf(line, "MeshMulDiffuse = ( %d , %f , %f , %f )", &frame, &r, &g, &b) == 4) {
			if (nr_matched > 0) {
				for (int k = 0; k < nr_matched; k++) {
					struct mpr_track_set *ts = mpr->mesh_tracks[matched[k]];
					append_vec3_key(&ts->mul_diffuse, &ts->nr_mul_diffuse, frame, r, g, b);
				}
			} else {
				WARNING("mpr: MeshMulDiffuse outside Mesh scope");
			}
		} else if (sscanf(line, "MeshAddAmbient = ( %d , %f , %f , %f )", &frame, &r, &g, &b) == 4) {
			if (nr_matched > 0) {
				for (int k = 0; k < nr_matched; k++) {
					struct mpr_track_set *ts = mpr->mesh_tracks[matched[k]];
					append_vec3_key(&ts->add_ambient, &ts->nr_add_ambient, frame, r, g, b);
				}
			} else {
				WARNING("mpr: MeshAddAmbient outside Mesh scope");
			}
		} else if (sscanf(line, "MeshTextureAnime = ( %d , %d )", &frame, &ti) == 2) {
			if (nr_matched > 0) {
				for (int k = 0; k < nr_matched; k++) {
					struct mpr_track_set *ts = mpr->mesh_tracks[matched[k]];
					append_int_key(&ts->texture_anime, &ts->nr_texture_anime, frame, ti);
				}
			} else {
				WARNING("mpr: MeshTextureAnime outside Mesh scope");
			}
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

	free(matched);
	mpr_finalize(mpr);
	return mpr;
}

static void txtex_collect_mesh_vec3(struct mpr *mpr, struct model *model, struct ex_tree *frame_node,
                                    const char *utf8_key, bool is_ambient, int frame)
{
	struct ex_tree *sub = RE_ex_child(frame_node, utf8_key);
	if (!sub || sub->is_leaf)
		return;
	for (unsigned i = 0; i < sub->nr_children; i++) {
		struct ex_tree *c = &sub->children[i];
		if (!c->is_leaf || !c->name)
			continue;
		struct ex_value *val = &c->leaf.value;
		if (val->type != EX_LIST || val->list->nr_items < 3)
			continue;
		vec3 v;
		for (int k = 0; k < 3; k++) {
			struct ex_value *ev = ex_list_get(val->list, k);
			v[k] = ev && ev->type == EX_FLOAT ? ev->f : (ev && ev->type == EX_INT ? (float)ev->i : 0.0f);
		}
		for (int mi = 0; mi < model->nr_meshes; mi++) {
			if (!model->meshes[mi].name || strcmp(model->meshes[mi].name, c->name->text))
				continue;
			struct mpr_track_set *ts = ensure_mesh_track_at(mpr, mi);
			if (is_ambient)
				append_vec3_key(&ts->add_ambient, &ts->nr_add_ambient, frame, v[0], v[1], v[2]);
			else
				append_vec3_key(&ts->mul_diffuse, &ts->nr_mul_diffuse, frame, v[0], v[1], v[2]);
		}
	}
}

struct mpr *mpr_load_txtex(uint8_t *data, size_t size, struct model *model, const char *motion_name)
{
	if (!RE_is_exf(data, size) || !motion_name)
		return NULL;
	struct ex *ex = ex_read(data, size);
	if (!ex)
		return NULL;
	struct ex_value *all_v = RE_ex_block(ex, "モーション追加データ");
	if (!all_v || all_v->type != EX_TREE || all_v->tree->is_leaf) {
		ex_free(ex);
		return NULL;
	}
	struct ex_tree *mot_node = NULL;
	for (unsigned i = 0; i < all_v->tree->nr_children; i++) {
		struct ex_tree *c = &all_v->tree->children[i];
		if (!c->is_leaf && c->name && !strcasecmp(c->name->text, motion_name)) {
			mot_node = c;
			break;
		}
	}
	if (!mot_node) {
		ex_free(ex);
		return NULL;
	}
	struct mpr *mpr = xcalloc(1, sizeof(struct mpr));
	mpr->nr_meshes = model->nr_meshes;
	mpr->mesh_tracks = xcalloc(model->nr_meshes, sizeof(struct mpr_track_set *));

	for (unsigned i = 0; i < mot_node->nr_children; i++) {
		struct ex_tree *fn = &mot_node->children[i];
		if (fn->is_leaf || !fn->name || !isdigit((unsigned char)fn->name->text[0]))
			continue;
		int frame = atoi(fn->name->text);
		float fv;
		vec3 v3;
		if (RE_ex_float(fn, "オブジェクトアルファ乗算カラー", &fv))
			append_float_key(&mpr->object, frame, fv);
		if (RE_ex_vec3(fn, "オブジェクトディフューズ乗算カラー", v3))
			append_vec3_key(&mpr->object.mul_diffuse, &mpr->object.nr_mul_diffuse, frame, v3[0], v3[1], v3[2]);
		if (RE_ex_vec3(fn, "オブジェクトアンビエント加算カラー", v3))
			append_vec3_key(&mpr->object.add_ambient, &mpr->object.nr_add_ambient, frame, v3[0], v3[1], v3[2]);

		struct ex_tree *ma = RE_ex_child(fn, "メッシュアルファ乗算カラー");
		if (ma && !ma->is_leaf) {
			for (unsigned j = 0; j < ma->nr_children; j++) {
				struct ex_tree *mc = &ma->children[j];
				if (!mc->is_leaf || !mc->name)
					continue;
				struct ex_value *val = &mc->leaf.value;
				float a = val->type == EX_FLOAT ? val->f : (val->type == EX_INT ? (float)val->i : 1.0f);
				if (val->type != EX_FLOAT && val->type != EX_INT)
					continue;
				for (int mi = 0; mi < model->nr_meshes; mi++) {
					if (!model->meshes[mi].name || strcmp(model->meshes[mi].name, mc->name->text))
						continue;
					struct mpr_track_set *ts = ensure_mesh_track_at(mpr, mi);
					append_float_key(ts, frame, a);
				}
			}
		}
		txtex_collect_mesh_vec3(mpr, model, fn, "メッシュディフューズ乗算カラー", false, frame);
		txtex_collect_mesh_vec3(mpr, model, fn, "メッシュアンビエント加算カラー", true, frame);

		struct ex_tree *mta = RE_ex_child(fn, "メッシュテクスチャアニメーション");
		if (mta && !mta->is_leaf) {
			for (unsigned j = 0; j < mta->nr_children; j++) {
				struct ex_tree *mc = &mta->children[j];
				if (!mc->is_leaf || !mc->name)
					continue;
				struct ex_value *val = &mc->leaf.value;
				if (val->type != EX_INT && val->type != EX_FLOAT)
					continue;
				int ti = val->type == EX_INT ? val->i : (int)val->f;
				for (int mi = 0; mi < model->nr_meshes; mi++) {
					if (!model->meshes[mi].name || strcmp(model->meshes[mi].name, mc->name->text))
						continue;
					struct mpr_track_set *ts = ensure_mesh_track_at(mpr, mi);
					append_int_key(&ts->texture_anime, &ts->nr_texture_anime, frame, ti);
				}
			}
		}
	}
	ex_free(ex);
	if (!mpr_finalize(mpr)) {
		mpr_free(mpr);
		return NULL;
	}
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
	if (nr == 0 || !isfinite(frame))
		return fallback;
	int i = find_float_key(keys, nr, frame);
	if (i < 0)
		return keys[0].v;
	if (i >= nr - 1)
		return keys[nr - 1].v;
	float denom = (float)(keys[i + 1].frame - keys[i].frame);
	if (denom <= 0.0f)
		return keys[i].v;
	float t = (frame - keys[i].frame) / denom;
	return glm_lerp(keys[i].v, keys[i + 1].v, t);
}

static void eval_vec3(const struct mpr_vec3_key *keys, int nr, float frame, const vec3 fallback, vec3 out)
{
	if (nr == 0 || !isfinite(frame)) {
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
	float denom = (float)(keys[i + 1].frame - keys[i].frame);
	if (denom <= 0.0f) {
		glm_vec3_copy((float *)keys[i].v, out);
		return;
	}
	float t = (frame - keys[i].frame) / denom;
	glm_vec3_lerp((float *)keys[i].v, (float *)keys[i + 1].v, t, out);
}

static int eval_int(const struct mpr_int_key *keys, int nr, float frame, int fallback)
{
	if (nr == 0 || !isfinite(frame))
		return fallback;
	int i = find_int_key(keys, nr, frame);
	if (i < 0)
		return keys[0].v;
	return keys[i].v;  // step
}

// Combine inst defaults with .mpr Object-scope keyframes.
void mpr_evaluate_object(const struct mpr *mpr, float frame,
		struct RE_instance *inst, struct mpr_modulation *out)
{
	out->alpha = inst->alpha;
	glm_vec3_copy(inst->ambient, out->ambient);
	glm_vec3_copy(inst->diffuse, out->diffuse);
	if (!mpr)
		return;
	out->alpha *= eval_float(mpr->object.mul_alpha, mpr->object.nr_mul_alpha, frame, 1.f);
	vec3 add;
	eval_vec3(mpr->object.add_ambient, mpr->object.nr_add_ambient, frame, (vec3){0,0,0}, add);
	glm_vec3_add(out->ambient, add, out->ambient);
	vec3 mul;
	eval_vec3(mpr->object.mul_diffuse, mpr->object.nr_mul_diffuse, frame, (vec3){1,1,1}, mul);
	glm_vec3_mul(out->diffuse, mul, out->diffuse);
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
	int nr_meshes = mpr->nr_meshes < model->nr_meshes ? mpr->nr_meshes : model->nr_meshes;
	for (int i = 0; i < nr_meshes; i++) {
		const struct mpr_track_set *t = mpr->mesh_tracks[i];
		if (!t || t->nr_texture_anime == 0)
			continue;
		int mat = model->meshes[i].material;
		if (mat < 0 || mat >= model->nr_materials)
			continue;
		mat_tex_index[mat] = eval_int(t->texture_anime, t->nr_texture_anime, frame, mat_tex_index[mat]);
	}
}
