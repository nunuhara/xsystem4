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
#include "xsystem4.h"

// Rendered glyph: pixels stored in GPU memory.
struct fnl_render_glyph {
	float advance;
	Texture texture;
};

// Bitmap glyph: pixels stored in normal memory to be used when creating
// fnl_render_glyph objects.
struct fnl_bitmap_glyph {
	unsigned width;
	unsigned height;
	unsigned advance;
	uint8_t *pixels;
};

// Object representing a bitmap font size.
struct fnl_bitmap {
	struct fnl_font_face *face;
	struct fnl_bitmap_glyph **glyphs;
	unsigned nr_glyphs;
};

// Object representing a rendered font size.
struct fnl_render_size {
	struct fnl_bitmap *bitmap;
	float size;
	unsigned denominator;
	struct fnl_render_glyph **glyphs;
	unsigned nr_glyphs;
};

// Object representing a font for rendering.
struct fnl_render_font {
	struct fnl_bitmap *bitmaps;
	unsigned nr_bitmaps;
	struct fnl_render_size *sizes;
	unsigned nr_sizes;
};

static struct fnl *fontlib = NULL;
static struct fnl_render_font *fonts = NULL;
static unsigned nr_fonts = 0;

void fnl_renderer_init(const char *path)
{
	if (!(fontlib = fnl_open(path)))
		ERROR("Error opening font library '%s'", display_utf0(path));
	if (fontlib->nr_fonts < 1)
		ERROR("Font library doesn't contain any fonts");

	fonts = xcalloc(fontlib->nr_fonts, sizeof(struct fnl_render_font));
	nr_fonts = fontlib->nr_fonts;

	for (unsigned i = 0; i < nr_fonts; i++) {
		struct fnl_font *lib_font = &fontlib->fonts[i];
		struct fnl_render_font *font = &fonts[i];

		font->nr_bitmaps = lib_font->nr_faces;
		font->bitmaps = xcalloc(font->nr_bitmaps, sizeof(struct fnl_bitmap));
		for (unsigned i = 0; i < font->nr_bitmaps; i++) {
			struct fnl_bitmap *bitmap = &font->bitmaps[i];
			bitmap->face = &lib_font->faces[i];
			bitmap->nr_glyphs = bitmap->face->nr_glyphs;
			bitmap->glyphs = xcalloc(bitmap->nr_glyphs, sizeof(struct fnl_bitmap_glyph*));
		}

		font->nr_sizes = font->nr_bitmaps * 12;
		font->sizes = xcalloc(font->nr_sizes, sizeof(struct fnl_render_size));
		for (unsigned i = 0; i < font->nr_bitmaps; i++) {
			struct fnl_bitmap *bitmap = &font->bitmaps[i];
			struct fnl_render_size *sizes = &font->sizes[i*12];
			for (unsigned i = 0; i < 12; i++) {
				sizes[i].bitmap = bitmap;
				sizes[i].size = bitmap->face->height / (float)(i+1);
				sizes[i].denominator = i + 1;
				sizes[i].nr_glyphs = bitmap->nr_glyphs;
				sizes[i].glyphs = xcalloc(sizes[i].nr_glyphs, sizeof(struct fnl_render_glyph*));
			}
		}
	}
}

static void fnl_free_font(struct fnl_render_font *font)
{
	for (unsigned i = 0; i < font->nr_bitmaps; i++) {
		for (unsigned g = 0; g < font->bitmaps[i].nr_glyphs; g++) {
			if (!font->bitmaps[i].glyphs[g])
				continue;
			free(font->bitmaps[i].glyphs[g]->pixels);
			free(font->bitmaps[i].glyphs[g]);
		}
		free(font->bitmaps[i].glyphs);
	}
	free(font->bitmaps);

	for (unsigned i = 0; i < font->nr_sizes; i++) {
		for (unsigned g = 0; g < font->sizes[i].nr_glyphs; g++) {
			if (!font->sizes[i].glyphs[g])
				continue;
			gfx_delete_texture(&font->sizes[i].glyphs[g]->texture);
			free(font->sizes[i].glyphs[g]);
		}
		free(font->sizes[i].glyphs);
	}
	free(font->sizes);
}

void fnl_renderer_fini(void)
{
	for (int i = 0; i < nr_fonts; i++) {
		fnl_free_font(&fonts[i]);
	}
	free(fonts);
	fonts = NULL;
	nr_fonts = 0;

	fnl_free(fontlib);
	fontlib = NULL;
}

static unsigned get_font_type(int type)
{
	unsigned font = type - 256;
	if (font >= nr_fonts) {
		WARNING("Invalid font type: %d", type);
		// XXX: Really not sure why this is how it works...
		font = type == 1 ? 2 : 1;
	}
	return font;
}

static struct fnl_render_size *_fnl_get_actual_font_size(int font_type, float size)
{
	struct fnl_render_font *font = &fonts[get_font_type(font_type)];

	float min_diff = 9999;
	struct fnl_render_size *closest = &font->sizes[0];
	for (unsigned i = 0; i < font->nr_sizes; i++) {
		float diff = fabsf(font->sizes[i].size - size);
		if (diff < min_diff) {
			min_diff = diff;
			closest = &font->sizes[i];
		}
	}
	return closest;
}

static struct fnl_render_size *_fnl_get_actual_font_size_round_down(int font_type, float size)
{
	struct fnl_render_font *font = &fonts[get_font_type(font_type)];

	float min_diff = 9999;
	struct fnl_render_size *closest = &font->sizes[0];
	for (unsigned i = 0; i < font->nr_sizes; i++) {
		if (font->sizes[i].size > size)
			continue;
		float diff = fabsf(font->sizes[i].size - size);
		if (diff < min_diff) {
			min_diff = diff;
			closest = &font->sizes[i];
		}
	}
	return closest;
}

float fnl_get_actual_font_size(int font_type, float size)
{
	return _fnl_get_actual_font_size(font_type, size)->size;
}

float fnl_get_actual_font_size_round_down(int font_type, float size)
{
	return _fnl_get_actual_font_size_round_down(font_type, size)->size;
}

static struct fnl_bitmap_glyph *fnl_get_bitmap_glyph(struct fnl_bitmap *bitmap, unsigned index)
{
	if (bitmap->glyphs[index])
		return bitmap->glyphs[index];

	unsigned long data_size;
	uint8_t *data = fnl_glyph_data(fontlib, &bitmap->face->glyphs[index], &data_size);

	const int glyph_w = (data_size*8) / bitmap->face->height;
	const int glyph_h = bitmap->face->height;

	struct fnl_bitmap_glyph *out = xmalloc(sizeof(struct fnl_bitmap_glyph));
	out->width = glyph_w;
	out->height = glyph_h;
	out->advance = bitmap->face->glyphs[index].real_width;
	out->pixels = xmalloc(glyph_w * glyph_h);

	// expand 1-bit bitmap to 8-bit
	for (unsigned i = 0; i < glyph_w * glyph_h; i++) {
		unsigned row = (glyph_h - 1) - i / glyph_w;
		unsigned col = i % glyph_w;
		bool on = data[i/8] & (1 << (7 - i % 8));
		out->pixels[row*glyph_w + col] = on ? 255 : 0;
	}

	free(data);
	bitmap->glyphs[index] = out;
	return out;
}

static struct fnl_render_glyph *fnl_render_glyph(struct fnl_render_size *size, uint16_t code)
{
	unsigned int index = fnl_char_to_index(code);
	if (index >= size->nr_glyphs)
		return NULL;
	if (size->glyphs[index])
		return size->glyphs[index];
	if (!size->bitmap->face->glyphs[index].data_pos)
		return NULL;

	struct fnl_bitmap_glyph *fullsize = fnl_get_bitmap_glyph(size->bitmap, index);
	struct fnl_render_glyph *rendered = xmalloc(sizeof(struct fnl_render_glyph));
	rendered->advance = (float)fullsize->advance / (float)size->denominator;
	const unsigned width = fullsize->width / size->denominator;
	const unsigned height = fullsize->height / size->denominator;

	// Sample each pixel in `size->denominator`-sized blocks and compute the average.
	// TODO: no need to sample every single pixel; 4 should be fine?
	uint8_t *pixels = xmalloc(width * height);
	unsigned *acc = xcalloc(width * height, sizeof(unsigned));
	for (unsigned i = 0; i < width * height; i++) {
		unsigned dst_row = i / width;
		unsigned dst_col = i % width;
		for (unsigned r = 0; r < size->denominator; r++) {
			unsigned src_row = dst_row * size->denominator + r;
			for (unsigned c = 0; c < size->denominator; c++) {
				unsigned src_col = dst_col * size->denominator + c;
				acc[i] += fullsize->pixels[src_row*fullsize->width + src_col];
			}
		}
		pixels[i] = acc[i] / (size->denominator * size->denominator);
	}
	gfx_init_texture_amap(&rendered->texture, width, height, pixels, (SDL_Color){0,0,0,0});
	size->glyphs[index] = rendered;

	free(pixels);
	free(acc);
	return rendered;
}

float fnl_draw_text(struct text_style *ts, Texture *dst, float x, int y, char *text)
{
	if (!ts->font_size) {
		ts->font_size = _fnl_get_actual_font_size(ts->font_type, ts->size);
	}

	int len = sjis_count_char(text);
	uint16_t *chars = xmalloc(len * sizeof(uint16_t));
	struct fnl_render_glyph **glyphs = xmalloc(len * sizeof(struct fnl_render_glyph*));
	for (int i = 0; i < len; i++) {
		chars[i] = sjis_code(text);
		glyphs[i] = fnl_render_glyph(ts->font_size, chars[i]);
		text = sjis_skip_char(text);
	}

	float original_x = x;
	if (ts->edge_width > 0.1) {
		for (int i = 0; i < len; i++) {
			float scale_x = chars[i] == ' ' ? ts->space_scale_x : ts->scale_x;
			gfx_draw_glyph(dst, x, y, &glyphs[i]->texture, ts->edge_color, scale_x, ts->edge_width);
			x += glyphs[i]->advance * scale_x + ts->font_spacing;
		}
	}

	x = original_x;
	for (int i = 0; i < len; i++) {
		float scale_x = chars[i] == ' ' ? ts->space_scale_x : ts->scale_x;
		gfx_draw_glyph(dst, x, y, &glyphs[i]->texture, ts->color, scale_x, ts->bold_width);
		x += glyphs[i]->advance * scale_x + ts->font_spacing;
	}

	free(chars);
	free(glyphs);
	return x - original_x;
}

static float _fnl_size_char(struct fnl_render_size *size, uint16_t code)
{
	unsigned index = fnl_char_to_index(code);
	if (index > size->nr_glyphs)
		return 0.0;
	if (!size->bitmap->face->glyphs[index].data_pos)
		return 0.0;
	return (float)size->bitmap->face->glyphs[index].real_width / (float)size->denominator;
}

float fnl_size_char(struct text_style *ts, uint16_t code)
{
	if (!ts->font_size) {
		ts->font_size = _fnl_get_actual_font_size(ts->font_type, ts->size);
	}
	return _fnl_size_char(ts->font_size, code);
}

float fnl_size_text(struct text_style *ts, char *text)
{
	if (!ts->font_size) {
		ts->font_size = _fnl_get_actual_font_size(ts->font_type, ts->size);
	}

	float size = 0.0;
	int len = sjis_count_char(text);
	for (int i = 0; i < len; i++) {
		size += _fnl_size_char(ts->font_size, sjis_code(text));
		text = sjis_skip_char(text);
	}
	return size;
}
