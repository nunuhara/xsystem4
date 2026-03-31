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

#include <limits.h>
#include <math.h>
#include <assert.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/flat.h"

#include "gfx/gfx.h"
#include "sact.h"
#include "scene.h"
#include "sprite.h"
#include "xsystem4.h"

#include "parts_internal.h"

static struct {
	struct shader shader;
	GLint blend_rate;
	GLint bot_left;
	GLint top_right;
	GLint add_color;
	GLint multiply_color;
	GLint draw_filter;
	GLint use_clipper;
	GLint clipper_tex;
	GLint inv_clipper_transform;
} parts_shader;

static void set_draw_filter_blend_func(int draw_filter)
{
	switch (draw_filter) {
	case PARTS_DRAW_FILTER_ADDITIVE:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	case PARTS_DRAW_FILTER_MULTIPLY:
		glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
		break;
	case PARTS_DRAW_FILTER_SCREEN:
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
		break;
	default:
		break;
	}
}

static void parts_render_texture(struct texture *texture, mat4 mw_transform, Rectangle *rect, float blend_rate, vec3 add_color, vec3 multiply_color, int draw_filter, int alpha_clipper)
{
	mat4 wv_transform = WV_TRANSFORM(config.view_width, config.view_height);

	struct gfx_render_job job = {
		.shader = &parts_shader.shader,
		.shape = GFX_RECTANGLE,
		.texture = texture->handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = texture,
	};

	gfx_prepare_job(&job);
	glUniform1f(parts_shader.blend_rate, blend_rate);
	glUniform2f(parts_shader.bot_left, rect->x, rect->y);
	glUniform2f(parts_shader.top_right, rect->x + rect->w, rect->y + rect->h);
	glUniform3fv(parts_shader.add_color, 1, add_color);
	glUniform3fv(parts_shader.multiply_color, 1, multiply_color);
	glUniform1i(parts_shader.draw_filter, draw_filter);

	struct parts *clipper = alpha_clipper ? parts_try_get(alpha_clipper) : NULL;
	if (clipper) {
		struct parts_common *c_common = &clipper->states[clipper->state].common;

		// Calculate the inverse of the clipper's world matrix.
		mat4 clip_mw = GLM_MAT4_IDENTITY_INIT;
		glm_translate(clip_mw, (vec3) { clipper->global.pos.x, clipper->global.pos.y, 0 });
		glm_rotate_z(clip_mw, glm_rad(clipper->local.rotation.z), clip_mw);
		glm_scale(clip_mw, (vec3){ clipper->global.scale.x, clipper->global.scale.y, 1.0 });
		glm_translate(clip_mw, (vec3){ c_common->origin_offset.x, c_common->origin_offset.y, 0 });
		glm_scale(clip_mw, (vec3){ c_common->w, c_common->h, 1.0 });
		mat4 clip_inv;
		glm_mat4_inv(clip_mw, clip_inv);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, c_common->texture.handle);
		glActiveTexture(GL_TEXTURE0);

		glUniform1i(parts_shader.use_clipper, 1);
		glUniform1i(parts_shader.clipper_tex, 1);
		glUniformMatrix4fv(parts_shader.inv_clipper_transform, 1, GL_FALSE, clip_inv[0]);
	} else {
		glUniform1i(parts_shader.use_clipper, 0);
	}

	gfx_run_job(&job);
}

static void parts_render_text(struct parts *parts, struct parts_text *t)
{
	vec3 add_color = {
		parts->global.add_color.r / 255.0f,
		parts->global.add_color.g / 255.0f,
		parts->global.add_color.b / 255.0f
	};
	vec3 multiply_color = {
		parts->global.multiply_color.r / 255.0f,
		parts->global.multiply_color.g / 255.0f,
		parts->global.multiply_color.b / 255.0f,
	};
	float blend_rate = parts->global.alpha / 255.0;

	int x = parts->global.pos.x + t->common.origin_offset.x;
	int y = parts->global.pos.y + t->common.origin_offset.y;
	for (int i = 0; i < t->nr_lines; i++) {
		struct parts_text_line *line = &t->lines[i];
		for (int j = 0; j < line->nr_chars; j++) {
			struct parts_text_char *ch = &line->chars[j];
			mat4 mw_transform = WORLD_TRANSFORM(ch->t.w, ch->t.h, x, y);
			Rectangle r = { 0, 0, ch->t.w, ch->t.h };
			parts_render_texture(&ch->t, mw_transform, &r, blend_rate, add_color, multiply_color, 0, parts->alpha_clipper_parts_no);
			x += ch->advance;
		}
		x = parts->global.pos.x + t->common.origin_offset.x;
		y += line->height + t->line_space;
	}
}

static void parts_render_cg(struct parts *parts, struct parts_common *common)
{
	set_draw_filter_blend_func(parts->draw_filter);

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { parts->global.pos.x, parts->global.pos.y, 0 });
	// FIXME: need perspective for 3D rotate
	//glm_rotate_x(mw_transform, parts->rotation.x, mw_transform);
	//glm_rotate_y(mw_transform, parts->rotation.y, mw_transform);
	glm_rotate_z(mw_transform, glm_rad(parts->local.rotation.z), mw_transform);
	glm_scale(mw_transform, (vec3){ parts->global.scale.x, parts->global.scale.y, 1.0 });
	glm_translate(mw_transform, (vec3){ common->origin_offset.x, common->origin_offset.y, 0 });
	glm_scale(mw_transform, (vec3){ common->w, common->h, 1.0 });

	Rectangle r = common->surface_area;
	if (!r.w && !r.h) {
		r = (Rectangle) { 0, 0, common->texture.w, common->texture.h };
	}

	vec3 add_color = {
		parts->global.add_color.r / 255.0f,
		parts->global.add_color.g / 255.0f,
		parts->global.add_color.b / 255.0f
	};
	vec3 multiply_color = {
		parts->global.multiply_color.r / 255.0f,
		parts->global.multiply_color.g / 255.0f,
		parts->global.multiply_color.b / 255.0f,
	};
	parts_render_texture(&common->texture, mw_transform, &r, parts->global.alpha / 255.0, add_color, multiply_color, parts->draw_filter, parts->alpha_clipper_parts_no);

	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

struct emitter_render_ud {
	struct parts_flat *f;
	mat4 transform;       // root * base * layer for this emitter+birth_frame
	float parent_alpha;
	vec2 align;
	int alpha_clipper;
	int draw_filter;
	vec3 add_color;
	vec3 mul_color;
};

static void render_emitter_particle_cb(const struct flat_emitter_particle *p,
		void *ud)
{
	struct emitter_render_ud *d = ud;
	if (p->cg_lib_idx < 0 || (size_t)p->cg_lib_idx >= d->f->nr_libraries)
		return;
	Texture *tex = &d->f->textures[p->cg_lib_idx];
	if (!tex->handle)
		return;

	mat4 m = GLM_MAT4_IDENTITY_INIT;
	glm_translate(m, (vec3){ p->pos[0], p->pos[1], 0 });
	if (p->rot[2] != 0)
		glm_rotate_z(m, glm_rad(p->rot[2]), m);
	if (p->rot[0] != 0)
		glm_rotate_x(m, glm_rad(-p->rot[0]), m);
	if (p->rot[1] != 0)
		glm_rotate_y(m, glm_rad(p->rot[1]), m);
	glm_scale(m, (vec3){ p->scale[0], p->scale[1], 1.0f });
	glm_translate(m, (vec3){ -d->align[0], -d->align[1], 0 });

	// Bring particle into screen space, then kill the Z-output row so
	// clip_z stays at the near plane.
	glm_mat4_mul(d->transform, m, m);
	m[0][2] = m[1][2] = m[2][2] = m[3][2] = 0.0f;
	glm_scale(m, (vec3){ tex->w, tex->h, 1.0f });

	if (d->draw_filter != PARTS_DRAW_FILTER_NORMAL)
		set_draw_filter_blend_func(d->draw_filter);

	float blend_rate = d->parent_alpha * p->fade_alpha;
	Rectangle rect = { 0, 0, tex->w, tex->h };
	parts_render_texture(tex, m, &rect, blend_rate,
			d->add_color, d->mul_color, d->draw_filter, d->alpha_clipper);

	if (d->draw_filter != PARTS_DRAW_FILTER_NORMAL)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

static void render_flat_emitter(struct parts *parts, struct parts_flat *f,
		int emitter_lib_idx, int local, int frame_count,
		struct flat_key_data_graphic *keys,
		mat4 root, float parent_alpha,
		struct flat_key_stack *key_stack)
{
	vec2 align;
	if (!parts_flat_emitter_get_align_offset(f, emitter_lib_idx, align))
		return;

	struct flat_emitter *em = &f->flat->libraries[emitter_lib_idx].emitter;
	int active_frames = max(1, frame_count - em->particle_lifetime + 1);
	float parts_alpha = parts->global.alpha / 255.0f;

	// TODO: key_stack reflects the current render frame, but each particle
	// should see the ancestor keyframes at its birth frame instead.
	// Currently all particles of this emitter share the same ancestor pose,
	// which is noticeable only on `猿玉／取得済みマーク.flat` in Rance 9.
	// Fixing this requires per-emitter state in parts_flat_update to record
	// ancestor keys at each birth frame; base would then depend on birth_frame.
	mat4 base;
	parts_flat_build_emitter_base_matrix(em, key_stack, base);

	int min_birth = max(0, local - em->particle_lifetime + 1);
	int max_birth = min(active_frames - 1, local);
	for (int birth_frame = min_birth; birth_frame <= max_birth; birth_frame++) {
		int age = local - birth_frame;

		// Use the birth frame's key transform so particles stay at their
		// birth position while the emitter moves.
		struct flat_key_data_graphic *birth_key = &keys[birth_frame];
		float layer_alpha = parent_alpha * birth_key->alpha / 255.0f;

		struct flat_emitter_layer_effective eff;
		parts_flat_emitter_resolve_layer(em, birth_key,
				parts_alpha, layer_alpha, &eff);

		// Build per-birth-frame layer matrix from birth_key and the emitter's
		// inherit_* flags.
		mat4 layer_m;
		parts_flat_build_layer_matrix(birth_key, eff.pos,
				eff.use_rotation, eff.use_scale, eff.use_origin,
				eff.reverse_lr, eff.reverse_tb,
				layer_m);

		struct emitter_render_ud ud = {
			.f = f,
			.parent_alpha = eff.alpha,
			.alpha_clipper = parts->alpha_clipper_parts_no,
			.draw_filter = eff.draw_filter,
		};
		glm_vec2_copy(align, ud.align);
		glm_vec3_copy(eff.add_color, ud.add_color);
		glm_vec3_copy(eff.mul_color, ud.mul_color);
		glm_mat4_mul(base, layer_m, ud.transform);
		glm_mat4_mul(root, ud.transform, ud.transform);
		parts_flat_foreach_emitter_particle(f, emitter_lib_idx, keys,
				birth_frame, age, frame_count,
				render_emitter_particle_cb, &ud);
	}
}

struct flat_draw_ctx {
	mat4 matrix;
	float alpha;
	vec3 add_color;
	vec3 mul_color;
	int draw_filter;
};

static void render_flat_layer(struct parts *parts, struct parts_flat *f,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		struct flat_draw_ctx *ctx, mat4 root,
		struct flat_key_stack *key_stack);

static void render_flat_cg(struct parts *parts, Texture *tex,
		struct flat_key_data_graphic *key, struct flat_draw_ctx *ctx)
{
	if (!tex->handle)
		return;

	set_draw_filter_blend_func(ctx->draw_filter);

	mat4 render_m;
	glm_mat4_copy(ctx->matrix, render_m);
	// Kill the Z-output row to pin clip_z at the near plane, avoiding
	// near/far clipping of 3D-rotated sprites.
	render_m[0][2] = render_m[1][2] = render_m[2][2] = render_m[3][2] = 0.0f;
	// area_x/area_y select a sub-rectangle of the texture atlas, but
	// should not shift the on-screen position. This translation cancels
	// the offset that the sub-rect's top-left would otherwise introduce.
	glm_translate(render_m, (vec3){ -(float)key->area_x, -(float)key->area_y, 0.0f });
	glm_scale(render_m, (vec3){ tex->w, tex->h, 1.0f });

	Rectangle rect;
	if (key->area_width && key->area_height) {
		rect = (Rectangle){ key->area_x, key->area_y, key->area_width, key->area_height };
	} else {
		rect = (Rectangle){ 0, 0, tex->w, tex->h };
	}

	parts_render_texture(tex, render_m, &rect, ctx->alpha, ctx->add_color, ctx->mul_color,
			ctx->draw_filter, parts->alpha_clipper_parts_no);

	if (ctx->draw_filter != PARTS_DRAW_FILTER_NORMAL)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

static void render_flat_item(struct parts *parts, struct parts_flat *f,
		struct flat_layer_state *state, size_t tl_idx,
		struct flat_timeline *tl, int local,
		struct flat_draw_ctx *parent, mat4 root,
		struct flat_key_stack *key_stack)
{
	int lib_idx = parts_flat_find_library(f->flat, tl->library_name->text);
	if (lib_idx < 0 || (size_t)lib_idx >= f->flat->nr_libraries)
		return;

	struct flat_library *lib = &f->flat->libraries[lib_idx];
	if (lib->type == FLAT_LIB_EMITTER) {
		render_flat_emitter(parts, f, lib_idx, local, tl->frame_count,
				tl->graphic.keys,
				root, parent->alpha, key_stack);
		return;
	}

	struct flat_key_data_graphic *key = &tl->graphic.keys[local];
	mat4 layer_m;
	vec2 pos = { key->pos_x, key->pos_y };
	parts_flat_build_layer_matrix(key, pos,
			true, true, true,
			key->reverse_lr, key->reverse_tb,
			layer_m);

	struct flat_draw_ctx ctx;
	glm_mat4_mul(parent->matrix, layer_m, ctx.matrix);
	ctx.alpha = parent->alpha * key->alpha / 255.0f;
	vec3 key_add = { key->add_r / 255.0f, key->add_g / 255.0f, key->add_b / 255.0f };
	vec3 key_mul = { key->mul_r / 255.0f, key->mul_g / 255.0f, key->mul_b / 255.0f };
	glm_vec3_add(parent->add_color, key_add, ctx.add_color);
	glm_vec3_mul(parent->mul_color, key_mul, ctx.mul_color);
	ctx.draw_filter = key->draw_filter != PARTS_DRAW_FILTER_NORMAL
			? key->draw_filter : parent->draw_filter;

	switch (lib->type) {
	case FLAT_LIB_CG:
		render_flat_cg(parts, &f->textures[lib_idx], key, &ctx);
		break;
	case FLAT_LIB_TIMELINE: {
		struct flat_layer_state *child = state->children[tl_idx];
		if (child && key_stack->count < FLAT_MAX_ANCESTOR_DEPTH) {
			key_stack->keys[key_stack->count++] = key;
			render_flat_layer(parts, f, child,
					lib->timeline.timelines, lib->timeline.nr_timelines,
					&ctx, root, key_stack);
			key_stack->count--;
		}
		break;
	}
	case FLAT_LIB_STOP_MOTION: {
		int cg_idx = parts_flat_stop_motion_get_cg_lib(f, lib_idx, local);
		if (cg_idx >= 0 && (size_t)cg_idx < f->nr_libraries)
			render_flat_cg(parts, &f->textures[cg_idx], key, &ctx);
		break;
	}
	case FLAT_LIB_EMITTER:
		// cannot happen, handled above
		break;
	case FLAT_LIB_MEMORY:
		// not implemented
		break;
	}
}

static void render_flat_layer(struct parts *parts, struct parts_flat *f,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		struct flat_draw_ctx *ctx, mat4 root,
		struct flat_key_stack *key_stack)
{
	// reverse order for correct z-ordering
	for (size_t i = nr_timelines; i-- > 0;) {
		struct flat_timeline *tl = &timelines[i];
		if (tl->type != FLAT_TIMELINE_GRAPHIC)
			continue;
		int local = state->current_frame - tl->begin_frame;
		if (local < 0 || local >= tl->frame_count)
			continue;

		if (local >= (int)tl->graphic.count)
			continue;

		render_flat_item(parts, f, state, i, tl, local, ctx, root, key_stack);
	}
}

static void parts_render_flat(struct parts *parts, struct parts_flat *f)
{
	if (!f->flat || !f->root_state)
		return;

	struct flat_draw_ctx ctx;
	glm_mat4_identity(ctx.matrix);
	glm_translate(ctx.matrix, (vec3){ parts->global.pos.x, parts->global.pos.y, 0 });
	glm_rotate_z(ctx.matrix, glm_rad(parts->local.rotation.z), ctx.matrix);
	glm_scale(ctx.matrix, (vec3){ parts->global.scale.x, parts->global.scale.y, 1.0f });
	ctx.alpha = parts->global.alpha / 255.0f;
	glm_vec3_zero(ctx.add_color);
	glm_vec3_one(ctx.mul_color);
	ctx.draw_filter = PARTS_DRAW_FILTER_NORMAL;

	struct flat_key_stack key_stack = { .count = 0 };
	render_flat_layer(parts, f, f->root_state,
			f->flat->timelines, f->flat->nr_timelines,
			&ctx, ctx.matrix, &key_stack);
}

static void parts_render_3dlayer(struct parts *parts, struct parts_3dlayer *l)
{
	if (l->sprite_no < 0)
		return;
	struct sact_sprite *sp = sact_try_get_sprite(l->sprite_no);
	if (!sp)
		return;
	struct texture *tex = sprite_get_texture(sp);
	if (!tex->handle)
		return;
	// The bound sprite's texture is recreated by RE_set_viewport, so
	// refresh the parts_common's view of it each frame.
	l->common.texture = *tex;
	l->common.w = tex->w;
	l->common.h = tex->h;
	parts_render_cg(parts, &l->common);
}

static void parts_render_flash_shape(struct parts *parts, struct parts_flash *f, struct parts_flash_object *obj, struct swf_tag_define_shape *tag)
{
	struct texture *src = ht_get_int(f->bitmaps, tag->fill_style.bitmap_id, NULL);
	if (!src)
		ERROR("undefined bitmap id %d", tag->fill_style.bitmap_id);

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { parts->global.pos.x, parts->global.pos.y, 0 });
	glm_rotate_z(mw_transform, parts->local.rotation.z, mw_transform);
	glm_scale(mw_transform, (vec3){ parts->global.scale.x, parts->global.scale.y, 1.0 });
	glm_translate(mw_transform, (vec3){ f->common.origin_offset.x, f->common.origin_offset.y, 0 });

	glm_mul(mw_transform, obj->matrix, mw_transform);

	glm_translate(mw_transform, (vec3){
		twips_to_float(tag->bounds.x_min),
		twips_to_float(tag->bounds.y_min),
		0});
	glm_scale(mw_transform, (vec3){
		twips_to_float(tag->bounds.x_max - tag->bounds.x_min),
		twips_to_float(tag->bounds.y_max - tag->bounds.y_min),
		1.0});

	Rectangle r = { 0, 0, src->w, src->h };

	float blend_rate = (parts->global.alpha / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[3]);
	vec3 add_color = {
		(parts->global.add_color.r + obj->color_transform.add_terms[0]) / 255.0f,
		(parts->global.add_color.g + obj->color_transform.add_terms[1]) / 255.0f,
		(parts->global.add_color.b + obj->color_transform.add_terms[2]) / 255.0f
	};
	vec3 multiply_color = {
		(parts->global.multiply_color.r / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[0]),
		(parts->global.multiply_color.g / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[1]),
		(parts->global.multiply_color.b / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[2])
	};
	parts_render_texture(src, mw_transform, &r, blend_rate, add_color, multiply_color, 0, parts->alpha_clipper_parts_no);
}

static void parts_render_flash_sprite(struct parts *parts, struct parts_flash *f, struct parts_flash_object *obj, struct swf_tag_define_sprite *tag)
{
	struct parts_flash_object *obj2 = ht_get_int(f->sprites, tag->sprite_id, NULL);
	if (!obj2)
		return;  // sprite has no visual elements.

	struct swf_tag *child = ht_get_int(f->dictionary, obj2->character_id, NULL);
	if (!child)
		ERROR("character %d is not defined", obj2->character_id);
	if (child->type != TAG_DEFINE_SHAPE)
		ERROR("unexpected child type %d", child->type);

	struct parts_flash_object transform;
	glm_mul(obj->matrix, obj2->matrix, transform.matrix);
	for (int i = 0; i < 4; i++) {
		transform.color_transform.add_terms[i] =
			obj->color_transform.add_terms[i] + obj2->color_transform.add_terms[i];
	}
	for (int i = 0; i < 4; i++) {
		transform.color_transform.mult_terms[i] =
			fixed16_mul(obj->color_transform.mult_terms[i], obj2->color_transform.mult_terms[i]);
	}
	parts_render_flash_shape(parts, f, &transform, (struct swf_tag_define_shape*)child);
}

static void parts_render_flash(struct parts *parts, struct parts_flash *f)
{
	struct parts_flash_object *obj;
	TAILQ_FOREACH(obj, &f->display_list, entry) {
		struct swf_tag *tag = ht_get_int(f->dictionary, obj->character_id, NULL);
		if (!tag) {
			WARNING("character %d is not defined", obj->character_id);
			continue;
		}
		// set blend mode
		switch (obj->blend_mode) {
		case PARTS_FLASH_BLEND_MULTIPLY:
			glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
			break;
		case PARTS_FLASH_BLEND_SCREEN:
			glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
			break;
		case PARTS_FLASH_BLEND_ADD:
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
			break;
		default:
			break;
		}
		// render object
		switch (tag->type) {
		case TAG_DEFINE_SHAPE:
			parts_render_flash_shape(parts, f, obj, (struct swf_tag_define_shape*)tag);
			break;
		case TAG_DEFINE_SPRITE:
			parts_render_flash_sprite(parts, f, obj, (struct swf_tag_define_sprite*)tag);
			break;
		default:
			ERROR("unknown tag %d", tag->type);
		}
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
	}
}

void parts_render(struct parts *parts)
{
	if (!parts->global.show)
		return;
	if (parts->message_window && !parts_message_window_show)
		return;
	if (parts->linked_to >= 0) {
		struct parts *link_parts = parts_get(parts->linked_to);
		if (!link_parts->is_hovered)
			return;
	}

	// render
	struct parts_state *state = &parts->states[parts->state];
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_RECT_DETECTION:
	case PARTS_LAYOUT_BOX:
		break;
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_NUMERAL:
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
	case PARTS_CONSTRUCTION_PROCESS:
	case PARTS_MOVIE:
		if (state->common.texture.handle)
			parts_render_cg(parts, &state->common);
		break;
	case PARTS_TEXT:
		parts_render_text(parts, &state->text);
		break;
	case PARTS_FLASH:
		parts_render_flash(parts, &state->flash);
		break;
	case PARTS_FLAT:
		parts_render_flat(parts, &state->flat);
		break;
	case PARTS_3DLAYER:
		parts_render_3dlayer(parts, &state->layer3d);
		break;
	}
}

void parts_render_family(struct parts *parts)
{
	parts_render(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_render_family(child);
	}
}

void parts_engine_render(possibly_unused struct sprite *_)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_render(parts);
	}
}

void parts_sprite_render(struct sprite *sp)
{
	parts_render((struct parts*)sp);
}

static bool pe_dirty = false;

void parts_render_update(int passed_time)
{
	if (pe_dirty) {
		struct parts *p;
		PARTS_LIST_FOREACH(p) {
			scene_sprite_dirty(&p->sp);
		}
		pe_dirty = false;
	}
}

void parts_engine_dirty(void)
{
	pe_dirty = true;
}

void parts_engine_clean(void)
{
	pe_dirty = false;
}

void parts_dirty(possibly_unused struct parts *parts)
{
	pe_dirty = true;
}

void parts_render_init(void)
{
	gfx_load_shader(&parts_shader.shader, "shaders/parts.v.glsl", "shaders/parts.f.glsl");
	parts_shader.blend_rate = glGetUniformLocation(parts_shader.shader.program, "blend_rate");
	parts_shader.bot_left = glGetUniformLocation(parts_shader.shader.program, "bot_left");
	parts_shader.top_right = glGetUniformLocation(parts_shader.shader.program, "top_right");
	parts_shader.add_color = glGetUniformLocation(parts_shader.shader.program, "add_color");
	parts_shader.multiply_color = glGetUniformLocation(parts_shader.shader.program, "multiply_color");
	parts_shader.draw_filter = glGetUniformLocation(parts_shader.shader.program, "draw_filter");
	parts_shader.use_clipper = glGetUniformLocation(parts_shader.shader.program, "use_clipper");
	parts_shader.clipper_tex = glGetUniformLocation(parts_shader.shader.program, "clipper_tex");
	parts_shader.inv_clipper_transform = glGetUniformLocation(parts_shader.shader.program, "inv_clipper_transform");
}
