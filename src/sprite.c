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
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"

bool sact_dirty = true;

TAILQ_HEAD(listhead, sact_sprite) sprite_list =
	TAILQ_HEAD_INITIALIZER(sprite_list);

void sprite_free(struct sact_sprite *sp)
{
	scene_unregister_sprite(&sp->sp);
	gfx_delete_texture(&sp->texture);
	gfx_delete_texture(&sp->text.texture);
	memset(sp, 0, sizeof(struct sact_sprite));
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

static void sprite_render(struct sprite *_sp)
{
	struct sact_sprite *sp = (struct sact_sprite*)_sp;
	sprite_init_texture(sp);
	gfx_render_texture(&sp->texture, &sp->rect);
	if (sp->text.texture.handle) {
		gfx_render_texture(&sp->text.texture, &sp->rect);
	}
}

struct texture *sprite_get_texture(struct sact_sprite *sp)
{
	sprite_init_texture(sp);
	return &sp->texture;
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
	sprite_dirty(sp);
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
	void *pixels = gfx_get_pixels(sprite_get_texture(sp));

	struct cg cg = {
		.type = ALCG_UNKNOWN,
		.metrics = {
			.w = sp->rect.w,
			.h = sp->rect.h,
			.bpp = 24,
			.has_pixel = sp->sp.has_pixel,
			.has_alpha = sp->sp.has_alpha,
			.pixel_pitch = sp->rect.w * 3,
			.alpha_pitch = 1
		},
		.pixels = pixels
	};
	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("Failed to open %s: %s", display_utf0(path), strerror(errno));
		free(pixels);
		return 0;
	}
	int r = cg_write(&cg, ALCG_QNT, fp);
	fclose(fp);
	free(pixels);
	return r;
}

/*
 * Attach pixel data to a sprite. The texture is initialized lazily.
 */
void sprite_init(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a)
{
	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = (a >= 0 ? a : 255) };
	sp->rect.w = w;
	sp->rect.h = h;
	gfx_delete_texture(&sp->texture);

	sp->sp.has_pixel = true;
	sp->sp.has_alpha = a >= 0;
	sp->sp.render = sprite_render;
	sprite_dirty(sp);
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

int sprite_get_blend_rate(struct sact_sprite *sp)
{
	return sprite_get_texture(sp)->alpha_mod;
}

void sprite_set_blend_rate(struct sact_sprite *sp, int rate)
{
	sprite_get_texture(sp)->alpha_mod = max(0, min(255, rate));
	sprite_dirty(sp);
}

int sprite_set_draw_method(struct sact_sprite *sp, enum draw_method method)
{
	sprite_get_texture(sp)->draw_method = method;
	sprite_dirty(sp);
	return 1;
}

enum draw_method sprite_get_draw_method(struct sact_sprite *sp)
{
	return sprite_get_texture(sp)->draw_method;
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
		gfx_init_texture_rgba(&sp->text.texture, sp->rect.w, sp->rect.h, c);
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
		gfx_init_texture_rgba(&dsp->text.texture, dsp->rect.w, dsp->rect.h, c);
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
