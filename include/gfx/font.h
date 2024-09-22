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

#ifndef SYSTEM4_FONT_H
#define SYSTEM4_FONT_H

#include "system4.h"
#include "gfx/gfx.h"

struct fnl;
struct hash_table;

// standard font weight values
enum {
	FW_NORMAL = 400,
	FW_BOLD   = 700,
	FW_MEDIUM = 1400,
	FW_HEAVY  = 1700
};

enum font_face {
	FONT_GOTHIC = 0,
	FONT_MINCHO = 1
};

enum font_weight {
	FONT_WEIGHT_NORMAL,
	FONT_WEIGHT_BOLD,
	FONT_WEIGHT_HEAVY
};
#define NR_FONT_WEIGHTS (FONT_WEIGHT_HEAVY+1)

struct glyph {
	Rectangle rect;
	float advance;
	Texture t[NR_FONT_WEIGHTS];
};

struct font_size {
	float size;
	int y_offset;
	struct font *font;
	struct hash_table *glyph_table;
};

enum charmap {
	CHARMAP_UNICODE,
	CHARMAP_SJIS
};

struct font {
	enum charmap charmap;
	struct font_size *(*get_size)(struct font *font, float size);
	float (*get_actual_size)(struct font *font, float size);
	float (*get_actual_size_round_down)(struct font *font, float size);
	bool (*get_glyph)(struct font_size *size, struct glyph *dst, uint32_t code, enum font_weight weight);
	float (*size_char)(struct font_size *size, uint32_t code);
	float (*size_char_kerning)(struct font_size *size, uint32_t code, uint32_t code_next);
};

struct text_style {
	unsigned face;
	float size;
	float bold_width; // SengokuRanceFont
	unsigned weight;  // SACT2, etc.
	float edge_left;
	float edge_up;
	float edge_right;
	float edge_down;
	SDL_Color color;
	SDL_Color edge_color;
	float scale_x;
	float space_scale_x;
	float font_spacing;

	// cached, for SengokuRanceFont
	struct font_size *font_size;
};

enum text_render_mode {
	RENDER_COPY,
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

static inline float text_style_edge_width(struct text_style *ts)
{
	return max(max(ts->edge_up, ts->edge_down), max(ts->edge_left, ts->edge_right));
}

static inline bool text_style_has_edge(struct text_style *ts)
{
	return text_style_edge_width(ts) > 0.01f;
}

static inline void text_style_set_edge_width(struct text_style *ts, float w)
{
	ts->edge_left = w;
	ts->edge_up = w;
	ts->edge_right = w;
	ts->edge_down = w;
}

extern bool gfx_text_advance_edges;

void gfx_font_init(void);
void ft_font_init(void);
struct font *ft_font_load(const char *path);
struct font *fnl_font_load(struct fnl *lib, unsigned index);

bool gfx_set_font(enum font_face face, unsigned int size);
bool gfx_set_font_size(unsigned int size);
bool gfx_set_font_face(enum font_face face);
bool gfx_set_font_weight(int weight);
bool gfx_set_font_underline(bool on);
bool gfx_set_font_strikeout(bool on);
bool gfx_set_font_space(int space);
bool gfx_set_font_color(SDL_Color color);

int gfx_get_font_size(void);
enum font_face gfx_get_font_face(void);
int gfx_get_font_weight(void);
bool gfx_get_font_underline(void);
bool gfx_get_font_strikeout(void);
int gfx_get_font_space(void);
SDL_Color gfx_get_font_color(void);
void gfx_set_font_name(const char *name);

struct font_size *gfx_font_get_size(unsigned face, float size);
enum font_weight gfx_int_to_font_weight(int weight);
float _gfx_render_text(Texture *dst, char *msg, struct text_render_metrics *tm);

int gfx_render_text(Texture *dst, float x, int y, char *msg, struct text_style *ts, bool blend);
float gfx_render_textf(Texture *dst, float x, int y, char *msg, struct text_style *ts, bool blend);
void gfx_render_dash_text(Texture *dst, struct text_style *ts);
void gfx_draw_text_to_amap(Texture *dst, int x, int y, char *text);
void gfx_draw_text_to_pmap(Texture *dst, int x, int y, char *text);
float gfx_size_char(struct text_style *ts, const char *ch);
float gfx_size_char_kerning(struct text_style *ts, uint32_t code, uint32_t code_next);
float gfx_size_text(struct text_style *ts, const char *text);
float gfx_get_actual_font_size(unsigned face, float size);
float gfx_get_actual_font_size_round_down(unsigned face, float size);

static inline float text_style_width(struct text_style *ts, const char *ch)
{
	return (gfx_size_char(ts, ch) + (ts->bold_width * 2) + ts->edge_left + ts->edge_right)
		* ts->scale_x;
}

static inline float text_style_height(struct text_style *ts)
{
	return ts->size + (ts->bold_width * 2) + ts->edge_up + ts->edge_down;
}

#endif /* SYSTEM4_FONT_H */
