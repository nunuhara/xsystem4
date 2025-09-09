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

#include <errno.h>
#include <stdlib.h>
#include <assert.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "input.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "json.h"
#include "plugin.h"
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"

bool sact_dirty = true;

LIST_HEAD(listhead, sact_sprite) sprites_with_plugins =
	LIST_HEAD_INITIALIZER(sprites_with_plugins);

// XXX: sprites should typically be set to zero and then initialized with this
//      function upon allocation.
void sprite_init(struct sact_sprite *sp)
{
	sp->blend_rate = 255;
	sp->multiply_color = (SDL_Color){255,255,255,255};
}

void sprite_free(struct sact_sprite *sp)
{
	scene_unregister_sprite(&sp->sp);
	gfx_delete_texture(&sp->texture);
	gfx_delete_texture(&sp->text.texture);
	if (sp->plugin) {
		if (sp->plugin->free)
			sp->plugin->free(sp->plugin);
		LIST_REMOVE(sp, entry);
	}
	// restore to initial state
	memset(sp, 0, sizeof(struct sact_sprite));
	sprite_init(sp);
}

static void sprite_init_texture(struct sact_sprite *sp)
{
	if (!sp->texture.handle) {
		if (sp->sp.has_alpha)
			gfx_init_texture_rgba(&sp->texture, sp->rect.w, sp->rect.h, sp->color);
		else
			gfx_init_texture_rgb(&sp->texture, sp->rect.w, sp->rect.h, sp->color);
	}
}

static struct sprite_shader {
	struct shader s;
	GLuint blend_rate;
	GLuint multiply_color;
	GLuint add_color;
	GLuint bot_left;
	GLuint top_right;
} sprite_shader = {0};

static void prepare_sact_shader(struct gfx_render_job *job, void *data)
{
	struct sprite_shader *s = (struct sprite_shader*)job->shader;
	struct sact_sprite *sp = (struct sact_sprite*)data;
	glUniform1f(s->blend_rate, sp->blend_rate / 255.0f);
	glUniform3f(s->multiply_color,
			sp->multiply_color.r / 255.0f,
			sp->multiply_color.g / 255.0f,
			sp->multiply_color.b / 255.0f);
}

static void prepare_chipmunk_shader(struct gfx_render_job *job, void *data)
{
	struct sprite_shader *s = (struct sprite_shader*)job->shader;
	struct sact_sprite *sp = (struct sact_sprite*)data;
	glUniform1f(s->blend_rate, sp->blend_rate / 255.0f);
	glUniform3f(s->multiply_color,
			sp->multiply_color.r / 255.0f,
			sp->multiply_color.g / 255.0f,
			sp->multiply_color.b / 255.0f);
	glUniform3f(s->add_color,
			sp->add_color.r / 255.0f,
			sp->add_color.g / 255.0f,
			sp->add_color.b / 255.0f);

	Rectangle r = sp->surface_area;
	if (!r.w && !r.h) {
		r = (Rectangle) { 0, 0, sp->rect.w, sp->rect.h };
	}

	glUniform2f(s->bot_left, r.x, r.y);
	glUniform2f(s->top_right, r.x + r.w, r.y + r.h);
}

void sprite_init_sact(void)
{
	if (sprite_shader.s.prepare) {
		if (sprite_shader.s.prepare != prepare_sact_shader) {
			WARNING("mixed SACT2/ChipmunkSpriteEngine initialization");
			return;
		}
	}
	gfx_load_shader(&sprite_shader.s, "shaders/render.v.glsl", "shaders/sprite.f.glsl");
	sprite_shader.blend_rate = glGetUniformLocation(sprite_shader.s.program, "blend_rate");
	sprite_shader.multiply_color = glGetUniformLocation(sprite_shader.s.program, "multiply_color");
	sprite_shader.s.prepare = prepare_sact_shader;

}

void sprite_init_chipmunk(void)
{
	if (sprite_shader.s.prepare) {
		if (sprite_shader.s.prepare != prepare_chipmunk_shader) {
			WARNING("mixed SACT2/ChipmunkSpriteEngine initialization");
		} else {
			return;
		}
	}
	gfx_load_shader(&sprite_shader.s, "shaders/render.v.glsl", "shaders/parts.f.glsl");
	sprite_shader.blend_rate = glGetUniformLocation(sprite_shader.s.program, "blend_rate");
	sprite_shader.multiply_color = glGetUniformLocation(sprite_shader.s.program, "multiply_color");
	sprite_shader.add_color = glGetUniformLocation(sprite_shader.s.program, "add_color");
	sprite_shader.bot_left = glGetUniformLocation(sprite_shader.s.program, "bot_left");
	sprite_shader.top_right = glGetUniformLocation(sprite_shader.s.program, "top_right");
	sprite_shader.s.prepare = prepare_chipmunk_shader;
}

static void sprite_render(struct sprite *_sp)
{
	struct sact_sprite *sp = (struct sact_sprite*)_sp;
	if (sp->plugin && sp->plugin->render) {
		sp->plugin->render(sp);
		return;
	}

	sprite_init_texture(sp);

	switch (sp->draw_method) {
	case DRAW_METHOD_SCREEN:
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_MULTIPLY:
		glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_ADDITIVE:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	case DRAW_METHOD_NORMAL:
		break;
	}

	Rectangle r = sp->rect;
	if (sp->surface_area.x || sp->surface_area.y) {
		r.x -= sp->surface_area.x;
		r.y -= sp->surface_area.y;
	}

	_gfx_render_texture(&sprite_shader.s, &sp->texture, &r, sp);
	if (sp->text.texture.handle) {
		_gfx_render_texture(&sprite_shader.s, &sp->text.texture, &r, sp);
	}

	if (sp->draw_method != DRAW_METHOD_NORMAL)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

struct texture *sprite_get_texture(struct sact_sprite *sp)
{
	sprite_init_texture(sp);
	return &sp->texture;
}

static cJSON *_sprite_to_json(struct sprite *sp, bool verbose)
{
	return sprite_to_json((struct sact_sprite*)sp, verbose);
}

void sprite_set_cg(struct sact_sprite *sp, struct cg *cg)
{
	gfx_delete_texture(&sp->texture);
	gfx_init_texture_with_cg(&sp->texture, cg);
	sp->rect.w = cg->metrics.w;
	sp->rect.h = cg->metrics.h;
	sp->sp.has_pixel = true;
	sp->sp.has_alpha = cg->metrics.has_alpha;
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
	sprite_dirty(sp);
}

// XXX: Only used in English Rance VI
void sprite_set_cg_2x(struct sact_sprite *sp, struct cg *cg)
{
	// load CG at 1x
	Texture tmp;
	gfx_init_texture_with_cg(&tmp, cg);

	SDL_Color c = { 0, 0, 0, 255 };
	int w = cg->metrics.w*2;
	int h = cg->metrics.h*2;

	// upscale into sprite texture
	gfx_delete_texture(&sp->texture);
	if (cg->metrics.has_alpha) {
		gfx_init_texture_rgba(&sp->texture, w, h, c);
	} else {
		gfx_init_texture_rgb(&sp->texture, w, h, c);
	}
	gfx_copy_stretch_with_alpha_map(&sp->texture, 0, 0, w, h, &tmp, 0, 0, cg->metrics.w, cg->metrics.h);

	sp->rect.w = w;
	sp->rect.h = h;
	sp->sp.has_pixel = true;
	sp->sp.has_alpha = cg->metrics.has_alpha;
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
	sprite_dirty(sp);
	gfx_delete_texture(&tmp);
}

int sprite_set_cg_from_asset(struct sact_sprite *sp, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;
	sprite_set_cg(sp, cg);
	sp->cg_no = cg_no;
	cg_free(cg);
	return 1;
}

int sprite_set_cg_2x_from_asset(struct sact_sprite *sp, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;
	sprite_set_cg_2x(sp, cg);
	sp->cg_no = cg_no;
	cg_free(cg);
	return 1;
}

int sprite_set_cg_by_name(struct sact_sprite *sp, const char *name)
{
	struct cg *cg = asset_cg_load_by_name(name, &sp->cg_no);
	if (!cg)
		return 0;
	sprite_set_cg(sp, cg);
	cg_free(cg);
	return 1;
}

int sprite_set_cg_from_file(struct sact_sprite *sp, const char *path)
{
	struct cg *cg = cg_load_file(path);
	if (!cg)
		return 0;
	sprite_set_cg(sp, cg);
	cg_free(cg);
	return 1;
}

int sprite_save_cg(struct sact_sprite *sp, const char *path)
{
	return gfx_save_texture(sprite_get_texture(sp), path, ALCG_QNT);
}

/*
 * Attach pixel data to a sprite. The texture is initialized lazily.
 */
void sprite_init_color(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a)
{
	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = (a >= 0 ? a : 255) };
	sp->rect.w = w;
	sp->rect.h = h;
	gfx_delete_texture(&sp->texture);

	sp->sp.has_pixel = true;
	sp->sp.has_alpha = a >= 0;
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
	sprite_dirty(sp);
}

void sprite_init_custom(struct sact_sprite *sp)
{
	sp->sp.render = sprite_render;
	sp->sp.to_json = _sprite_to_json;
}

void sprite_set_pos(struct sact_sprite *sp, int x, int y)
{
	sp->rect.x = x;
	sp->rect.y = y;
	sprite_dirty(sp);
}

void sprite_set_x(struct sact_sprite *sp, int x)
{
	sp->rect.x = x;
	sprite_dirty(sp);
}

void sprite_set_y(struct sact_sprite *sp, int y)
{
	sp->rect.y = y;
	sprite_dirty(sp);
}

void sprite_set_blend_rate(struct sact_sprite *sp, int rate)
{
	sp->blend_rate = max(0, min(255, rate));
	sprite_dirty(sp);
}

void sprite_set_multiply_color(struct sact_sprite *sp, int r, int g, int b)
{
	sp->multiply_color.r = max(0, min(255, r));
	sp->multiply_color.g = max(0, min(255, g));
	sp->multiply_color.b = max(0, min(255, b));
}

void sprite_set_add_color(struct sact_sprite *sp, int r, int g, int b)
{
	sp->add_color.r = max(0, min(255, r));
	sp->add_color.g = max(0, min(255, g));
	sp->add_color.b = max(0, min(255, b));
}

void sprite_set_surface_area(struct sact_sprite *sp, int x, int y, int w, int h)
{
	sp->surface_area = (Rectangle) { x, y, w, h };
}

int sprite_set_draw_method(struct sact_sprite *sp, enum draw_method method)
{
	sp->draw_method = method;
	sprite_dirty(sp);
	return 1;
}

enum draw_method sprite_get_draw_method(struct sact_sprite *sp)
{
	return sp->draw_method;
}

void sprite_set_text_home(struct sact_sprite *sp, int x, int y)
{
	sp->text.home = (Point) { .x = x, .y = y };
	sprite_dirty(sp);
}

void sprite_set_text_line_space(struct sact_sprite *sp, int px)
{
	sp->text.line_space = px;
	sprite_dirty(sp);
}

void sprite_set_text_char_space(struct sact_sprite *sp, int px)
{
	sp->text.char_space = px;
	sprite_dirty(sp);
}

void sprite_set_text_pos(struct sact_sprite *sp, int x, int y)
{
	if (sp->text.pos.y != y)
		sp->text.current_line_height = 0;
	sp->text.pos = (Point) { .x = x, .y = y };
	sprite_dirty(sp);
}

void sprite_text_draw(struct sact_sprite *sp, struct string *text, struct text_style *ts)
{
	if (!sp->text.texture.handle) {
		gfx_init_texture_rgba(&sp->text.texture, sp->rect.w, sp->rect.h, COLOR(0, 0, 0, 0));
	}

	ts->font_spacing = sp->text.char_space;
	sp->text.pos.x += gfx_render_text(&sp->text.texture, sp->text.pos.x, sp->text.pos.y, text->text, ts, false);
	if (sp->text.current_line_height < ts->size)
		sp->text.current_line_height = ts->size;
	sprite_dirty(sp);
}

void sprite_text_clear(struct sact_sprite *sp)
{
	gfx_delete_texture(&sp->text.texture);
	sprite_dirty(sp);
}

void sprite_text_home(struct sact_sprite *sp, int size)
{
	sp->text.pos = sp->text.home;
	sp->text.current_line_height = size;
	sprite_dirty(sp);
}

void sprite_text_new_line(struct sact_sprite *sp, int size)
{
	if (!size)
		size = sp->text.current_line_height;
	sp->text.pos = POINT(sp->text.home.x, sp->text.pos.y + size + sp->text.line_space);
	sp->text.current_line_height = 0;
	sprite_dirty(sp);
}

void sprite_text_copy(struct sact_sprite *dsp, struct sact_sprite *ssp)
{
	sprite_text_clear(dsp);
	if (ssp->text.texture.handle) {
		SDL_Color c = { 0, 0, 0, 0 };
		gfx_init_texture_rgba(&dsp->text.texture, dsp->rect.w, dsp->rect.h, c);
		gfx_copy(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
		gfx_copy_amap(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
	}
	dsp->text.home = ssp->text.home;
	dsp->text.pos = ssp->text.pos;
	dsp->text.char_space = ssp->text.char_space;
	dsp->text.line_space = ssp->text.line_space;
	dsp->text.current_line_height = ssp->text.current_line_height;
	sprite_dirty(dsp);
}

bool sprite_is_point_in_rect(struct sact_sprite *sp, int x, int y)
{
	Point p = POINT(x, y);
	return !!SDL_PointInRect(&p, &sp->rect);
}

bool sprite_is_point_in(struct sact_sprite *sp, int x, int y)
{
	if (!sprite_is_point_in_rect(sp, x, y))
		return 0;

	// check alpha
	struct texture *t = sprite_get_texture(sp);
	SDL_Color c = gfx_get_pixel(t, x - sp->rect.x, y - sp->rect.y);
	return !!c.a;
}

int sprite_get_amap_value(struct sact_sprite *sp, int x, int y)
{
	return gfx_get_pixel(sprite_get_texture(sp), x, y).a;
}

void sprite_get_pixel_value(struct sact_sprite *sp, int x, int y, int *r, int *g, int *b)
{
	SDL_Color c = gfx_get_pixel(sprite_get_texture(sp), x, y);
	*r = c.r;
	*g = c.g;
	*b = c.b;
}

void sprite_bind_plugin(struct sact_sprite *sp, struct draw_plugin *plugin)
{
	if (!sp->plugin && plugin)
		LIST_INSERT_HEAD(&sprites_with_plugins, sp, entry);
	else if (sp->plugin && !plugin)
		LIST_REMOVE(sp, entry);
	if (sp->plugin != plugin && sp->plugin && sp->plugin->free)
		sp->plugin->free(sp->plugin);
	sp->plugin = plugin;
}

void sprite_call_plugins(void)
{
	struct sact_sprite *sp;
	LIST_FOREACH(sp, &sprites_with_plugins, entry) {
		if (sp->plugin->update)
			sp->plugin->update(sp);
	}
}
