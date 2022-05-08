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

#include <assert.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H

#include "system4.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/types.h"
#include "xsystem4.h"

/*
 * NOTE: There are two different text rendering APIs: SACT2 and DrawGraph.
 *
 *       SACT2 is a stateless API which uses the text_metrics structure.
 *       It is implemented via the single function gfx_render_text and
 *       supports bold and outlined font styles.
 *
 *       DrawGraph is a stateful API that uses the font_metrics structure.
 *       The various gfx_setfont_* and gfx_draw_text* functions implement
 *       this API. It supports bold, underline and strikethrough font
 *       styles and two different rendering modes (pixel map and alpha map)
 */

#define MAX_FONT_SIZE 128
#define DEFAULT_FONT_GOTHIC "fonts/VL-Gothic-Regular.ttf"
#define DEFAULT_FONT_MINCHO "fonts/HanaMinA.ttf"

enum font_weight {
	WEIGHT_NORMAL,
	WEIGHT_BOLD,
	WEIGHT_HEAVY
};
#define NR_FONT_WEIGHTS (WEIGHT_HEAVY+1)

struct glyph {
	float off_x;
	float off_y;
	Texture t[NR_FONT_WEIGHTS];
};

struct font_size {
	unsigned size;
	struct font *font;
	struct hash_table *glyph_table;
};

struct font {
	FT_Face font;
	unsigned nr_sizes;
	struct font_size *sizes;
};

static FT_Library ft_lib;
static struct font fonts[2];

const char *font_paths[] = {
	[FONT_GOTHIC] = XSYS4_DATA_DIR "/" DEFAULT_FONT_GOTHIC,
	[FONT_MINCHO] = XSYS4_DATA_DIR "/" DEFAULT_FONT_MINCHO
};

static bool _load_font(enum font_face type)
{
	static const char *local_font_paths[] = {
		[FONT_GOTHIC] = DEFAULT_FONT_GOTHIC,
		[FONT_MINCHO] = DEFAULT_FONT_MINCHO
	};
	static const char *default_font_paths[] = {
		[FONT_GOTHIC] = XSYS4_DATA_DIR "/" DEFAULT_FONT_GOTHIC,
		[FONT_MINCHO] = XSYS4_DATA_DIR "/" DEFAULT_FONT_MINCHO
	};

	if (!FT_New_Face(ft_lib, font_paths[type], 0, &fonts[type].font))
		return true;
	if (!FT_New_Face(ft_lib, default_font_paths[type], 0, &fonts[type].font))
		return true;
	return !FT_New_Face(ft_lib, local_font_paths[type], 0, &fonts[type].font);
}

static void load_font(enum font_face type)
{
	const char *typestr = type == FONT_GOTHIC ? "gothic" : "mincho";
	if (!_load_font(type)) {
		ERROR("Failed to load %s font", typestr);
	}
	if (!fonts[type].font->charmap) {
		ERROR("No unicode charmap for %s font", typestr);
	}
}

void gfx_font_init(void)
{
	if (FT_Init_FreeType(&ft_lib)) {
		ERROR("Failed to initialize FreeType");
	}
	load_font(FONT_GOTHIC);
	load_font(FONT_MINCHO);
}

static struct font_size *get_font_size(enum font_face type, unsigned size)
{
	struct font *font = &fonts[type];
	for (int i = 0; i < font->nr_sizes; i++) {
		if (font->sizes[i].size == size) {
			return &font->sizes[i];
		}
	}

	font->sizes = xrealloc_array(font->sizes, font->nr_sizes, font->nr_sizes+1, sizeof(struct font_size));
	struct font_size *fs = &font->sizes[font->nr_sizes++];
	fs->size = size;
	fs->font = font;
	fs->glyph_table = ht_create(4096);
	return fs;
}

// Convert a glyph rendered by FreeType to a block-sized texture.
// Block size is size x 1.5*size (full-width) or size/2 x 1.5*size (half-width)
static void init_glyph_texture(Texture *dst, FT_Bitmap *glyph, int bitmap_top, int size, bool half_width)
{
	// calculate block size and offsets
	int width = half_width ? size/2 : size;
	int height = size + size/2;
	int off_x = max(0, (width - (int)glyph->width) / 2);
	int off_y = max(0, size - bitmap_top);
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
}

static struct glyph *get_glyph(struct font_size *font_size, uint32_t code, bool half_width, enum font_weight weight)
{
	struct ht_slot *slot = ht_put_int(font_size->glyph_table, code, NULL);
	if (slot->value && ((struct glyph*)slot->value)->t[weight].handle)
		return slot->value;

	if (!slot->value) {
		slot->value = xcalloc(1, sizeof(struct glyph));
	}
	struct glyph *glyph = slot->value;
	Texture *t = &glyph->t[weight];

	// render bitmap
	FT_Face font = font_size->font->font;
	if (FT_Load_Char(font, code, FT_LOAD_RENDER)) {
		WARNING("Failed to load glyph for codepoint 0x%x", code);
		return NULL;
	}
	if (weight != WEIGHT_NORMAL) {
		int bold_weight = weight == WEIGHT_HEAVY ? 128 : 64;
		FT_GlyphSlot_Own_Bitmap(font->glyph);
		FT_Bitmap_Embolden(ft_lib, &font->glyph->bitmap, bold_weight, 0);
	}

	// create texture from bitmap
	FT_Bitmap *bitmap = &font->glyph->bitmap;
	if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
		init_glyph_texture(t, bitmap, font->glyph->bitmap_top, font_size->size, half_width);
	} else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
		FT_Bitmap tmp;
		FT_Bitmap_New(&tmp);
		if (FT_Bitmap_Convert(ft_lib, &font->glyph->bitmap, &tmp, 1)) {
			WARNING("Failed to convert monochrome glyph to grayscale");
			FT_Bitmap_Done(ft_lib, &tmp);
			return NULL;
		}
		// round non-zero values up to 255 because FreeType doesn't
		for (int i = 0; i < tmp.rows * tmp.width; i++) {
			if (tmp.buffer[i])
				tmp.buffer[i] = 255;
		}
		init_glyph_texture(t, &tmp, font->glyph->bitmap_top, font_size->size, half_width);
		FT_Bitmap_Done(ft_lib, &tmp);
	} else {
		WARNING("Font returned glyph with unsupported pixel mode");
		return NULL;
	}

	glyph->off_x = 0;
	glyph->off_y = (float)(font_size->size * 0.85) - font_size->size;
	return glyph;
}

enum text_render_mode {
	RENDER_BLENDED,
	RENDER_PMAP,
	RENDER_AMAP,
};

struct text_render_metrics {
	Point pos;
	enum font_face type;
	int size;
	SDL_Color color;
	int char_space;
	enum font_weight weight;
	float edge_width;
	enum text_render_mode mode;
};

int _gfx_render_text(Texture *dst, char *msg, struct text_render_metrics *tm)
{
	float pos_x = tm->pos.x;
	float pos_y = tm->pos.y;
	struct font_size *font_size = get_font_size(tm->type, tm->size);
	if (FT_Set_Pixel_Sizes(font_size->font->font, 0, font_size->size)) {
		WARNING("Failed setting font size to %d", font_size->size);
	}

	while (*msg) {
		// extract char code from msg
		int c;
		int char_w = SJIS_2BYTE(*msg) ? 2 : 1;
		msg = sjis_char2unicode(msg, &c);
		if (c == '^' && game_rance02_mg) {
			c = 0xE9; // é
		} else if (c == 0xFF89 && game_rance6_mg) { // half-width katakana 'no' (ﾉ)
			c = 0xE9; // é
		}

		// get glyph for char code
		struct glyph *glyph = get_glyph(font_size, c, char_w == 1, tm->weight);
		if (!glyph) {
			continue;
		}

		// render glyph
		Texture *t = &glyph->t[tm->weight];
		float x_pos = pos_x + glyph->off_x;
		int y_pos = roundf(pos_y + glyph->off_y);
		if (tm->mode == RENDER_BLENDED) {
			gfx_draw_glyph(dst, x_pos, y_pos, t, tm->color, config.text_x_scale, tm->edge_width);
		} else if (tm->mode == RENDER_PMAP) {
			gfx_draw_glyph_to_pmap(dst, x_pos, y_pos, t, tm->color, config.text_x_scale);
		} else if (tm->mode == RENDER_AMAP) {
			gfx_draw_glyph_to_amap(dst, x_pos, y_pos, t, config.text_x_scale);
		}

		// advance
		int advance = (char_w == 2 ? tm->size : tm->size / 2) + tm->char_space;
		pos_x += advance * config.text_x_scale;
	}
	return roundf(pos_x - tm->pos.x);
}

static enum font_weight int_to_font_weight(int weight)
{
	// 0 -> 550 = LIGHT
	if (weight <= 550)
		return WEIGHT_NORMAL;
	// 551 -> 999 = BOLD
	if (weight <= 999)
		return WEIGHT_BOLD;
	// 1000 = LIGHT
	if (weight == 1000)
		return WEIGHT_NORMAL;
	// x001 -> x550 = MEDIUM
	// x551 -> x999 = HEAVY-BOLD
	// NOTE: we don't distinguish between MEDIUM and BOLD (they are nearly identical)
	return (weight % 1000) < 551 ? WEIGHT_BOLD : WEIGHT_HEAVY;
}

int gfx_render_text(Texture *dst, Point pos, char *msg, struct text_metrics *tm, int char_space)
{
	enum font_weight weight = int_to_font_weight(tm->weight);
	enum font_face type = tm->face > 1 ? FONT_GOTHIC : tm->face;
	int outline = max(tm->outline_left, tm->outline_right);
	outline = max(outline, tm->outline_up);
	outline = max(outline, tm->outline_down);
	if (outline) {
		struct text_render_metrics metrics = {
			.pos = pos,
			.type = type,
			.size = tm->size,
			.color = tm->outline_color,
			.char_space = char_space,
			.weight = weight,
			.edge_width = outline,
			.mode = RENDER_BLENDED,
		};
		_gfx_render_text(dst, msg, &metrics);
	}
	struct text_render_metrics metrics = {
		.pos = pos,
		.type = type,
		.size = tm->size,
		.color = tm->color,
		.char_space = char_space,
		.weight = weight,
		.edge_width = 0.0,
		.mode = RENDER_BLENDED,
	};
	return _gfx_render_text(dst, msg, &metrics);
}

// current font state
static struct font_metrics font_metrics = {
	.size = 16,
	.face = FONT_GOTHIC,
	.weight = FW_BOLD,
	.underline = false,
	.strikeout = false,
	.space = 0,
	.color = {
		.r = 255,
		.g = 255,
		.b = 255,
		.a = 255
	}
};

static void gfx_draw_text(Texture *dst, int x, int y, char *text, enum text_render_mode mode)
{
	struct text_render_metrics metrics = {
		.pos = { x, y },
		.type = font_metrics.face > 1 ? FONT_GOTHIC : font_metrics.face,
		.size = font_metrics.size,
		.color = font_metrics.color,
		.char_space = font_metrics.space,
		.weight = int_to_font_weight(font_metrics.weight),
		.edge_width = 0.0,
		.mode = mode,
	};
	// TODO: underline and strikethrough
	_gfx_render_text(dst, text, &metrics);
}

void gfx_draw_text_to_amap(Texture *dst, int x, int y, char *text)
{
	gfx_draw_text(dst, x, y, text, RENDER_AMAP);
}

void gfx_draw_text_to_pmap(Texture *dst, int x, int y, char *text)
{
	gfx_draw_text(dst, x, y, text, RENDER_PMAP);
}

bool gfx_set_font_size(unsigned int size)
{
	font_metrics.size = size;
	return true;
}

int gfx_get_font_size(void)
{
	return font_metrics.size;
}

bool gfx_set_font_face(enum font_face face)
{
	font_metrics.face = face;
	return true;
}

enum font_face gfx_get_font_face(void)
{
	return font_metrics.face;
}

bool gfx_set_font_weight(int weight)
{
	font_metrics.weight = weight;
	return true;
}

int gfx_get_font_weight(void)
{
	return font_metrics.weight;
}

bool gfx_set_font_underline(bool on)
{
	font_metrics.underline = on;
	return true;
}

bool gfx_get_font_underline(void)
{
	return font_metrics.underline;
}

bool gfx_set_font_strikeout(bool on)
{
	font_metrics.strikeout = on;
	return true;
}

bool gfx_get_font_strikeout(void)
{
	return font_metrics.strikeout;
}

bool gfx_set_font_space(int space)
{
	font_metrics.space = space;
	return true;
}

int gfx_get_font_space(void)
{
	return font_metrics.space;
}

bool gfx_set_font_color(SDL_Color color)
{
	font_metrics.color = color;
	return true;
}

SDL_Color gfx_get_font_color(void)
{
	return font_metrics.color;
}

void gfx_set_font_name(const char *name)
{
	static const char mincho_name[] = { 0x82, 0x6c, 0x82, 0x72, 0x20, 0x96, 0xbe, 0x92, 0xa9, 0 };
	static const char gothic_name[] = { 0x82, 0x6c, 0x82, 0x72, 0x20, 0x83, 0x53, 0x83, 0x56, 0x83, 0x62, 0x83, 0x4e, 0 };
	if (!strcmp(name, mincho_name)) {
		font_metrics.face = FONT_MINCHO;
	} else if (!strcmp(name, gothic_name)) {
		font_metrics.face = FONT_GOTHIC;
	} else {
		WARNING("Unhandled font name: \"%s\"", display_sjis0(name));
	}
}
