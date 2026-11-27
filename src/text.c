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
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/fnl.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/font.h"
#if defined(ALICEHUB_IOS)
#include "sact.h"
#endif
#include "vm.h"
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

#define MAX_FNL_FONTS 32
struct font *font_ttf[2] = {0};
struct font *font_fnl[MAX_FNL_FONTS] = {0};

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
	static const char *gbk_gothic_paths[] = {
#ifdef __ANDROID__
		"/system/fonts/NotoSansCJK-Regular.ttc",
		"/system/fonts/NotoSansSC-Regular.otf",
		"/system/fonts/DroidSansFallback.ttf",
#else
		"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/truetype/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
		"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
#endif
	};
	static const char *gbk_mincho_paths[] = {
#ifdef __ANDROID__
		"/system/fonts/NotoSerifCJK-Regular.ttc",
#else
		"/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
		"/usr/share/fonts/truetype/noto/NotoSerifCJK-Regular.ttc",
#endif
	};

#if defined(ALICEHUB_IOS)
	static const char *const default_font_env[2][2] = {
		{ [FONT_GOTHIC] = "XSYSTEM4_FONT_GOTHIC", [FONT_MINCHO] = "XSYSTEM4_FONT_MINCHO" },
		{ [FONT_GOTHIC] = "XSYSTEM4_FONT_GOTHIC_SC", [FONT_MINCHO] = "XSYSTEM4_FONT_MINCHO_SC" },
	};
#endif

	struct font *font;
	bool use_sc_fonts = sys4_text_is_gbk || sys4_has_mai_word_table();
	// user specified font
	if (config.font_paths[type] && (font = ft_font_load(config.font_paths[type])))
		return font;
#if defined(ALICEHUB_IOS)
	// platform default set by the launcher
	const char *env_font = getenv(default_font_env[use_sc_fonts][type]);
	if (env_font && *env_font && (font = ft_font_load(env_font))) {
		struct font *fallback = ft_font_load(default_font_paths[FONT_MINCHO]);
		if (!fallback)
			fallback = ft_font_load(local_font_paths[FONT_MINCHO]);
		ft_font_set_fallback(font, fallback);
		return font;
	}
#endif
	if (use_sc_fonts) {
		const char *const *paths = type == FONT_GOTHIC ? gbk_gothic_paths : gbk_mincho_paths;
		size_t nr_paths = type == FONT_GOTHIC
			? sizeof(gbk_gothic_paths) / sizeof(gbk_gothic_paths[0])
			: sizeof(gbk_mincho_paths) / sizeof(gbk_mincho_paths[0]);
		for (size_t i = 0; i < nr_paths; i++) {
			if ((font = ft_font_load(paths[i])))
				return font;
		}
		if ((font = ft_font_load(default_font_paths[FONT_MINCHO]))) return font;
		if ((font = ft_font_load(local_font_paths[FONT_MINCHO]))) return font;
	}
	// installed default font
	if ((font = ft_font_load(default_font_paths[type])))
		return font;
	// local default font
	if ((font = ft_font_load(local_font_paths[type])))
		return font;
	ERROR("Failed to load %s font", type == FONT_GOTHIC ? "gothic" : "mincho");
}

static bool font_initialized = false;
#if defined(ALICEHUB_IOS)
static struct fnl *session_fnl;
#endif

// Chinese releases (UTF-8, GBK, or CHS.TBL) substitute a TTF for .fnl bitmap
// fonts, whose glyphs fill only part of the cell (e.g. 0.56–0.81 of cell height).
// Scale each .fnl face index to match its original glyph width/height ratio and
// vertically center it within the cell so text matches the original layout.
static float fnl_substitute_scales[MAX_FNL_FONTS];
static unsigned fnl_substitute_count = 0;

static uint32_t read_le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool fnl_uses_ttf_substitute(void)
{
	return sys4_text_is_gbk || sys4_has_mai_word_table() || !config.fnl_path;
}

static void set_fnl_substitute_scales(const float *scales, unsigned count)
{
	if (count > MAX_FNL_FONTS)
		count = MAX_FNL_FONTS;
	for (unsigned i = 0; i < count; i++)
		fnl_substitute_scales[i] = scales[i];
	fnl_substitute_count = count;
}

static void apply_fallback_fnl_substitute_scale(void)
{
	// Fallback ratios measured from each title's original *Font.fnl (full-width glyph
	// width / cell height) for packages that omit the .fnl (Evenicle 2 Rance, Evenicle
	// Gaiden) or repacks with stripped *Font.fnl files. Games whose face >= 256 uses
	// native 1.0x TTF sizing (Rance 10, Okusama, Dohna Dohna) stay unscaled.
	if (game_night_remaster) {
		static const float s[] = { 163.f / 288.f, 149.f / 192.f, 154.f / 192.f, 159.f / 192.f, 156.f / 192.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_evenicle2 || game_evenicle2_rance) {
		static const float s[] = { 159.f / 200.f, 118.f / 176.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_evenicle || game_evenicle_rance || game_evenicle_gaiden) {
		static const float s[] = { 190.f / 272.f, 87.f / 112.f, 77.f / 96.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_ixseal) {
		static const float s[] = { 293.f / 440.f, 154.f / 240.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_oyakorankan) {
		static const float s[] = { 43.f / 56.f, 84.f / 120.f, 61.f / 80.f, 107.f / 160.f, 89.f / 136.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_tsumamigui3 || game_tsumamigui3_jun || game_tsumamigui3_ntr) {
		static const float s[] = { 129.f / 192.f, 112.f / 160.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_haharanman) {
		static const float s[] = { 123.f / 160.f, 152.f / 200.f, 131.f / 160.f, 104.f / 160.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_rance01) {
		static const float s[] = { 309.f / 384.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_rance03) {
		static const float s[] = { 268.f / 384.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_rance8) {
		static const float s[] = { 134.f / 192.f, 149.f / 192.f, 90.f / 128.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	} else if (game_rance9) {
		static const float s[] = { 120.f / 160.f, 101.f / 144.f, 128.f / 168.f, 148.f / 224.f, 145.f / 224.f, 109.f / 200.f };
		set_fnl_substitute_scales(s, sizeof(s) / sizeof(s[0]));
	}
}

static float get_fnl_substitute_scale(unsigned face)
{
	if (face <= 255 || !fnl_uses_ttf_substitute() || !fnl_substitute_count)
		return 1.0f;
	unsigned idx = face - 256;
	return idx < fnl_substitute_count ? fnl_substitute_scales[idx] : fnl_substitute_scales[0];
}

int gfx_get_fnl_substitute_y_pad(unsigned face, float size)
{
	float scale = get_fnl_substitute_scale(face);
	if (scale >= 0.999f || size <= 0.0f)
		return 0;
	float scaled = roundf(max(1.0f, size * scale));
	int pad = (int)lroundf((size - scaled) * 0.5f);
	return pad > 0 ? pad : 0;
}

// Reads only the metrics it needs, with bounds checks: fnl_open() exits on a
// damaged index, and the .fnl is otherwise unused when TTF stands in for it.
static void init_fnl_substitute_scale(void)
{
	fnl_substitute_count = 0;
	for (unsigned i = 0; i < MAX_FNL_FONTS; i++)
		fnl_substitute_scales[i] = 1.0f;
	// The Rance 03 CN release draws its TTF at the full requested size; its
	// Japanese *Font.fnl ratio (0.70) shrinks the text and offsets its layout.
	if (!fnl_uses_ttf_substitute() || (game_rance03 && sys4_text_is_gbk))
		return;
	if (!config.fnl_path) {
		apply_fallback_fnl_substitute_scale();
		return;
	}
	FILE *f = file_open_utf8(config.fnl_path, "rb");
	if (!f) {
		apply_fallback_fnl_substitute_scale();
		return;
	}
	uint8_t header[16];
	uint8_t *index = NULL;
	if (fread(header, sizeof(header), 1, f) != 1 || memcmp(header, "FNA\0", 4))
		goto out;
	uint32_t index_size = read_le32(header + 12);
	if (index_size < 4 || index_size > (64u << 20))
		goto out;
	index = xmalloc(index_size);
	if (fread(index, index_size, 1, f) != 1)
		goto out;
	// nr_fonts, then per font: nr_faces, then per face: height, uk, nr_glyphs, glyphs[10 bytes]
	uint32_t nr_fonts = read_le32(index);
	size_t pos = 4;
	unsigned g_primary = fnl_char_to_index(0x8a6d); // 確 (full-width)
	unsigned g_fallback = fnl_char_to_index(0x889f); // 亜 (full-width)
	for (uint32_t fi = 0; fi < nr_fonts && fi < MAX_FNL_FONTS; fi++) {
		if (index_size - pos < 4)
			break;
		uint32_t nr_faces = read_le32(index + pos);
		pos += 4;
		uint32_t best_height = 0;
		unsigned best_width = 0;
		bool ok = true;
		for (uint32_t i = 0; i < nr_faces; i++) {
			if (index_size - pos < 12) {
				ok = false;
				break;
			}
			uint32_t height = read_le32(index + pos);
			uint32_t nr_glyphs = read_le32(index + pos + 8);
			size_t glyphs = pos + 12;
			if (nr_glyphs > (index_size - glyphs) / 10) {
				ok = false;
				break;
			}
			pos = glyphs + (size_t)nr_glyphs * 10;
			if (height > best_height) {
				unsigned width = 0;
				if (g_primary < nr_glyphs) {
					size_t off = glyphs + (size_t)g_primary * 10;
					if (read_le32(index + off + 2))
						width = index[off] | ((unsigned)index[off + 1] << 8);
				}
				if (!width && g_fallback < nr_glyphs) {
					size_t off = glyphs + (size_t)g_fallback * 10;
					if (read_le32(index + off + 2))
						width = index[off] | ((unsigned)index[off + 1] << 8);
				}
				if (width) {
					best_height = height;
					best_width = width;
				}
			}
		}
		if (!ok)
			break;
		if (best_height && best_width && best_width < best_height)
			fnl_substitute_scales[fi] = (float)best_width / (float)best_height;
		fnl_substitute_count = fi + 1;
	}
out:
	free(index);
	fclose(f);
	if (!fnl_substitute_count)
		apply_fallback_fnl_substitute_scale();
}

void gfx_font_init(void)
{
	if (font_initialized)
		return;
	ft_font_init();
	init_fnl_substitute_scale();
	font_ttf[FONT_GOTHIC] = load_font(FONT_GOTHIC);
	font_ttf[FONT_MINCHO] = load_font(FONT_MINCHO);

	if (config.fnl_path && !fnl_uses_ttf_substitute()) {
		struct fnl *fnl = fnl_open(config.fnl_path);
#if defined(ALICEHUB_IOS)
		session_fnl = fnl;
#endif
		if (!fnl)
			ERROR("Failed to load .fnl font library '%s'", config.fnl_path);
		if (!fnl->nr_fonts)
			ERROR("No fonts in .fnl font library '%s'", config.fnl_path);
		for (unsigned i = 0; i < fnl->nr_fonts && i < MAX_FNL_FONTS; i++) {
			font_fnl[i] = fnl_font_load(fnl, i);
		}
	}
	if ((config.fnl_path || fnl_substitute_count || (ain && ain->version >= 12)) && !game_rance7_mg)
		gfx_text_advance_edges = true;
	font_initialized = true;
}

static struct font *get_font(unsigned face)
{
	if (face > 255) {
		if (sys4_text_is_gbk || sys4_has_mai_word_table())
			return font_ttf[0];
		face -= 256;
		if (face >= MAX_FNL_FONTS || !font_fnl[face]) {
			WARNING("Invalid fnl face: %u", face);
			return font_ttf[0];
		}
		return font_fnl[face];
	}
	if (face > 1)
		return font_ttf[0];
	return font_ttf[face];
}

struct font_size *gfx_font_get_size(unsigned face, float size)
{
	struct font *font = get_font(face);
	float scale = get_fnl_substitute_scale(face);
	if (scale < 0.999f && size > 0.0f)
		size = max(1.0f, size * scale);
	return font->get_size(font, size);
}

static struct font_size *text_style_font_size(struct text_style *ts)
{
	if (ts->font_size)
		return ts->font_size;
	return ts->font_size = gfx_font_get_size(ts->face, ts->size);
}

#define MAX_CACHED_GLYPHS_PER_SIZE 4096

struct glyph_cache_tracker {
	struct font_size *fs;
	unsigned count;
};

static struct glyph_cache_tracker *glyph_cache_trackers = NULL;
static size_t nr_glyph_cache_trackers = 0;
static size_t cap_glyph_cache_trackers = 0;

static unsigned *get_glyph_cache_count(struct font_size *fs)
{
	for (size_t i = 0; i < nr_glyph_cache_trackers; i++) {
		if (glyph_cache_trackers[i].fs == fs)
			return &glyph_cache_trackers[i].count;
	}
	if (nr_glyph_cache_trackers == cap_glyph_cache_trackers) {
		cap_glyph_cache_trackers = cap_glyph_cache_trackers ? cap_glyph_cache_trackers * 2 : 16;
		glyph_cache_trackers = xrealloc(glyph_cache_trackers,
			cap_glyph_cache_trackers * sizeof(*glyph_cache_trackers));
	}
	struct glyph_cache_tracker *t = &glyph_cache_trackers[nr_glyph_cache_trackers++];
	t->fs = fs;
	t->count = 0;
	return &t->count;
}

static void free_cached_glyph(void *val)
{
	struct glyph *g = val;
	if (!g)
		return;
	for (size_t i = 0; i < sizeof(g->t) / sizeof(g->t[0]); i++)
		gfx_delete_texture(&g->t[i]);
	free(g);
}

static struct glyph *font_get_glyph(struct font_size *size, uint32_t code, enum font_weight weight)
{
	if (!size)
		return NULL;
	if (!size->glyph_table)
		size->glyph_table = ht_create(4096);
	unsigned *cached_count = get_glyph_cache_count(size);
	// return cached glyph if available
	struct ht_slot *slot = ht_put_int(size->glyph_table, code, NULL);
	if (slot->value)
		gfx_remap_texture(&((struct glyph*)slot->value)->t[weight]);
	if (slot->value && ((struct glyph*)slot->value)->t[weight].handle)
		return slot->value;
	if (!slot->value && *cached_count >= MAX_CACHED_GLYPHS_PER_SIZE) {
		ht_foreach_value(size->glyph_table, free_cached_glyph);
		ht_free_int(size->glyph_table);
		size->glyph_table = ht_create(4096);
		*cached_count = 0;
		slot = ht_put_int(size->glyph_table, code, NULL);
	}
	// alloc if necessary
	if (!slot->value) {
		slot->value = xcalloc(1, sizeof(struct glyph));
		(*cached_count)++;
	}
	// render glyph
	if (!size->font->get_glyph(size, slot->value, code, weight))
		return NULL;
	return slot->value;
}

static float font_size_char(struct font_size *size, uint32_t code)
{
	return size ? size->font->size_char(size, code) : 0.0f;
}

static uint32_t char_to_code(const char *ch, enum charmap charmap)
{
	if (SJIS_2BYTE(*ch) && ch[1] == '\0')
		return (uint8_t)*ch;
	if (charmap == CHARMAP_SJIS)
		return sjis_code(ch);
	int c;
	mai_sjis_char2unicode(ch, &c);
	if (c == '^' && game_rance02_mg)
		return 0xE9; // é
	// half-width katakana 'no' (ﾉ)
	if (c == 0xFF89 && game_rance6_mg)
		return 0xE9; // é
	return c;
}

float gfx_size_char_unstyled(struct text_style *ts, const char *ch)
{
	struct font_size *size = text_style_font_size(ts);
	if (!size)
		return 0.0f;
	return font_size_char(size, char_to_code(ch, size->font->charmap));
}

float gfx_size_char(struct text_style *ts, const char *ch)
{
	float w = gfx_size_char_unstyled(ts, ch);
	if (gfx_text_advance_edges)
		w += ts->edge_left + ts->edge_right + ceilf(ts->bold_width)*2;
	return w * ts->scale_x;
}

float gfx_size_char_extent(struct text_style *ts, const char *ch)
{
	float w = gfx_size_char(ts, ch);
	struct font_size *size = text_style_font_size(ts);
	uint32_t code = char_to_code(ch, size->font->charmap);
	struct glyph *glyph = font_get_glyph(size, code, gfx_int_to_font_weight(ts->weight));
	if (!glyph)
		return w;
	float glyph_w = glyph->rect.w * config.text_x_scale;
	if (gfx_text_advance_edges)
		glyph_w += ts->edge_left + ts->edge_right + ceilf(ts->bold_width)*2;
	return max(w, glyph_w * ts->scale_x);
}

float gfx_size_text(struct text_style *ts, const char *text)
{
	struct font_size *size = text_style_font_size(ts);
	if (!size)
		return 0.0f;
	float edge_advance = gfx_text_advance_edges
		? ts->edge_left + ts->edge_right + ceilf(ts->bold_width)*2
		: 0.0f;
	float x = 0.0f;
	while (*text) {
		x += font_size_char(size, char_to_code(text, size->font->charmap));
		x += edge_advance;
		text += (SJIS_2BYTE(*text) && text[1] != '\0') ? 2 : 1;
	}
	return x * ts->scale_x;
}

float gfx_get_actual_font_size(unsigned face, float size)
{
	struct font *font = get_font(face);
	float scale = get_fnl_substitute_scale(face);
	if (scale < 0.999f && size > 0.0f)
		size = max(1.0f, size * scale);
	return font->get_actual_size(font, size);
}

float gfx_get_actual_font_size_round_down(unsigned face, float size)
{
	struct font *font = get_font(face);
	float scale = get_fnl_substitute_scale(face);
	if (scale < 0.999f && size > 0.0f)
		size = max(1.0f, size * scale);
	return font->get_actual_size_round_down(font, size);
}

float _gfx_render_text(Texture *dst, char *msg, struct text_render_metrics *tm)
{
	if (!tm || !tm->font_size || !msg)
		return 0.0f;
	float pos_x = tm->x;
	int pos_y = tm->y + tm->font_size->y_offset;

	while (*msg) {
		pos_x += tm->edge_spacing;
		// get glyph for character
		float scale_x = *msg == ' ' ? tm->space_scale_x : tm->scale_x;
		float draw_scale_x = scale_x * config.text_x_scale;
		uint32_t code = char_to_code(msg, tm->font_size->font->charmap);
		msg += (SJIS_2BYTE(*msg) && msg[1] != '\0') ? 2 : 1;
		struct glyph *glyph = font_get_glyph(tm->font_size, code, tm->weight);
		if (!glyph)
			continue;

		// render glyph
		Texture *t = &glyph->t[tm->weight];
		if (tm->mode == RENDER_COPY) {
			float x = pos_x - glyph->rect.x * draw_scale_x;
			int y = pos_y - glyph->rect.y;
			gfx_draw_glyph(dst, x, y, t, tm->color, draw_scale_x, tm->edge_width, false);
		} else if (tm->mode == RENDER_BLENDED) {
			float x = pos_x - glyph->rect.x * draw_scale_x;
			int y = pos_y - glyph->rect.y;
			gfx_draw_glyph(dst, x, y, t, tm->color, draw_scale_x, tm->edge_width, true);
		} else if (tm->mode == RENDER_PMAP) {
			gfx_draw_glyph_to_pmap(dst, pos_x, pos_y, t, glyph->rect, tm->color, draw_scale_x);
		} else if (tm->mode == RENDER_AMAP) {
			gfx_draw_glyph_to_amap(dst, pos_x, pos_y, t, glyph->rect, draw_scale_x);
		} else if (tm->mode == RENDER_BLEND_ALPHA || tm->mode == RENDER_ALPHA_ONLY) {
			gfx_draw_glyph_mode(dst, pos_x - glyph->rect.x * draw_scale_x, pos_y - glyph->rect.y,
					t, tm->color, draw_scale_x, tm->edge_width,
					tm->mode == RENDER_BLEND_ALPHA ? GLYPH_BLEND_ALPHA : GLYPH_ALPHA_ONLY);
		}

		// advance
		pos_x += glyph->advance * scale_x * config.text_x_scale + tm->font_spacing;
		pos_x += tm->edge_spacing;
	}
	return pos_x - tm->x;
}

enum font_weight gfx_int_to_font_weight(int weight)
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

float gfx_render_text_mode(Texture *dst, float x, int y, char *msg, struct text_style *ts, enum text_render_mode mode)
{
	enum font_weight weight = gfx_int_to_font_weight(ts->weight);
	struct font_size *font_size = text_style_font_size(ts);
	float edge_width = text_style_edge_width(ts);
	int fnl_y_pad = gfx_get_fnl_substitute_y_pad(ts->face, ts->size);

	float edge_advance = gfx_text_advance_edges
		? edge_width + ceilf(ts->bold_width)
		: 0.f;
	int edge_y = (gfx_text_advance_edges
		? lroundf(ts->edge_up + ceilf(ts->bold_width))
		: 0) + fnl_y_pad;

	if (edge_width > 0.01f) {
		struct text_render_metrics metrics = {
			.x = x,
			.y = y + edge_y,
			.color = ts->edge_color,
			.weight = weight,
			.edge_width = edge_width,
			.scale_x = ts->scale_x,
			.space_scale_x = ts->space_scale_x,
			.font_spacing = ts->font_spacing,
			.edge_spacing = edge_advance,
			.mode = mode,
			.font_size = font_size,
		};
		_gfx_render_text(dst, msg, &metrics);
		if (mode == RENDER_COPY)
			mode = RENDER_BLENDED;  // core text is blended on top of the edge
	}
	struct text_render_metrics metrics = {
		.x = x,
		.y = y + edge_y,
		.color = ts->color,
		.weight = weight,
		.edge_width = ts->bold_width,
		.scale_x = ts->scale_x,
		.space_scale_x = ts->space_scale_x,
		.font_spacing = ts->font_spacing,
		.edge_spacing = edge_advance,
		.mode = mode,
		.font_size = font_size,
	};
	return _gfx_render_text(dst, msg, &metrics);
}

float gfx_render_text_edge_only(Texture *dst, float x, int y, char *msg, struct text_style *ts)
{
	float edge_width = text_style_edge_width(ts);
	if (edge_width <= 0.01f)
		return 0.0f;
	enum font_weight weight = gfx_int_to_font_weight(ts->weight);
	struct font_size *font_size = text_style_font_size(ts);
	int fnl_y_pad = gfx_get_fnl_substitute_y_pad(ts->face, ts->size);
	float edge_advance = gfx_text_advance_edges
		? edge_width + ceilf(ts->bold_width)
		: 0.f;
	int edge_y = (gfx_text_advance_edges
		? lroundf(ts->edge_up + ceilf(ts->bold_width))
		: 0) + fnl_y_pad;
	struct text_render_metrics metrics = {
		.x = x,
		.y = y + edge_y,
		.color = ts->edge_color,
		.weight = weight,
		.edge_width = edge_width,
		.scale_x = ts->scale_x,
		.space_scale_x = ts->space_scale_x,
		.font_spacing = ts->font_spacing,
		.edge_spacing = edge_advance,
		.mode = RENDER_COPY,
		.font_size = font_size,
	};
	return _gfx_render_text(dst, msg, &metrics);
}

float gfx_render_text_core_only(Texture *dst, float x, int y, char *msg, struct text_style *ts)
{
	enum font_weight weight = gfx_int_to_font_weight(ts->weight);
	struct font_size *font_size = text_style_font_size(ts);
	float edge_width = text_style_edge_width(ts);
	int fnl_y_pad = gfx_get_fnl_substitute_y_pad(ts->face, ts->size);
	float edge_advance = gfx_text_advance_edges
		? edge_width + ceilf(ts->bold_width)
		: 0.f;
	int edge_y = (gfx_text_advance_edges
		? lroundf(ts->edge_up + ceilf(ts->bold_width))
		: 0) + fnl_y_pad;
	struct text_render_metrics metrics = {
		.x = x,
		.y = y + edge_y,
		.color = ts->color,
		.weight = weight,
		.edge_width = ts->bold_width,
		.scale_x = ts->scale_x,
		.space_scale_x = ts->space_scale_x,
		.font_spacing = ts->font_spacing,
		.edge_spacing = edge_advance,
		.mode = RENDER_COPY,
		.font_size = font_size,
	};
	return _gfx_render_text(dst, msg, &metrics);
}

float gfx_render_textf(Texture *dst, float x, int y, char *msg, struct text_style *ts, bool blend)
{
	return gfx_render_text_mode(dst, x, y, msg, ts, blend ? RENDER_BLENDED : RENDER_COPY);
}

int gfx_render_text(Texture *dst, float x, int y, char *msg, struct text_style *ts, bool blend)
{
	return lroundf(gfx_render_textf(dst, x, y, msg, ts, blend));
}

void gfx_render_dash_text(Texture *dst, struct text_style *ts)
{
	Texture glyph;
	gfx_init_texture_rgba(&glyph, dst->w, dst->h, COLOR(0, 0, 0, 0));

	// draw a horizontal line
	int x = lroundf(ts->size * 0.12f);
	int w = dst->w - x * 2;
	int h = max(1, lroundf(ts->size * 0.06f));
	int y = (ts->size - h) / 2;
	gfx_fill(&glyph, x, y, w, h, 255, 255, 255);

	float edge_width = text_style_edge_width(ts);
	if (edge_width > 0.01f) {
		gfx_draw_glyph(dst, 0, 0, &glyph, ts->edge_color, 1.f, edge_width, false);
	}
	gfx_draw_glyph(dst, 0, 0, &glyph, ts->color, 1.f, ts->bold_width, false);

	gfx_delete_texture(&glyph);
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

#if defined(ALICEHUB_IOS)
void gfx_font_size_fini(struct font_size *size)
{
	if (!size->glyph_table)
		return;
	ht_foreach_value(size->glyph_table, free_cached_glyph);
	ht_free_int(size->glyph_table);
	size->glyph_table = NULL;
}

void gfx_font_fini(void)
{
	text_sprite_ts.font_size = NULL;
	for (unsigned i = 0; i < MAX_FNL_FONTS; i++) {
		fnl_font_free(font_fnl[i]);
		font_fnl[i] = NULL;
	}
	if (session_fnl)
		fnl_free(session_fnl);
	session_fnl = NULL;
	for (unsigned i = 0; i < sizeof(font_ttf) / sizeof(*font_ttf); i++) {
		ft_font_free(font_ttf[i]);
		font_ttf[i] = NULL;
	}
	ft_font_fini();
	free(glyph_cache_trackers);
	glyph_cache_trackers = NULL;
	nr_glyph_cache_trackers = cap_glyph_cache_trackers = 0;
	memset(fnl_substitute_scales, 0, sizeof(fnl_substitute_scales));
	fnl_substitute_count = 0;
	gfx_text_advance_edges = false;
	font_metrics = (struct font_metrics){ .size = 16, .face = FONT_GOTHIC, .weight = FW_BOLD,
		.color = { 255, 255, 255, 255 } };
	font_initialized = false;
}
#endif

static void gfx_draw_text(Texture *dst, int x, int y, char *text, enum text_render_mode mode)
{
	struct font_size *font_size = gfx_font_get_size(font_metrics.face, font_metrics.size);
	struct text_render_metrics metrics = {
		.x = x,
		.y = y + gfx_get_fnl_substitute_y_pad(font_metrics.face, font_metrics.size),
		.color = font_metrics.color,
		.weight = gfx_int_to_font_weight(font_metrics.weight),
		.edge_width = 0.0f,
		.scale_x = 1.0f,
		.space_scale_x = 1.0f,
		.font_spacing = font_metrics.space,
		.mode = mode,
		.font_size = font_size,
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
	static const char gbk_mincho_name[] = { 0xcb, 0xce, 0xcc, 0xe5, 0 };
	static const char gbk_gothic_name[] = { 0xba, 0xda, 0xcc, 0xe5, 0 };
	static const char gbk_ms_mincho_name[] = { 0xa3, 0xcd, 0xa3, 0xd3, 0x20, 0xc3, 0xf7, 0xb3, 0xaf, 0 };
	static const char gbk_ms_gothic_name[] = { 0xa3, 0xcd, 0xa3, 0xd3, 0x20, 0xa5, 0xb4, 0xa5, 0xb7, 0xa5, 0xc3, 0xa5, 0xaf, 0 };
	if (!strcmp(name, mincho_name) || (sys4_text_is_gbk && (!strcmp(name, gbk_mincho_name) || !strcmp(name, gbk_ms_mincho_name)))) {
		font_metrics.face = FONT_MINCHO;
	} else if (!strcmp(name, gothic_name) || (sys4_text_is_gbk && (!strcmp(name, gbk_gothic_name) || !strcmp(name, gbk_ms_gothic_name)))) {
		font_metrics.face = FONT_GOTHIC;
	} else {
		WARNING("Unhandled font name: \"%s\"", display_sjis0(name));
	}
}
