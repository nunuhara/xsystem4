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

#include "gfx/gfx.h"
#include "scene.h"
#include "xsystem4.h"

#include "parts_internal.h"

// Goat sprites are integrated into the scene via a single (virtual) sprite
static struct sprite goat_sprite;

struct parts_render_params {
	Point offset;
	uint8_t alpha;
};

static struct {
	struct shader shader;
	GLint alpha_mod;
	GLint bot_left;
	GLint top_right;
} parts_shader;

static void parts_render_text(struct parts *parts, struct parts_common *common,
		struct parts_render_params *params)
{
	Rectangle rect = {
		.x = params->offset.x + common->origin_offset.x,
		.y = params->offset.y + common->origin_offset.y,
		.w = common->w,
		.h = common->h
	};
	gfx_render_texture(&parts->states[parts->state].common.texture, &rect);
}

static void parts_render_cg(struct parts *parts, struct parts_common *common,
		struct parts_render_params *params)
{
	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { params->offset.x, params->offset.y, 0 });
	// FIXME: need perspective for 3D rotate
	//glm_rotate_x(mw_transform, parts->rotation.x, mw_transform);
	//glm_rotate_y(mw_transform, parts->rotation.y, mw_transform);
	glm_rotate_z(mw_transform, parts->rotation.z, mw_transform);
	glm_scale(mw_transform, (vec3){ parts->scale.x, parts->scale.y, 1.0 });
	glm_translate(mw_transform, (vec3){ common->origin_offset.x, common->origin_offset.y, 0 });
	glm_scale(mw_transform, (vec3){ common->w, common->h, 1.0 });
	mat4 wv_transform = WV_TRANSFORM(config.view_width, config.view_height);

	struct gfx_render_job job = {
		.shader = &parts_shader.shader,
		.texture = common->texture.handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = &common->texture,
	};

	Rectangle r = common->surface_area;
	if (!r.w && !r.h) {
		r = (Rectangle) { 0, 0, common->texture.w, common->texture.h };
	}

	gfx_prepare_job(&job);
	glUniform1f(parts_shader.alpha_mod, params->alpha / 255.0);
	glUniform2f(parts_shader.bot_left, r.x, r.y);
	glUniform2f(parts_shader.top_right, r.x + r.w, r.y + r.h);
	gfx_run_job(&job);
}

void parts_render(struct parts *parts)
{
	if (!parts->show)
		return;
	if (parts->linked_to >= 0) {
		struct parts *link_parts = parts_get(parts->linked_to);
		struct parts_state *link_state = &link_parts->states[link_parts->state];
		if (!SDL_PointInRect(&parts_prev_pos, &link_state->common.hitbox))
			return;
	}

	struct parts_render_params params = {
		.offset = parts->pos,
		.alpha = parts->alpha,
	};

	// adjust pos/alpha per parent parts
	struct parts *parent = parts;
	while (parent->parent >= 0) {
		parent = parts_get(parent->parent);
		if (!parent->show)
			return;
		params.alpha *= parent->alpha / 255.0f;
		params.offset.x += parent->pos.x;
		params.offset.y += parent->pos.y;
	}

	// render
	struct parts_state *state = &parts->states[parts->state];
	if (state->common.texture.handle) {
		switch (state->type) {
		case PARTS_UNINITIALIZED:
			break;
		case PARTS_CG:
		case PARTS_ANIMATION:
		case PARTS_NUMERAL:
		case PARTS_HGAUGE:
		case PARTS_VGAUGE:
		case PARTS_CONSTRUCTION_PROCESS:
			parts_render_cg(parts, &state->common, &params);
			break;
		case PARTS_TEXT:
			parts_render_text(parts, &state->common, &params);
			break;
		}
	}
}

void parts_engine_render(possibly_unused struct sprite *_)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_render(parts);
	}
}

void parts_engine_dirty(void)
{
	scene_sprite_dirty(&goat_sprite);
}

void parts_dirty(possibly_unused struct parts *parts)
{
	parts_engine_dirty();
}

static void _parts_engine_print(possibly_unused struct sprite *_)
{
	parts_engine_print();
}

void parts_render_init(void)
{
	goat_sprite.z = 0;
	goat_sprite.z2 = INT_MAX;
	goat_sprite.has_pixel = true;
	goat_sprite.has_alpha = true;
	goat_sprite.render = parts_engine_render;
	goat_sprite.debug_print = _parts_engine_print;
	scene_register_sprite(&goat_sprite);

	gfx_load_shader(&parts_shader.shader, "shaders/render.v.glsl", "shaders/parts.f.glsl");
	parts_shader.alpha_mod = glGetUniformLocation(parts_shader.shader.program, "alpha_mod");
	parts_shader.bot_left = glGetUniformLocation(parts_shader.shader.program, "bot_left");
	parts_shader.top_right = glGetUniformLocation(parts_shader.shader.program, "top_right");
}
