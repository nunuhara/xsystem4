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
#include <stdio.h>
#include <dirent.h>
#include <limits.h>

#include "system4.h"
#include "system4/fnl.h"
#include "system4/string.h"

#include "gfx/gfx.h"
#include "vm/page.h"
#include "sact.h"
#include "hll.h"
#include "xsystem4.h"

static struct fnl *fontlib;

struct text_metrics sr_tm = {
	.color = { 255, 255, 255, 255 },
	.outline_color = { 0, 0, 0, 255 },
	.size = 16,
	.weight = FW_NORMAL,
	.face = FONT_MINCHO,
	.outline_left = 1,
	.outline_up = 1,
	.outline_right = 1,
	.outline_down = 1
};

struct text_style *sp_ts;
int nr_ts = 0;

static struct text_style *get_sp_metrics(int sp_no)
{
	if (sp_no > nr_ts) {
		nr_ts = sp_no + 256;
		sp_ts = xrealloc(sp_ts, nr_ts * sizeof(struct text_style));
	}
	return sp_ts + sp_no;
}

static unsigned get_font_type(int type)
{
	unsigned font = type - 256;
	if (font >= fontlib->nr_fonts) {
		WARNING("Invalid font type: %d", type);
		// XXX: Really not sure why this is how it works...
		font = type == 1 ? 2 : 1;
	}
	return font;
}

/*
static unsigned get_font_face(int font, float height)
{
	float min_diff = 9999;
	unsigned closest = 0;

	// FIXME: this isn't right; see comment for GetActualFontSize
	for (unsigned i = 0; i < fontlib->fonts[font].nr_faces; i++) {
		if (fontlib->fonts[font].faces[i].height == height)
			return i;
		float diff = fabsf(height - fontlib->fonts[font].faces[i].height);
		if (diff < min_diff) {
			min_diff = diff;
			closest = i;
		}
	}
	return closest;
}
*/

//static void SengokuRanceFont_AlphaComposite(int spriteNumberDest, int destX, int destY, int spriteNumberSrc, int srcX, int srcY, int w, int h);

static void SengokuRanceFont_SetFontType(int type)
{
	WARNING("SengokuRanceFont.SetFontType(%d)", type);
}

static void SengokuRanceFont_SetFontSize(float size)
{
	sr_tm.size = size;
}

static void SengokuRanceFont_SetFontColor(int r, int g, int b)
{
	sr_tm.color.r = r;
	sr_tm.color.g = g;
	sr_tm.color.b = b;
}

//static void SengokuRanceFont_SetBoldWidth(float boldWidth);

static void SengokuRanceFont_SetEdgeColor(int r, int g, int b)
{
	sr_tm.outline_color.r = r;
	sr_tm.outline_color.g = g;
	sr_tm.outline_color.b = b;
}

static void SengokuRanceFont_SetEdgeWidth(float w)
{
	sr_tm.outline_left = w;
	sr_tm.outline_up = w;
	sr_tm.outline_right = w;
	sr_tm.outline_down = w;
}

static float SengokuRanceFont_GetTextWidth(struct string *str)
{
	return str->size * 8;
}

//static float SengokuRanceFont_GetCharacterWidth(int charCode);

//static void SengokuRanceFont_SetTextX(float x);
//static void SengokuRanceFont_SetTextY(int y);
//static void SengokuRanceFont_SetTextPos(float x, int y);
//static void SengokuRanceFont_SetScaleX(float scaleX);
//static void SengokuRanceFont_DrawTextToSprite(int spriteNumber, struct string *str);
//static void SengokuRanceFont_DrawTextPreload(struct string *str);
//static void SengokuRanceFont_SetFontFileName(struct string *fontName);
//static void SengokuRanceFont_SetCharacterSpacing(float characterSpacing);
//static void SengokuRanceFont_SetSpaceScaleX(float spaceScaleX);
//static void SengokuRanceFont_SetReduceDescender(int reduceDescender);

static void SengokuRanceFont_SP_ClearState(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return;
	sp->text.home.x = 0;
	sp->text.home.y = 0;
	sp->text.pos.x = 0;
	sp->text.pos.y = 0;
}

//static float SengokuRanceFont_SP_GetTextCharSpace(int spriteNumber);
//static float SengokuRanceFont_SP_GetTextHomeX(int spriteNumber);
//static int SengokuRanceFont_SP_GetTextHomeY(int spriteNumber);
//static int SengokuRanceFont_SP_GetTextLineSpace(int spriteNumber);
//static float SengokuRanceFont_SP_GetTextPosX(int spriteNumber);
//static int SengokuRanceFont_SP_GetTextPosY(int spriteNumber);
//static void SengokuRanceFont_SP_SetTextCharSpace(int spriteNumber, float characterSpacing);

static void SengokuRanceFont_SP_SetTextHome(int sp_no, float x, int y)
{
	sact_SP_SetTextHome(sp_no, x, y);
}

//static void SengokuRanceFont_SP_SetTextLineSpace(int spriteNumber, int lineSpacing) {}

static void SengokuRanceFont_SP_SetTextPos(int sp_no, float x, int y)
{
	sact_SP_SetTextPos(sp_no, x, y);
}

//static void SengokuRanceFont_SP_TextClear(int spriteNumber) {}
//static void SengokuRanceFont_SP_TextCopy(int destSpriteNumber, int sourceSpriteNumber) {}

static void SengokuRanceFont_SP_TextHome(int sp_no, float size)
{
	sact_SP_TextHome(sp_no, size);
}

//static void SengokuRanceFont_SP_TextNewLine(int spriteNumber, float fontSize);

static void SengokuRanceFont_SP_SetTextMetricsClassic(int sp_no, struct page *page)
{
	struct text_style *ts = get_sp_metrics(sp_no);
	ts->font_type     = get_font_type(256);
	ts->color.r       = page->values[0].i;
	ts->color.g       = page->values[1].i;
	ts->color.b       = page->values[2].i;
	ts->color.a       = 255;
	ts->size          = page->values[3].i;
	//ts->weight        = page->values[4].i;
	//ts->face          = page->values[5].i;
	ts->edge_width    = max(page->values[6].i, max(page->values[7].i, max(page->values[8].i, page->values[9].i)));
	ts->edge_color.r  = page->values[10].i;
	ts->edge_color.g  = page->values[11].i;
	ts->edge_color.b  = page->values[12].i;
	ts->edge_color.a  = 255;
	ts->bold_width    = 0; // FIXME: use weight?
	ts->scale_x       = 1;
	ts->space_scale_x = 1;
	ts->font_spacing  = 1;
	ts->font_size     = fnl_get_font_size(&fontlib->fonts[ts->font_type], ts->size);
}

static void SengokuRanceFont_SP_SetTextStyle(int sp_no, struct page *page)
{
	struct text_style *ts = get_sp_metrics(sp_no);
	ts->font_type          = get_font_type(page->values[0].i < 0 ? 256 : page->values[0].i);
	ts->size               = page->values[1].f;
	ts->bold_width         = page->values[2].f < 0 ? 0 : page->values[2].f;
	ts->color.r            = page->values[3].i < 0 ? 255 : page->values[3].i;
	ts->color.g            = page->values[4].i < 0 ? 255 : page->values[4].i;
	ts->color.b            = page->values[5].i < 0 ? 255 : page->values[5].i;
	ts->color.a            = 255;
	ts->edge_width         = page->values[6].f < 0 ? 0 : page->values[6].f;
	ts->edge_color.r       = page->values[7].i < 0 ? 0 : page->values[7].i;
	ts->edge_color.g       = page->values[8].i < 0 ? 0 : page->values[8].i;
	ts->edge_color.b       = page->values[9].i < 0 ? 0 : page->values[9].i;
	ts->edge_color.a       = 255;
	ts->scale_x            = page->values[10].f < 0 ? 1 : page->values[10].f;
	ts->space_scale_x      = page->values[11].f < 0 ? 1 : page->values[11].f;
	ts->font_spacing       = page->values[12].f < 0 ? 1 : page->values[12].f;
	ts->font_size          = fnl_get_font_size(&fontlib->fonts[ts->font_type], ts->size);

	if (ts->scale_x != 1.0) {
		WARNING("X SCALE = %f", ts->scale_x);
	}

}

static void SengokuRanceFont_SP_TextDraw(int sp_no, struct string *text)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	struct text_style *ts = get_sp_metrics(sp_no);

	// in case of uninitialized text style...
	if (ts->font_type >= fontlib->nr_fonts) {
		ts->font_type = 0;
	}
	if (ts->size < 8) {
		ts->size = 8;
	}
	if (!ts->font_size) {
		ts->font_size = fnl_get_font_size(&fontlib->fonts[ts->font_type], ts->size);
	}

	if (!sp->text.texture.handle) {
		SDL_Color c = ts->color;
		c.a = 0;
		gfx_init_texture_rgba(&sp->text.texture, sp->rect.w, sp->rect.h, c);
	}

	sp->text.pos.x += fnl_draw_text(fontlib, ts, &sp->text.texture, sp->text.pos.x, sp->text.pos.y, text->text);
}

//static void SengokuRanceFont_SP_TextDrawPreload(int spriteNumber, struct string *text);
//static void SengokuRanceFont_SP_SetFontSize(int spriteNumber, float fontSize);
//static void SengokuRanceFont_SP_SetFontType(int spriteNumber, int fontType);
//static void SengokuRanceFont_SP_SetFontColor(int spriteNumber, int r, int g, int b);
//static void SengokuRanceFont_SP_SetBoldWidth(int spriteNumber, float boldWidth);
//static void SengokuRanceFont_SP_SetEdgeColor(int spriteNumber, int r, int g, int b);
//static void SengokuRanceFont_SP_SetEdgeWidth(int spriteNumber, float edgeWidth);
//static void SengokuRanceFont_SP_SetScaleX(int spriteNumber, float scaleX);
//static void SengokuRanceFont_SP_SetSpaceScaleX(int spriteNumber, float spaceScaleX);
//static float SengokuRanceFont_SP_GetFontSize(int spriteNumber);
//static int SengokuRanceFont_SP_GetFontType(int spriteNumber);
//static void SengokuRanceFont_SP_GetFontColor(int spriteNumber, int *r, int *g, int *b);
//static float SengokuRanceFont_SP_GetBoldWidth(int spriteNumber);
//static void SengokuRanceFont_SP_GetEdgeColor(int spriteNumber, int *r, int *g, int *b);
//static float SengokuRanceFont_SP_GetEdgeWidth(int spriteNumber);
//static float SengokuRanceFont_SP_GetScaleX(int spriteNumber);
//static float SengokuRanceFont_SP_GetSpaceScaleX(int spriteNumber);

static float SengokuRanceFont_GetActualFontSize(int font_type, float font_size)
{
	font_type = get_font_type(font_type);
	return fnl_get_font_size(&fontlib->fonts[font_type], font_size)->size;
}

//static float SengokuRanceFont_GetActualFontSizeRoundDown(int fontType, float fontSize);
//static int SengokuRanceFont_SP_GetTextLastCharCode(int spriteNumber);
//static void SengokuRanceFont_SP_SetTextLastCharCode(int spriteNumber, int lastChar);
//static int SengokuRanceFont_SP_GetReduceDescender(int spriteNumber);

static void SengokuRanceFont_SP_SetReduceDescender(int spriteNumber, int reduceDescender)
{
	WARNING("SengokuRanceFont.SP_SetReduceDescender(%d, %d)", spriteNumber, reduceDescender);
}

static void SengokuRanceFont_ModuleInit(void)
{
	DIR *dir;
	struct dirent *d;
	char path[PATH_MAX];

	if (!(dir = opendir(config.game_dir))) {
		ERROR("Failed to open directory: %s", config.game_dir);
	}

	while ((d = readdir(dir))) {
		size_t name_len = strlen(d->d_name);
		if (name_len > 4 && strcasecmp(d->d_name+name_len-4, ".fnl"))
			continue;

		snprintf(path, PATH_MAX, "%s/%s", config.game_dir, d->d_name);
		fontlib = fnl_open(path);
		if (!(fontlib = fnl_open(path)))
			ERROR("Error opening font library '%s'", path);
		if (fontlib->nr_fonts < 1)
			ERROR("Font library doesn't contain any fonts");
		break;
	}

	closedir(dir);
}

HLL_LIBRARY(SengokuRanceFont,
	    HLL_EXPORT(_ModuleInit, SengokuRanceFont_ModuleInit),
	    //HLL_EXPORT(AlphaComposite, SengokuRanceFont_AlphaComposite),
	    HLL_EXPORT(SetFontType, SengokuRanceFont_SetFontType),
	    HLL_EXPORT(SetFontSize, SengokuRanceFont_SetFontSize),
	    HLL_EXPORT(SetFontColor, SengokuRanceFont_SetFontColor),
	    //HLL_EXPORT(SetBoldWidth, SengokuRanceFont_SetBoldWidth),
	    HLL_EXPORT(SetEdgeColor, SengokuRanceFont_SetEdgeColor),
	    HLL_EXPORT(SetEdgeWidth, SengokuRanceFont_SetEdgeWidth),
	    HLL_EXPORT(GetTextWidth, SengokuRanceFont_GetTextWidth),
	    //HLL_EXPORT(GetCharacterWidth, SengokuRanceFont_GetCharacterWidth),
	    //HLL_EXPORT(SetTextX, SengokuRanceFont_SetTextX),
	    //HLL_EXPORT(SetTextY, SengokuRanceFont_SetTextY),
	    //HLL_EXPORT(SetTextPos, SengokuRanceFont_SetTextPos),
	    //HLL_EXPORT(SetScaleX, SengokuRanceFont_SetScaleX),
	    //HLL_EXPORT(DrawTextToSprite, SengokuRanceFont_DrawTextToSprite),
	    //HLL_EXPORT(DrawTextPreload, SengokuRanceFont_DrawTextPreload),
	    //HLL_EXPORT(SetFontFileName, SengokuRanceFont_SetFontFileName),
	    //HLL_EXPORT(SetCharacterSpacing, SengokuRanceFont_SetCharacterSpacing),
	    //HLL_EXPORT(SetSpaceScaleX, SengokuRanceFont_SetSpaceScaleX),
	    //HLL_EXPORT(SetReduceDescender, SengokuRanceFont_SetReduceDescender),
	    HLL_EXPORT(SP_ClearState, SengokuRanceFont_SP_ClearState),
	    //HLL_EXPORT(SP_GetTextCharSpace, SengokuRanceFont_SP_GetTextCharSpace),
	    //HLL_EXPORT(SP_GetTextHomeX, SengokuRanceFont_SP_GetTextHomeX),
	    //HLL_EXPORT(SP_GetTextHomeY, SengokuRanceFont_SP_GetTextHomeY),
	    //HLL_EXPORT(SP_GetTextLineSpace, SengokuRanceFont_SP_GetTextLineSpace),
	    //HLL_EXPORT(SP_GetTextPosX, SengokuRanceFont_SP_GetTextPosX),
	    //HLL_EXPORT(SP_GetTextPosY, SengokuRanceFont_SP_GetTextPosY),
	    //HLL_EXPORT(SP_SetTextCharSpace, SengokuRanceFont_SP_SetTextCharSpace),
	    HLL_EXPORT(SP_SetTextHome, SengokuRanceFont_SP_SetTextHome),
	    HLL_EXPORT(SP_SetTextLineSpace, sact_SP_SetTextLineSpace),
	    HLL_EXPORT(SP_SetTextPos, SengokuRanceFont_SP_SetTextPos),
	    HLL_EXPORT(SP_TextClear, sact_SP_TextClear),
	    HLL_EXPORT(SP_TextCopy, sact_SP_TextCopy),
	    HLL_EXPORT(SP_TextHome, SengokuRanceFont_SP_TextHome),
	    //HLL_EXPORT(SP_TextNewLine, SengokuRanceFont_SP_TextNewLine),
	    HLL_EXPORT(SP_SetTextMetricsClassic, SengokuRanceFont_SP_SetTextMetricsClassic),
	    HLL_EXPORT(SP_SetTextStyle, SengokuRanceFont_SP_SetTextStyle),
	    // XXX: I'm really not sure what the difference is supposed to be between these two
	    HLL_EXPORT(SP_TextDrawClassic, SengokuRanceFont_SP_TextDraw),
	    HLL_EXPORT(SP_TextDraw, SengokuRanceFont_SP_TextDraw),
	    //HLL_EXPORT(SP_TextDrawPreload, SengokuRanceFont_SP_TextDrawPreload),
	    //HLL_EXPORT(SP_SetFontSize, SengokuRanceFont_SP_SetFontSize),
	    //HLL_EXPORT(SP_SetFontType, SengokuRanceFont_SP_SetFontType),
	    //HLL_EXPORT(SP_SetFontColor, SengokuRanceFont_SP_SetFontColor),
	    //HLL_EXPORT(SP_SetBoldWidth, SengokuRanceFont_SP_SetBoldWidth),
	    //HLL_EXPORT(SP_SetEdgeColor, SengokuRanceFont_SP_SetEdgeColor),
	    //HLL_EXPORT(SP_SetEdgeWidth, SengokuRanceFont_SP_SetEdgeWidth),
	    //HLL_EXPORT(SP_SetScaleX, SengokuRanceFont_SP_SetScaleX),
	    //HLL_EXPORT(SP_SetSpaceScaleX, SengokuRanceFont_SP_SetSpaceScaleX),
	    //HLL_EXPORT(SP_GetFontSize, SengokuRanceFont_SP_GetFontSize),
	    //HLL_EXPORT(SP_GetFontType, SengokuRanceFont_SP_GetFontType),
	    //HLL_EXPORT(SP_GetFontColor, SengokuRanceFont_SP_GetFontColor),
	    //HLL_EXPORT(SP_GetBoldWidth, SengokuRanceFont_SP_GetBoldWidth),
	    //HLL_EXPORT(SP_GetEdgeColor, SengokuRanceFont_SP_GetEdgeColor),
	    //HLL_EXPORT(SP_GetEdgeWidth, SengokuRanceFont_SP_GetEdgeWidth),
	    //HLL_EXPORT(SP_GetScaleX, SengokuRanceFont_SP_GetScaleX),
	    //HLL_EXPORT(SP_GetSpaceScaleX, SengokuRanceFont_SP_GetSpaceScaleX),
	    HLL_EXPORT(GetActualFontSize, SengokuRanceFont_GetActualFontSize),
	    //HLL_EXPORT(GetActualFontSizeRoundDown, SengokuRanceFont_GetActualFontSizeRoundDown),
	    //HLL_EXPORT(SP_GetTextLastCharCode, SengokuRanceFont_SP_GetTextLastCharCode),
	    //HLL_EXPORT(SP_SetTextLastCharCode, SengokuRanceFont_SP_SetTextLastCharCode),
	    //HLL_EXPORT(SP_GetReduceDescender, SengokuRanceFont_SP_GetReduceDescender),
	    HLL_EXPORT(SP_SetReduceDescender, SengokuRanceFont_SP_SetReduceDescender)
	);
