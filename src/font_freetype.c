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

#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H

#include "system4.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/font.h"
#include "xsystem4.h"

struct font_ft {
	struct font super;
	FT_Face font;
	unsigned current_size;
	unsigned nr_sizes;
	struct font_size *sizes;
};

static FT_Library ft_lib;

static bool is_half_width(uint32_t code)
{
	// ASCII characters
	if (code < 128)
		return true;
	// Latin letters
	if (0xbf <= code && code <= 0xff && code != 0xd7 && code != 0xf7)
		return true;
	// Halfwidth punctuation / katakana
	if (0xff61 <= code && code <= 0xff9f)
		return true;
	return false;
}

void ft_font_init(void)
{
	if (FT_Init_FreeType(&ft_lib)) {
		ERROR("Failed to initialize FreeType");
	}
}

static struct font_size *ft_font_get_size(struct font *_font, float size)
{
	struct font_ft *font = (struct font_ft*)_font;
	size = roundf(size);
	for (int i = 0; i < font->nr_sizes; i++) {
		if (fabsf(font->sizes[i].size - size) < 0.01) {
			return &font->sizes[i];
		}
	}

	font->sizes = xrealloc_array(font->sizes, font->nr_sizes, font->nr_sizes+1, sizeof(struct font_size));
	struct font_size *fs = &font->sizes[font->nr_sizes++];
	fs->size = size;
	fs->y_offset = -roundf(size * 0.15f);
	fs->font = _font;
	fs->glyph_table = ht_create(4096);
	return fs;
}

static float ft_font_get_actual_size(struct font *_, float size)
{
	return roundf(size);
}

// NOTE: we create glyph textures slightly larger than necessary so that outlines
//       aren't cut off at the texture boundary.
// FIXME: the outline rendering code should be fixed so this isn't necessary.
#define GLYPH_BORDER_SIZE 4

// Convert a glyph rendered by FreeType to a block-sized texture.
// Block size is size x 1.5*size (full-width) or size/2 x 1.5*size (half-width)
static Rectangle init_glyph_texture(Texture *dst, FT_Bitmap *glyph, int bitmap_left, int bitmap_top, int size, bool half_width)
{
	// calculate block size and offsets
	int block_width = (half_width ? size/2 : size);
	int block_height = size + size/2;
	int width = block_width + GLYPH_BORDER_SIZE*2;
	int height = block_height + GLYPH_BORDER_SIZE*2;
	int off_x = max(0, bitmap_left) + GLYPH_BORDER_SIZE;
	int off_y = max(0, size - bitmap_top) + GLYPH_BORDER_SIZE;
	uint8_t *bitmap = xcalloc(1, width * height);

	// Expand the glyph bitmap to block size
	for (int row = 0; row < glyph->rows && row + off_y < height; row++) {
		for (int col = 0; col < glyph->width && col + off_x < width; col++) {
			uint8_t p = glyph->buffer[row*glyph->width + col];
			bitmap[(row+off_y)*width + (col+off_x)] = p;
		}
	}

	// create texture from block-size bitmap
	gfx_init_texture_rmap(dst, width, height, bitmap);
	free(bitmap);

	return (Rectangle) {
		.x = GLYPH_BORDER_SIZE,
		.y = GLYPH_BORDER_SIZE,
		.w = block_width,
		.h = block_height
	};
}

static void ft_font_set_size(struct font_ft *font, unsigned size)
{
	if (font->current_size != size && FT_Set_Pixel_Sizes(font->font, 0, size)) {
		WARNING("Failed setting font size to %u", size);
	}
	font->current_size = size;
}

static bool ft_font_get_glyph(struct font_size *size, struct glyph *glyph, uint32_t code, enum font_weight weight)
{
	Texture *t = &glyph->t[weight];

	// render bitmap
	bool half_width = is_half_width(code);
	struct font_ft *font = (struct font_ft*)size->font;
	ft_font_set_size(font, size->size);
	if (FT_Load_Char(font->font, code, FT_LOAD_RENDER)) {
		WARNING("Failed to load glyph for codepoint 0x%x", code);
		return false;
	}
	if (weight != FONT_WEIGHT_NORMAL) {
		int bold_weight = weight == FONT_WEIGHT_HEAVY ? 128 : 64;
		FT_GlyphSlot_Own_Bitmap(font->font->glyph);
		FT_Bitmap_Embolden(ft_lib, &font->font->glyph->bitmap, bold_weight, 0);
	}

	// create texture from bitmap
	FT_Bitmap *bitmap = &font->font->glyph->bitmap;
	if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
		glyph->rect = init_glyph_texture(t, bitmap, font->font->glyph->bitmap_left,
				font->font->glyph->bitmap_top, size->size, half_width);
	} else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
		FT_Bitmap tmp;
		FT_Bitmap_New(&tmp);
		if (FT_Bitmap_Convert(ft_lib, &font->font->glyph->bitmap, &tmp, 1)) {
			WARNING("Failed to convert monochrome glyph to grayscale");
			FT_Bitmap_Done(ft_lib, &tmp);
			return false;
		}
		// round non-zero values up to 255 because FreeType doesn't
		for (int i = 0; i < tmp.rows * tmp.width; i++) {
			if (tmp.buffer[i])
				tmp.buffer[i] = 255;
		}
		glyph->rect = init_glyph_texture(t, &tmp, font->font->glyph->bitmap_left,
				font->font->glyph->bitmap_top, size->size, half_width);
		FT_Bitmap_Done(ft_lib, &tmp);
	} else {
		WARNING("Font returned glyph with unsupported pixel mode");
		return false;
	}

	glyph->advance = half_width ? size->size / 2 : size->size;
	return true;
}

static float ft_font_size_char(struct font_size *size, uint32_t code)
{
	return is_half_width(code) ? size->size / 2 : size->size;
}

struct font *ft_font_load(const char *path)
{
	struct font_ft *font = xcalloc(1, sizeof(struct font_ft));
	if (FT_New_Face(ft_lib, path, 0, &font->font)) {
		free(font);
		return NULL;
	}

	if (!font->font->charmap) {
		WARNING("Font '%s' doesn't contain unicode charmap", path);
		free(font);
		return NULL;
	}

	font->super.get_size = ft_font_get_size;
	font->super.get_actual_size = ft_font_get_actual_size;
	font->super.get_actual_size_round_down = ft_font_get_actual_size;
	font->super.get_glyph = ft_font_get_glyph;
	font->super.size_char = ft_font_size_char;
	return &font->super;
}


