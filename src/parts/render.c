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
#include <assert.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/hashtable.h"

#include "gfx/gfx.h"
#include "scene.h"
#include "xsystem4.h"

#include "parts_internal.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

static struct {
	struct shader shader;
	GLint blend_rate;
	GLint bot_left;
	GLint top_right;
	GLint add_color;
	GLint multiply_color;
} parts_shader;

static void parts_render_text(struct parts *parts, struct parts_text *t)
{
	int x = parts->global.pos.x + t->common.origin_offset.x;
	int y = parts->global.pos.y + t->common.origin_offset.y;
	for (int i = 0; i < t->nr_lines; i++) {
		struct parts_text_line *line = &t->lines[i];
		for (int j = 0; j < line->nr_chars; j++) {
			struct parts_text_char *ch = &line->chars[j];
			Rectangle rect = {
				.x = x,
				.y = y,
				.w = ch->t.w,
				.h = ch->t.h
			};
			gfx_render_texture(&ch->t, &rect);
			x += ch->advance;
		}
		x = parts->global.pos.x + t->common.origin_offset.x;
		y += line->height;
	}
}

static void parts_render_texture(struct texture *texture, mat4 mw_transform, Rectangle *rect, float blend_rate, vec3 add_color, vec3 multiply_color)
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
	gfx_run_job(&job);
}

static void parts_render_cg(struct parts *parts, struct parts_common *common)
{
	switch (parts->draw_filter) {
	case 1:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	default:
		break;
	}

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { parts->global.pos.x, parts->global.pos.y, 0 });
	// FIXME: need perspective for 3D rotate
	//glm_rotate_x(mw_transform, parts->rotation.x, mw_transform);
	//glm_rotate_y(mw_transform, parts->rotation.y, mw_transform);
	glm_rotate_z(mw_transform, parts->local.rotation.z * (M_PI/180.0), mw_transform);
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
	parts_render_texture(&common->texture, mw_transform, &r, parts->global.alpha / 255.0, add_color, multiply_color);

	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
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
	parts_render_texture(src, mw_transform, &r, blend_rate, add_color, multiply_color);
}

static void parts_render_flash_sprite(struct parts *parts, struct parts_flash *f, struct parts_flash_object *obj, struct swf_tag_define_sprite *tag)
{
	struct parts_flash_object *obj2 = ht_get_int(f->sprites, tag->sprite_id, NULL);
	if (!obj)
		ERROR("undefined sprite id %d", tag->sprite_id);

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
		struct parts_state *link_state = &link_parts->states[link_parts->state];
		if (!SDL_PointInRect(&parts_prev_pos, &link_state->common.hitbox))
			return;
	}

	// render
	struct parts_state *state = &parts->states[parts->state];
	switch (state->type) {
	case PARTS_UNINITIALIZED:
		break;
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_NUMERAL:
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
	case PARTS_CONSTRUCTION_PROCESS:
		if (state->common.texture.handle)
			parts_render_cg(parts, &state->common);
		break;
	case PARTS_TEXT:
		parts_render_text(parts, &state->text);
		break;
	case PARTS_FLASH:
		parts_render_flash(parts, &state->flash);
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
	// XXX: hack for Rance 01 load issue
	//      There is a bug in Rance 01 where a single bad frame is displayed after
	//      loading a save. When this happens, the game passes a negative time delta
	//      to PE_Update. We fix this issue by ignoring such calls.
	if (passed_time < 0)
		return;
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
	gfx_load_shader(&parts_shader.shader, "shaders/render.v.glsl", "shaders/parts.f.glsl");
	parts_shader.blend_rate = glGetUniformLocation(parts_shader.shader.program, "blend_rate");
	parts_shader.bot_left = glGetUniformLocation(parts_shader.shader.program, "bot_left");
	parts_shader.top_right = glGetUniformLocation(parts_shader.shader.program, "top_right");
	parts_shader.add_color = glGetUniformLocation(parts_shader.shader.program, "add_color");
	parts_shader.multiply_color = glGetUniformLocation(parts_shader.shader.program, "multiply_color");
}
