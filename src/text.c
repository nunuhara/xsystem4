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

#include "system4.h"
#include "system4/fnl.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/font.h"
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

#define DEFAULT_FONT_GOTHIC "fonts/VL-Gothic-Regular.ttf"
#define DEFAULT_FONT_MINCHO "fonts/HanaMinA.ttf"

#define MAX_FONTS 32
static struct font *fonts[MAX_FONTS] = {0};

// Controls whether edge widths are taken into account during text layout.
bool gfx_text_advance_edges = false;

static struct font *load_font(enum font_face type)
{
	static const char *local_font_paths[] = {
		[FONT_GOTHIC] = DEFAULT_FONT_GOTHIC,
		[FONT_MINCHO] = DEFAULT_FONT_MINCHO
	};
	static const char *default_font_paths[] = {
		[FONT_GOTHIC] = XSYS4_DATA_DIR "/" DEFAULT_FONT_GOTHIC,
		[FONT_MINCHO] = XSYS4_DATA_DIR "/" DEFAULT_FONT_MINCHO
	};

	struct font *font;
	// user specified font
	if (config.font_paths[type] && (font = ft_font_load(config.font_paths[type])))
		return font;
	// installed default font
	if ((font = ft_font_load(default_font_paths[type])))
		return font;
	// local default font
	if ((font = ft_font_load(local_font_paths[type])))
		return font;
	ERROR("Failed to load %s font", type == FONT_GOTHIC ? "gothic" : "mincho");
}

static bool font_initialized = false;

void gfx_font_init(void)
{
	if (font_initialized)
		return;
	if (config.fnl_path) {
		struct fnl *fnl = fnl_open(config.fnl_path);
		if (!fnl)
			ERROR("Failed to load .fnl font library '%s'", config.fnl_path);
		if (!fnl->nr_fonts)
			ERROR("No fonts in .fnl font library '%s'", config.fnl_path);
		for (unsigned i = 0; i < fnl->nr_fonts && i < MAX_FONTS; i++) {
			fonts[i] = fnl_font_load(fnl, i);
		}
	} else {
		ft_font_init();
		fonts[FONT_GOTHIC] = load_font(FONT_GOTHIC);
		fonts[FONT_MINCHO] = load_font(FONT_MINCHO);
	}
	font_initialized = true;
}

static unsigned get_face(unsigned face)
{
	if (game_rance7_mg) {
		unsigned i = face - 256;
		if (i >= MAX_FONTS || !fonts[i]) {
			// XXX: Really not sure why this is how it works...
			i = face == 1 ? 2 : 1;
		}
		face = i;
	} else if (config.fnl_path) {
		// TODO: check what actual games do here...
		//       In Rance01Font.fnl:
		//         fonts[0] is proportional sans/gothic
		//         fonts[1] is monospace sans/gothic
		//         fonts[2] is proportional serif/mincho
		face += 1;
	}
	if (face >= MAX_FONTS || !fonts[face])
		face = 0;
	return face;
}

static struct font *get_font(unsigned face)
{
	return fonts[get_face(face)];
}

static struct font_size *font_get_size(unsigned face, float size)
{
	struct font *font = get_font(face);
	return font->get_size(font, size);
}

static struct font_size *text_style_font_size(struct text_style *ts)
{
	if (ts->font_size)
		return ts->font_size;
	return ts->font_size = font_get_size(ts->face, ts->size);
}

static struct glyph *font_get_glyph(struct font_size *size, uint32_t code, enum font_weight weight)
{
	if (!size->glyph_table)
		size->glyph_table = ht_create(4096);
	// return cached glyph if available
	struct ht_slot *slot = ht_put_int(size->glyph_table, code, NULL);
	if (slot->value && ((struct glyph*)slot->value)->t[weight].handle)
		return slot->value;
	// alloc if necessary
	if (!slot->value)
		slot->value = xcalloc(1, sizeof(struct glyph));
	// render glyph
	if (!size->font->get_glyph(size, slot->value, code, weight))
		return NULL;
	return slot->value;
}

static float font_size_char(struct font_size *size, uint32_t code)
{
	return size->font->size_char(size, code);
}

static uint32_t char_to_code(const char *ch, enum charmap charmap)
{
	if (charmap == CHARMAP_SJIS)
		return sjis_code(ch);
	int c;
	sjis_char2unicode(ch, &c);
	if (c == '^' && game_rance02_mg)
		return 0xE9; // é
	// half-width katakana 'no' (ﾉ)
	if (c == 0xFF89 && game_rance6_mg)
		return 0xE9; // é
	return c;
}

float gfx_size_char(struct text_style *ts, const char *ch)
{
	struct font_size *size = text_style_font_size(ts);
	return font_size_char(size, char_to_code(ch, size->font->charmap));
}

float gfx_size_text(struct text_style *ts, const char *text)
{
	struct font_size *size = text_style_font_size(ts);
	float edge_advance = gfx_text_advance_edges ? ts->edge_left + ts->edge_right : 0.0f;
	float x = 0.0f;
	while (*text) {
		x += font_size_char(size, char_to_code(text, size->font->charmap));
		x += edge_advance;
		text = sjis_skip_char(text);
	}
	return x;
}

float gfx_get_actual_font_size(unsigned face, float size)
{
	struct font *font = get_font(face);
	return font->get_actual_size(font, size);
}

float gfx_get_actual_font_size_round_down(unsigned face, float size)
{
	struct font *font = get_font(face);
	return font->get_actual_size_round_down(font, size);
}

enum text_render_mode {
	RENDER_BLENDED,
	RENDER_PMAP,
	RENDER_AMAP,
};

struct text_render_metrics {
	float x;
	int y;
	SDL_Color color;
	enum font_weight weight;
	float edge_width;
	float scale_x;
	float space_scale_x;
	float font_spacing;
	float edge_spacing;
	enum text_render_mode mode;
	struct font_size *font_size;
};

static int render_text(Texture *dst, char *msg, struct text_render_metrics *tm)
{
	float pos_x = tm->x;
	int pos_y = tm->y + tm->font_size->y_offset;

	while (*msg) {
		pos_x += tm->edge_spacing;
		// get glyph for character
		float scale_x = *msg == ' ' ? tm->space_scale_x : tm->scale_x;
		uint32_t code = char_to_code(msg, tm->font_size->font->charmap);
		msg += SJIS_2BYTE(*msg) ? 2 : 1;
		struct glyph *glyph = font_get_glyph(tm->font_size, code, tm->weight);
		if (!glyph)
			continue;

		// render glyph
		Texture *t = &glyph->t[tm->weight];
		if (tm->mode == RENDER_BLENDED) {
			float x = pos_x - glyph->rect.x;
			int y = pos_y - glyph->rect.y;
			gfx_draw_glyph(dst, x, y, t, tm->color, config.text_x_scale, tm->edge_width);
		} else if (tm->mode == RENDER_PMAP) {
			gfx_draw_glyph_to_pmap(dst, pos_x, pos_y, t, glyph->rect, tm->color, config.text_x_scale);
		} else if (tm->mode == RENDER_AMAP) {
			gfx_draw_glyph_to_amap(dst, pos_x, pos_y, t, glyph->rect, config.text_x_scale);
		}

		// advance
		pos_x += glyph->advance * scale_x * config.text_x_scale + tm->font_spacing;
		pos_x += tm->edge_spacing;
	}
	return roundf(pos_x - tm->x);
}

static enum font_weight int_to_font_weight(int weight)
{
	// 0 -> 550 = LIGHT
	if (weight <= 550)
		return FONT_WEIGHT_NORMAL;
	// 551 -> 999 = BOLD
	if (weight <= 999)
		return FONT_WEIGHT_BOLD;
	// 1000 = LIGHT
	if (weight == 1000)
		return FONT_WEIGHT_NORMAL;
	// x001 -> x550 = MEDIUM
	// x551 -> x999 = HEAVY-BOLD
	// NOTE: we don't distinguish between MEDIUM and BOLD (they are nearly identical)
	return (weight % 1000) < 551 ? FONT_WEIGHT_BOLD : FONT_WEIGHT_HEAVY;
}

int gfx_render_text(Texture *dst, float x, int y, char *msg, struct text_style *ts)
{
	enum font_weight weight = int_to_font_weight(ts->weight);
	struct font_size *font_size = text_style_font_size(ts);
	float edge_width = text_style_edge_width(ts);
	if (edge_width > 0.01f) {
		struct text_render_metrics metrics = {
			.x = x,
			.y = y,
			.color = ts->edge_color,
			.weight = weight,
			.edge_width = edge_width,
			.scale_x = ts->scale_x,
			.space_scale_x = ts->space_scale_x,
			.font_spacing = ts->font_spacing,
			.edge_spacing = gfx_text_advance_edges ? edge_width : 0.0,
			.mode = RENDER_BLENDED,
			.font_size = font_size,
		};
		render_text(dst, msg, &metrics);
	}
	struct text_render_metrics metrics = {
		.x = x,
		.y = y,
		.color = ts->color,
		.weight = weight,
		.edge_width = ts->bold_width,
		.scale_x = ts->scale_x,
		.space_scale_x = ts->space_scale_x,
		.font_spacing = ts->font_spacing,
		.edge_spacing = gfx_text_advance_edges ? edge_width : 0.0,
		.mode = RENDER_BLENDED,
		.font_size = font_size,
	};
	return render_text(dst, msg, &metrics);
}

struct font_metrics {
	int size;
	enum font_face face;
	int weight;
	bool underline;
	bool strikeout;
	int space;
	SDL_Color color;
};

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
	struct font_size *font_size = font_get_size(font_metrics.face, font_metrics.size);
	struct text_render_metrics metrics = {
		.x = x,
		.y = y,
		.color = font_metrics.color,
		.weight = int_to_font_weight(font_metrics.weight),
		.edge_width = 0.0f,
		.scale_x = 1.0f,
		.space_scale_x = 1.0f,
		.font_spacing = font_metrics.space,
		.mode = mode,
		.font_size = font_size,
	};
	// TODO: underline and strikethrough
	render_text(dst, text, &metrics);
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

void gfx_print_text_style(struct text_style *style, int indent)
{
	printf("{\n");
	indent++;

	indent_printf(indent, "face = %u,\n", style->face);
	indent_printf(indent, "size = %f,\n", style->size);
	indent_printf(indent, "bold_width = %f,\n", style->bold_width);
	indent_printf(indent, "weight = %u,\n", style->weight);
	indent_printf(indent, "edge_weight = {l=%f,u=%f,r=%f,d=%f},\n",
			style->edge_left, style->edge_up, style->edge_right, style->edge_down);
	indent_printf(indent, "color = "); gfx_print_color(&style->color); printf(",\n");
	indent_printf(indent, "edge_color = "); gfx_print_color(&style->edge_color); printf(",\n");
	indent_printf(indent, "scale_x = %f,\n", style->scale_x);
	indent_printf(indent, "space_scale_x = %f,\n", style->space_scale_x);
	indent_printf(indent, "font_spacing = %f,\n", style->font_spacing);

	indent--;
	indent_printf(indent, "}");
}
