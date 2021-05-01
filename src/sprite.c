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

#include <stdlib.h>

#include "system4.h"
#include "system4/ald.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "audio.h"
#include "input.h"
#include "gfx/gfx.h"
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"

bool sact_dirty = true;

static struct sact_sprite wp;

TAILQ_HEAD(listhead, sact_sprite) sprite_list =
	TAILQ_HEAD_INITIALIZER(sprite_list);

void sprite_register(struct sact_sprite *sp)
{
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (p->z > sp->z) {
			TAILQ_INSERT_BEFORE(p, sp, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&sprite_list, sp, entry);
	if (sp->show)
		scene_dirty();
}

void sprite_unregister(struct sact_sprite *sp)
{
	TAILQ_REMOVE(&sprite_list, sp, entry);
	if (sp->show)
		scene_dirty();
}

void sprite_free(struct sact_sprite *sp)
{
	if (sp->show)
		sprite_unregister(sp);
	gfx_delete_texture(&sp->texture);
	gfx_delete_texture(&sp->text.texture);
	free(sp);
}

void sprite_render_scene(void)
{
	gfx_clear();
	if (wp.texture.handle) {
		Rectangle r = RECT(0, 0, wp.texture.w, wp.texture.h);
		gfx_render_texture(&wp.texture, &r);
	}
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (!p->texture.handle) {
			gfx_init_texture_with_color(&p->texture, p->rect.w, p->rect.h, p->color);
		}
		gfx_render_texture(&p->texture, &p->rect);
		if (p->text.texture.handle) {
			gfx_render_texture(&p->text.texture, &p->rect);
		}
	}
}

void sprite_flip(void)
{
	gfx_swap();
	sact_dirty = false;
}

struct texture *sprite_get_texture(struct sact_sprite *sp)
{
	if (!sp->texture.handle) {
		gfx_init_texture_with_color(&sp->texture, sp->rect.w, sp->rect.h, sp->color);
	}
	return &sp->texture;
}

int sprite_set_wp(int cg_no)
{
	if (!cg_no) {
		gfx_delete_texture(&wp.texture);
		return 1;
	}

	struct cg *cg = cg_load(ald[ALDFILE_CG], cg_no - 1);
	if (!cg)
		return 0;

	gfx_delete_texture(&wp.texture);
	gfx_init_texture_with_cg(&wp.texture, cg);
	cg_free(cg);
	scene_dirty();
	return 1;
}

int sprite_set_wp_color(int r, int g, int b)
{
	gfx_set_clear_color(r, g, b, 255);
	scene_dirty();
	return 1;
}


int sprite_set_cg(struct sact_sprite *sp, int cg_no)
{
	struct cg *cg = cg_load(ald[ALDFILE_CG], cg_no - 1);
	if (!cg)
		return 0;
	gfx_init_texture_with_cg(&sp->texture, cg);
	sp->rect.w = cg->metrics.w;
	sp->rect.h = cg->metrics.h;
	sp->cg_no = cg_no;
	cg_free(cg);
	sprite_dirty(sp);
	return 1;
}

void sprite_init(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a)
{
	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = (a >= 0 ? a : 255) };
	sp->rect.w = w;
	sp->rect.h = h;
	if (!sp->show) {
		sp->show = true;
		sprite_register(sp);
	}
	gfx_delete_texture(&sp->texture);
	sprite_dirty(sp);
}

int sprite_get_max_z(void)
{
	if (TAILQ_EMPTY(&sprite_list))
		return 0;
	return TAILQ_LAST(&sprite_list, listhead)->z;
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

void sprite_set_z(struct sact_sprite *sp, int z)
{
	sp->z = z;
	if (sp->show) {
		sprite_unregister(sp);
		sprite_register(sp);
	}
}

int sprite_get_blend_rate(struct sact_sprite *sp)
{
	return sprite_get_texture(sp)->alpha_mod;
}

void sprite_set_blend_rate(struct sact_sprite *sp, int rate)
{
	sprite_get_texture(sp)->alpha_mod = max(0, min(255, rate));
	sprite_dirty(sp);
}

void sprite_set_show(struct sact_sprite *sp, bool show)
{
	if (show == sp->show)
		return;
	sp->show = show;
	sprite_dirty(sp);
	if (show)
		sprite_register(sp);
	else
		sprite_unregister(sp);
}

int sprite_set_draw_method(struct sact_sprite *sp, int method)
{
	if (method < 0 || method >= NR_DRAW_METHODS) {
		WARNING("unknown draw method: %d", method);
		return 0;
	}
	sprite_get_texture(sp)->draw_method = method;
	sprite_dirty(sp);
	return 1;
}

int sprite_get_draw_method(struct sact_sprite *sp)
{
	return sprite_get_texture(sp)->draw_method;
}

int sprite_exists_alpha(struct sact_sprite *sp)
{
	return sp->texture.has_alpha;
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
	sp->text.pos = (Point) { .x = x, .y = y };
	sprite_dirty(sp);
}

void sprite_text_draw(struct sact_sprite *sp, struct string *text, struct text_metrics *tm)
{
	if (!sp->text.texture.handle) {
		SDL_Color c;
		if (tm->outline_left || tm->outline_right || tm->outline_up || tm->outline_down)
			c = tm->outline_color;
		else
			c = tm->color;
		c.a = 0;
		gfx_init_texture_with_color(&sp->text.texture, sp->rect.w, sp->rect.h, c);
	}

	sp->text.pos.x += gfx_render_text(&sp->text.texture, sp->text.pos, text->text, tm, sp->text.char_space);
	sprite_dirty(sp);
}

void sprite_text_clear(struct sact_sprite *sp)
{
	gfx_delete_texture(&sp->text.texture);
	sprite_dirty(sp);
}

void sprite_text_home(struct sact_sprite *sp, possibly_unused int size)
{
	// FIXME: do something with nTextSize
	sp->text.pos = sp->text.home;
	sprite_dirty(sp);
}

void sprite_text_new_line(struct sact_sprite *sp, int size)
{
	sp->text.pos = POINT(sp->text.home.x, sp->text.pos.y + size + sp->text.line_space);
	sprite_dirty(sp);
}

void sprite_text_copy(struct sact_sprite *dsp, struct sact_sprite *ssp)
{
	sprite_text_clear(dsp);
	if (ssp->text.texture.handle) {
		SDL_Color c = { 0, 0, 0, 0 };
		gfx_init_texture_with_color(&dsp->text.texture, dsp->rect.w, dsp->rect.h, c);
		gfx_copy(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
		gfx_copy_amap(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
	}
	dsp->text.home = ssp->text.home;
	dsp->text.pos = ssp->text.pos;
	dsp->text.char_space = ssp->text.char_space;
	dsp->text.line_space = ssp->text.line_space;
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
