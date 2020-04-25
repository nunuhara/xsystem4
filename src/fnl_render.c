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

#include "gfx/gfx.h"
#include "gfx/types.h"
#include "system4.h"
#include "system4/fnl.h"
#include "system4/utfsjis.h"
#include "gfx/gfx.h"

static void set_pixel(uint8_t *p, int w, int h, int x, int y, SDL_Color c)
{
	if (x >= w || y >= h)
		return;
	p[y*w*4 + x*4 + 0] = c.r;
	p[y*w*4 + x*4 + 1] = c.g;
	p[y*w*4 + x*4 + 2] = c.b;
	p[y*w*4 + x*4 + 3] = 255;
}

static void render_outline(struct fnl_font_inst *font, uint8_t *surf, struct fnl_glyph *glyph, int x, int w, int h)
{
	const int span = w * 4;
	const int glyph_w = (glyph->data_size*8) / h;
	const int size = 1.0 / font->scale * font->outline_size;


	uint32_t p = 0;
	for (int row = h - 1; row >= 0; row--) {
		uint8_t *pos = surf + row*span + x*4;
		for (int col = 0; col < glyph_w; col++, p++, pos += 4) {
			bool on = glyph->data[p/8] & (1 << (7 - p % 8));
			if (!on)
				continue;
			int start_x = x + col - size;
			int start_y = row - size;
			for (int ol_row = 0; ol_row < size*2; ol_row++) {
				for (int ol_col = 0; ol_col < size*2; ol_col++) {
					set_pixel(surf, w, h, start_x + ol_col, start_y + ol_row, font->outline_color);
				}
			}
		}
	}
}

static void render_glyph(struct fnl_font_inst *font, uint8_t *surf, struct fnl_glyph *glyph, int x, int w, int h)
{
	const int span = w * 4;
	const int glyph_w = (glyph->data_size*8) / h;

	if (font->outline_size)
		render_outline(font, surf, glyph, x, w, h);

	uint32_t p = 0;
	for (int row = h - 1; row >= 0; row--) {
		uint8_t *pos = surf + row*span + x*4;
		for (int col = 0; col < glyph_w; col++, p++) {
			bool on = glyph->data[p/8] & (1 << (7 - p % 8));
			if (on) {
				set_pixel(surf, w, h, x+col, row, font->color);
			}
			pos += 4;
		}
	}
}

static void render_text(struct fnl_font_inst *font, Texture *dst, char *text)
{
	// create array of fnl_glyph
	const int height = font->font->height;
	int len = sjis_count_char(text);
	struct fnl_glyph **glyphs = xcalloc(len, sizeof(struct fnl_glyph*));
	for (int i = 0; i < len; i++) {
		glyphs[i] = fnl_get_glyph(font->font, sjis_code(text));
		text = sjis_skip_char(text);
	}

	// calculate width/height of text surface
	int width = 0;
	for (int i = 0; i < len; i++) {
		//width += glyphs[i]->real_width;
		width += (glyphs[i]->data_size*8)/height;
	}

	// allocate memory for surface
	uint8_t *surf = xcalloc(4, width * height);

	// render glyphs to surface
	int x = 0;
	for (int i = 0; i < len; i++) {
		render_glyph(font, surf, glyphs[i], x, width, height);
		x += glyphs[i]->real_width;
	}

	// create texture from surface
	gfx_init_texture_with_pixels(dst, width, height, surf, GL_RGBA);
}

static void draw_text(Texture *dst, Texture *glyph, int x, int y, float scale)
{
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, x, y, glyph->w, glyph->h);

	GLfloat mw_transform[16] = {
		[0]  = glyph->w * scale,
		[5]  = glyph->h * scale,
		[10] = 1,
		[15] = 1
	};
	GLfloat wv_transform[16] = {
		[0]  =  2.0 / glyph->w,
		[5]  =  2.0 / glyph->h,
		[10] =  2,
		[12] = -1,
		[13] = -1,
		[15] =  1
	};
	struct gfx_render_job job = {
		.texture = glyph->handle,
		.world_transform = mw_transform,
		.view_transform = wv_transform,
		.data = glyph
	};
	gfx_render(&job);

	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

int fnl_draw_text(struct fnl_font_inst *font, Texture *dst, int x, int y, char *text)
{
	Texture rendered;
	render_text(font, &rendered, text);

	//glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	draw_text(dst, &rendered, x, y, font->scale);
	//glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

	int w = rendered.w;
	gfx_delete_texture(&rendered);
	return w;
}
