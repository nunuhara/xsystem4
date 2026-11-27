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
#include <ctype.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_ADVANCES_H
#include FT_TRUETYPE_TABLES_H
#include <SDL.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/font.h"
#include "xsystem4.h"

#define MAX_FONT_SIZES 512

struct font_ft {
	struct font super;
	FT_Face font;
#if defined(ALICEHUB_IOS)
	struct font_ft *fallback;
#endif
	unsigned current_size;
	unsigned nr_sizes;
	struct font_size sizes[MAX_FONT_SIZES];
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

	if (font->nr_sizes + 1 >= MAX_FONT_SIZES) {
		int best = 0;
		float best_diff = fabsf(font->sizes[0].size - size);
		for (unsigned i = 1; i < font->nr_sizes; i++) {
			float d = fabsf(font->sizes[i].size - size);
			if (d < best_diff) {
				best_diff = d;
				best = (int)i;
			}
		}
		return &font->sizes[best];
	}
	struct font_size *fs = &font->sizes[font->nr_sizes++];
	fs->size = size;
	float desc_ratio = 0.15f;
	TT_OS2 *os2 = FT_Get_Sfnt_Table(font->font, FT_SFNT_OS2);
	if (os2 && font->font->units_per_EM > 0 && os2->sTypoAscender > 0
			&& os2->sTypoAscender < (FT_Short)font->font->units_per_EM) {
		desc_ratio = (float)(font->font->units_per_EM - os2->sTypoAscender)
			/ (float)font->font->units_per_EM;
	}
	fs->y_offset = -roundf(size * desc_ratio);
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
	int block_width = max(half_width ? size/2 : size, max(0, bitmap_left) + (int)glyph->width);
	int block_height = max(1, size + size/2);
	block_width = max(1, block_width);
	int width = block_width + GLYPH_BORDER_SIZE*2;
	int height = block_height + GLYPH_BORDER_SIZE*2;
	int off_x = max(0, bitmap_left) + GLYPH_BORDER_SIZE;
	int off_y = max(0, size - bitmap_top) + GLYPH_BORDER_SIZE;
	uint8_t *bitmap = xcalloc(1, (size_t)width * height);

	// Expand the glyph bitmap to block size using abs(pitch) row stride
	if (glyph->buffer && glyph->rows > 0 && glyph->width > 0) {
		size_t stride = (size_t)abs(glyph->pitch);
		for (int row = 0; row < (int)glyph->rows && row + off_y < height; row++) {
			const uint8_t *src_row = glyph->pitch >= 0
				? glyph->buffer + (size_t)row * stride
				: glyph->buffer + (size_t)((int)glyph->rows - 1 - row) * stride;
			for (int col = 0; col < (int)glyph->width && col + off_x < width; col++) {
				bitmap[(row+off_y)*width + (col+off_x)] = src_row[col];
			}
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

static struct font_ft *ft_font_for_char(struct font_ft *font, uint32_t code)
{
#if defined(ALICEHUB_IOS)
	if (font->fallback && !FT_Get_Char_Index(font->font, code)
			&& FT_Get_Char_Index(font->fallback->font, code))
		return font->fallback;
#endif
	return font;
}

static bool ft_font_get_glyph(struct font_size *size, struct glyph *glyph, uint32_t code, enum font_weight weight)
{
	Texture *t = &glyph->t[weight];

	// render bitmap
	bool half_width = is_half_width(code);
	struct font_ft *primary = (struct font_ft*)size->font;
	struct font_ft *font = ft_font_for_char(primary, code);
	ft_font_set_size(font, size->size);
	FT_Error err = FT_Load_Char(font->font, code, FT_LOAD_RENDER);
#if defined(ALICEHUB_IOS)
	if (err && primary->fallback && font != primary->fallback
			&& FT_Get_Char_Index(primary->fallback->font, code)) {
		font = primary->fallback;
		ft_font_set_size(font, size->size);
		err = FT_Load_Char(font->font, code, FT_LOAD_RENDER);
	}
#endif
	if (err) {
		WARNING("Failed to load glyph for codepoint 0x%x", code);
		return false;
	}
	if (!config.proportional_text && half_width && size->size >= 4) {
		int cell_w = size->size / 2;
		float adv_px = font->font->glyph->advance.x / 64.0f;
		if (cell_w > 0 && adv_px > (float)cell_w + 0.5f) {
			unsigned x_ppem = (unsigned)floorf((float)size->size * (float)cell_w / adv_px);
			if (x_ppem < 1)
				x_ppem = 1;
			if (!FT_Set_Pixel_Sizes(font->font, x_ppem, size->size)) {
				font->current_size = 0;
				if (FT_Load_Char(font->font, code, FT_LOAD_RENDER)) {
					ft_font_set_size(font, size->size);
					if (FT_Load_Char(font->font, code, FT_LOAD_RENDER)) {
						WARNING("Failed to reload glyph for codepoint 0x%x", code);
						return false;
					}
				}
			}
		}
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
		if (tmp.buffer) {
			size_t stride = (size_t)abs(tmp.pitch);
			for (unsigned int r = 0; r < tmp.rows; r++) {
				uint8_t *row_ptr = tmp.pitch >= 0
					? tmp.buffer + (size_t)r * stride
					: tmp.buffer + (size_t)(tmp.rows - 1 - r) * stride;
				for (unsigned int c = 0; c < tmp.width; c++) {
					if (row_ptr[c])
						row_ptr[c] = 255;
				}
			}
		}
		glyph->rect = init_glyph_texture(t, &tmp, font->font->glyph->bitmap_left,
				font->font->glyph->bitmap_top, size->size, half_width);
		FT_Bitmap_Done(ft_lib, &tmp);
	} else {
		WARNING("Font returned glyph with unsupported pixel mode");
		return false;
	}

	glyph->advance = config.proportional_text ? font->font->glyph->advance.x / 64.f
	                         : (half_width ? size->size / 2 : size->size);
	return true;
}

static float ft_font_size_char(struct font_size *size, uint32_t code)
{
	return is_half_width(code) ? size->size / 2 : size->size;
}

static float ft_font_size_char_kerning(struct font_size *size, uint32_t code,
		uint32_t code_next)
{
	struct font_ft *primary = (struct font_ft*)size->font;
	struct font_ft *font = ft_font_for_char(primary, code);
	ft_font_set_size(font, size->size);

	FT_UInt index = FT_Get_Char_Index(font->font, code);

	FT_Fixed advance = 0;
	FT_Error err = FT_Get_Advance(font->font, index, FT_LOAD_DEFAULT, &advance);
#if defined(ALICEHUB_IOS)
	if (err && primary->fallback && font != primary->fallback) {
		FT_UInt fb_index = FT_Get_Char_Index(primary->fallback->font, code);
		if (fb_index) {
			font = primary->fallback;
			index = fb_index;
			ft_font_set_size(font, size->size);
			err = FT_Get_Advance(font->font, index, FT_LOAD_DEFAULT, &advance);
		}
	}
#endif
	if (err)
		return 0.f;

	if (!FT_HAS_KERNING(font->font) || ft_font_for_char(primary, code_next) != font) {
		return advance / 65536.f;
	}

	FT_UInt next_index = FT_Get_Char_Index(font->font, code_next);
	FT_Vector delta = {0, 0};
	FT_Get_Kerning(font->font, index, next_index, FT_KERNING_DEFAULT, &delta);

	return (advance / 65536.f) + (delta.x / 64.f);
}

#ifdef __ANDROID__
static uint8_t *load_asset_chunked(const char *path, size_t *size_out)
{
	SDL_RWops *rw = SDL_RWFromFile(path, "rb");
	if (!rw)
		return NULL;
	Sint64 total_size = SDL_RWsize(rw);
	if (total_size <= 0) {
		SDL_RWclose(rw);
		return NULL;
	}
	uint8_t *buf = malloc((size_t)total_size);
	if (!buf) {
		SDL_RWclose(rw);
		return NULL;
	}
	size_t offset = 0;
	const size_t chunk_size = 64 * 1024;
	while (offset < (size_t)total_size) {
		size_t to_read = (size_t)total_size - offset;
		if (to_read > chunk_size)
			to_read = chunk_size;
		size_t n = SDL_RWread(rw, buf + offset, 1, to_read);
		if (n == 0)
			break;
		offset += n;
	}
	SDL_RWclose(rw);
	if (offset != (size_t)total_size) {
		free(buf);
		return NULL;
	}
	*size_out = offset;
	return buf;
}
#endif

static bool is_sc_face_name(const char *family)
{
	if (!family)
		return false;
	return strstr(family, " SC") != NULL || strstr(family, "CN") != NULL;
}

static void select_gbk_ttc_face(const char *path, const FT_Byte *mem, FT_Long mem_size, FT_Face *face_io)
{
	FT_Face cur = *face_io;
	if ((!sys4_text_is_gbk && !sys4_has_mai_word_table()) || !cur || cur->num_faces <= 1)
		return;
	if (is_sc_face_name(cur->family_name) && strstr(cur->family_name, "Mono") != NULL)
		return;

	FT_Long num_faces = cur->num_faces;
	FT_Face fallback_sc = NULL;
	for (FT_Long i = 1; i < num_faces; i++) {
		FT_Face cand = NULL;
		FT_Error err = mem
			? FT_New_Memory_Face(ft_lib, mem, mem_size, i, &cand)
			: FT_New_Face(ft_lib, path, i, &cand);
		if (err || !cand)
			continue;
		if (cand->charmap && is_sc_face_name(cand->family_name)) {
			// Prefer the Mono SC face (0.5em half-width ASCII + full-width SC CJK,
			// matching Windows SimHei / MS Gothic fixed-cell metrics).
			if (strstr(cand->family_name, "Mono") != NULL) {
				if (fallback_sc)
					FT_Done_Face(fallback_sc);
				FT_Done_Face(cur);
				*face_io = cand;
				return;
			}
			if (!fallback_sc) {
				fallback_sc = cand;
				continue;
			}
		}
		FT_Done_Face(cand);
	}
	if (fallback_sc) {
		FT_Done_Face(cur);
		*face_io = fallback_sc;
	}
}

#if defined(ALICEHUB_IOS)
// Replaces *face_io with the face in a font collection whose PostScript name
// is ps_name. Returns false if the collection has no such face.
static bool select_named_ttc_face(const char *path, const char *ps_name, FT_Face *face_io)
{
	const char *cur_name = FT_Get_Postscript_Name(*face_io);
	if (cur_name && !strcmp(cur_name, ps_name))
		return true;
	FT_Long num_faces = (*face_io)->num_faces;
	for (FT_Long i = 1; i < num_faces; i++) {
		FT_Face cand = NULL;
		if (FT_New_Face(ft_lib, path, i, &cand) || !cand)
			continue;
		const char *name = FT_Get_Postscript_Name(cand);
		if (name && !strcmp(name, ps_name)) {
			FT_Done_Face(*face_io);
			*face_io = cand;
			return true;
		}
		FT_Done_Face(cand);
	}
	return false;
}

void ft_font_set_fallback(struct font *font, struct font *fallback)
{
	((struct font_ft*)font)->fallback = (struct font_ft*)fallback;
}

void ft_font_free(struct font *base)
{
	if (!base)
		return;
	struct font_ft *font = (struct font_ft*)base;
	ft_font_free(font->fallback ? &font->fallback->super : NULL);
	for (unsigned i = 0; i < font->nr_sizes; i++)
		gfx_font_size_fini(&font->sizes[i]);
	FT_Done_Face(font->font);
	free(font);
}

void ft_font_fini(void)
{
	if (ft_lib)
		FT_Done_FreeType(ft_lib);
	ft_lib = NULL;
}
#endif

static char *parse_font_spec(const char *spec, FT_Long *index)
{
	char *path = xstrdup(spec);
	*index = -1;

	char *colon = strrchr(path, ':');
	if (!colon || colon == path || !isdigit((unsigned char)colon[1]))
		return path;
	char *end;
	unsigned long n = strtoul(colon + 1, &end, 10);
	if (*end)
		return path;
	*index = (FT_Long)n;
	*colon = '\0';
	return path;
}

static struct font *load_font_file(const char *path, const char *ps_name, FT_Long index)
{
#ifndef ALICEHUB_IOS
	(void)ps_name;
#endif
	if (path[0] == '/' && !file_exists(path))
		return NULL;
#ifdef __ANDROID__
	if (path[0] == '/') {
		struct font_ft *font = xcalloc(1, sizeof(struct font_ft));
		if (FT_New_Face(ft_lib, path, index < 0 ? 0 : index, &font->font)) {
			free(font);
			return NULL;
		}
		if (index < 0) select_gbk_ttc_face(path, NULL, 0, &font->font);
		if (!font->font->charmap) {
			FT_Done_Face(font->font);
			free(font);
			return NULL;
		}
		font->super.get_size = ft_font_get_size;
		font->super.get_actual_size = ft_font_get_actual_size;
		font->super.get_actual_size_round_down = ft_font_get_actual_size;
		font->super.get_glyph = ft_font_get_glyph;
		font->super.size_char = ft_font_size_char;
		font->super.size_char_kerning = ft_font_size_char_kerning;
		return &font->super;
	}
#endif

	struct font_ft *font = xcalloc(1, sizeof(struct font_ft));

#ifdef __ANDROID__
	// On Android, path may be an asset name which FT_New_Face cannot read
	// directly, so read it into memory in 64 KB chunks.
	size_t size = 0;
	void *buf = load_asset_chunked(path, &size);
	if (!buf) {
		free(font);
		return NULL;
	}
	if (FT_New_Memory_Face(ft_lib, buf, size, index < 0 ? 0 : index, &font->font)) {
		free(buf);
		free(font);
		return NULL;
	}
	if (index < 0) select_gbk_ttc_face(path, buf, (FT_Long)size, &font->font);
#else
	if (!file_exists(path)) {
		free(font);
		return NULL;
	}
	if (FT_New_Face(ft_lib, path, index < 0 ? 0 : index, &font->font)) {
		free(font);
		return NULL;
	}
#if defined(ALICEHUB_IOS)
	if (ps_name) {
		if (!select_named_ttc_face(path, ps_name, &font->font)) {
			FT_Done_Face(font->font);
			free(font);
			return NULL;
		}
	} else
#endif
		if (index < 0) select_gbk_ttc_face(path, NULL, 0, &font->font);
#endif

	if (!font->font->charmap) {
		WARNING("Font '%s' doesn't contain unicode charmap", path);
		FT_Done_Face(font->font);
#ifdef __ANDROID__
		free(buf);
#endif
		free(font);
		return NULL;
	}

	font->super.get_size = ft_font_get_size;
	font->super.get_actual_size = ft_font_get_actual_size;
	font->super.get_actual_size_round_down = ft_font_get_actual_size;
	font->super.get_glyph = ft_font_get_glyph;
	font->super.size_char = ft_font_size_char;
	font->super.size_char_kerning = ft_font_size_char_kerning;
	return &font->super;
}

// path may end in "#<PostScript name>" to pick one face out of a .ttc collection.
struct font *ft_font_load(const char *spec)
{
	if (!spec || !*spec)
		return NULL;
	FT_Long index;
	char *path = parse_font_spec(spec, &index);
	const char *ps_name = NULL;
#if defined(ALICEHUB_IOS)
	char *sep = strrchr(path, '#');
	if (index < 0 && sep && sep != path && sep[1] && !file_exists(path)) {
		*sep = '\0';
		ps_name = sep + 1;
	}
#endif
	struct font *font = load_font_file(path, ps_name, index);
	free(path);
	return font;
}
