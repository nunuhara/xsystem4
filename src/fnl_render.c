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
#include <math.h>
#include "gfx/gfx.h"
#include "gfx/types.h"
#include "system4.h"
#include "system4/fnl.h"
#include "system4/utfsjis.h"
#include "gfx/gfx.h"

static void draw_glyph(Texture *dst, float x, int y, Texture *glyph, float scale_x, float scale_y)
{
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, roundf(x), y, glyph->w, glyph->h);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	mat4 mw_transform = MAT4(
			glyph->w * scale_x, 0,                  0, 0,
			0,                  glyph->h * scale_y, 0, 0,
			0,                  0,                  1, 0,
			0,                  0,                  0, 1);
	mat4 wv_transform = WV_TRANSFORM(glyph->w, glyph->h);
	struct gfx_render_job job = {
		.texture = glyph->handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = glyph
	};
	gfx_render(&job);

	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

struct fnl_glyph_texture {
	Texture t;
	SDL_Color c;
};

enum glyph_texture_type {
	FONT_TEXTURE = 0,
	EDGE_TEXTURE = 1
};

static Texture *get_glyph_texture(struct fnl_rendered_glyph *glyph, SDL_Color *c,
		enum glyph_texture_type type)
{
	if (!glyph->data) {
		glyph->data = xcalloc(2, sizeof(struct fnl_glyph_texture));
	}

	struct fnl_glyph_texture *g = glyph->data;
	g += type;

	if (!g->t.handle) {
		gfx_init_texture_amap(&g->t, glyph->width, glyph->height, glyph->pixels, *c);
		g->c = *c;
		return &g->t;
	}

	if (g->c.r != c->r || g->c.g != c->g || g->c.b != c->b) {
		gfx_fill(&g->t, 0, 0, g->t.w, g->t.h, c->r, c->g, c->b);
		g->c = *c;
	}
	return &g->t;
}

static void draw_glyph_outline(Texture *dst, float x, int y, Texture *glyph, struct text_style *ts)
{
	float edge_width = roundf(ts->edge_width);
	draw_glyph(dst, x-edge_width, y, glyph, ts->scale_x, 1.0);
	draw_glyph(dst, x+edge_width, y, glyph, ts->scale_x, 1.0);
	draw_glyph(dst, x, y-edge_width, glyph, ts->scale_x, 1.0);
	draw_glyph(dst, x, y+edge_width, glyph, ts->scale_x, 1.0);
}

float fnl_draw_text(struct fnl *fnl, struct text_style *ts, Texture *dst, float x, int y, char *text)
{
	int len = sjis_count_char(text);
	struct fnl_rendered_glyph **glyphs = xmalloc(len * sizeof(struct fnl_rendered_glyph*));
	for (int i = 0; i < len; i++) {
		glyphs[i] = fnl_render_glyph(ts->font_size, sjis_code(text));
		text = sjis_skip_char(text);
	}

	float original_x = x;
	if (ts->edge_width > 0.5) {
		for (int i = 0; i < len; i++) {
			Texture *t = get_glyph_texture(glyphs[i], &ts->edge_color, EDGE_TEXTURE);
			draw_glyph_outline(dst, x, y, t, ts);
			x += glyphs[i]->advance * ts->scale_x;
		}
	}

	x = original_x;
	for (int i = 0; i < len; i++) {
		Texture *t = get_glyph_texture(glyphs[i], &ts->color, FONT_TEXTURE);
		draw_glyph(dst, x, y, t, ts->scale_x, 1.0);
		x += glyphs[i]->advance * ts->scale_x;
	}

	free(glyphs);
	return x - original_x;
}

float fnl_size_text(struct fnl *fnl, struct text_style *ts, char *text)
{
	struct fnl_font_face *font = ts->font_size->face;
	int width = 0;
	int len = sjis_count_char(text);
	for (int i = 0; i < len; i++) {
		width += fnl_get_glyph(font, sjis_code(text))->real_width;
		text = sjis_skip_char(text);
	}

	return ((float)width / (float)ts->font_size->denominator) * ts->scale_x;
}

void free_textures(void *data)
{
	struct fnl_glyph_texture *textures = data;
	gfx_delete_texture(&textures[FONT_TEXTURE].t);
	gfx_delete_texture(&textures[EDGE_TEXTURE].t);
	free(textures);
}

void fnl_renderer_free(struct fnl *fnl)
{
	fnl_free(fnl, free_textures);
}
