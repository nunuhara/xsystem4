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
#include "system4.h"
#include "system4/fnl.h"
#include "system4/string.h"
#include "gfx/gfx.h"
#include "vm/page.h"
#include "sact.h"
#include "kvec.h"
#include "hll.h"

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

kvec_t(struct text_metrics) sp_tm;

static struct text_metrics *get_sp_metrics(int sp_no)
{
	(void)kv_a(struct text_metrics, sp_tm, sp_no);
	return &kv_A(sp_tm, sp_no);
}

void SengokuRanceFont_AlphaComposite(int spriteNumberDest, int destX, int destY, int spriteNumberSrc, int srcX, int srcY, int w, int h);

void SengokuRanceFont_SetFontType(int type)
{
	WARNING("SengokuRanceFont.SetFontType(%d)", type);
}

void SengokuRanceFont_SetFontSize(float size)
{
	sr_tm.size = size;
}

void SengokuRanceFont_SetFontColor(int r, int g, int b)
{
	sr_tm.color.r = r;
	sr_tm.color.g = g;
	sr_tm.color.b = b;
}

void SengokuRanceFont_SetBoldWidth(float boldWidth)
{
	// ???
}

void SengokuRanceFont_SetEdgeColor(int r, int g, int b)
{
	sr_tm.outline_color.r = r;
	sr_tm.outline_color.g = g;
	sr_tm.outline_color.b = b;
}

void SengokuRanceFont_SetEdgeWidth(float w)
{
	sr_tm.outline_left = w;
	sr_tm.outline_up = w;
	sr_tm.outline_right = w;
	sr_tm.outline_down = w;
}

float SengokuRanceFont_GetTextWidth(struct string *str)
{
	return str->size * 8;
}

float SengokuRanceFont_GetCharacterWidth(int charCode)
{
	return 8;
}

void SengokuRanceFont_SetTextX(float x);
void SengokuRanceFont_SetTextY(int y);
void SengokuRanceFont_SetTextPos(float x, int y);
void SengokuRanceFont_SetScaleX(float scaleX);
void SengokuRanceFont_DrawTextToSprite(int spriteNumber, struct string *str);
void SengokuRanceFont_DrawTextPreload(struct string *str);
void SengokuRanceFont_SetFontFileName(struct string *fontName);
void SengokuRanceFont_SetCharacterSpacing(float characterSpacing);
void SengokuRanceFont_SetSpaceScaleX(float spaceScaleX);
void SengokuRanceFont_SetReduceDescender(int reduceDescender);

void SengokuRanceFont_SP_ClearState(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return;
	sp->text.home.x = 0;
	sp->text.home.y = 0;
	sp->text.pos.x = 0;
	sp->text.pos.y = 0;
}

float SengokuRanceFont_SP_GetTextCharSpace(int spriteNumber);
float SengokuRanceFont_SP_GetTextHomeX(int spriteNumber);
int SengokuRanceFont_SP_GetTextHomeY(int spriteNumber);
int SengokuRanceFont_SP_GetTextLineSpace(int spriteNumber);
float SengokuRanceFont_SP_GetTextPosX(int spriteNumber);
int SengokuRanceFont_SP_GetTextPosY(int spriteNumber);
void SengokuRanceFont_SP_SetTextCharSpace(int spriteNumber, float characterSpacing);

void SengokuRanceFont_SP_SetTextHome(int sp_no, float x, int y)
{
	sact_SP_SetTextHome(sp_no, x, y);
}

//void SengokuRanceFont_SP_SetTextLineSpace(int spriteNumber, int lineSpacing) {}

void SengokuRanceFont_SP_SetTextPos(int sp_no, float x, int y)
{
	sact_SP_SetTextPos(sp_no, x, y);
}

//void SengokuRanceFont_SP_TextClear(int spriteNumber) {}
//void SengokuRanceFont_SP_TextCopy(int destSpriteNumber, int sourceSpriteNumber) {}

void SengokuRanceFont_SP_TextHome(int sp_no, float size)
{
	sact_SP_TextHome(sp_no, size);
}

void SengokuRanceFont_SP_TextNewLine(int spriteNumber, float fontSize);

void SengokuRanceFont_SP_SetTextMetricsClassic(int sp_no, struct page *page)
{
	struct text_metrics *tm = get_sp_metrics(sp_no);
	tm->color.r         = page->values[0].i;
	tm->color.g         = page->values[1].i;
	tm->color.b         = page->values[2].i;
	tm->color.a         = 255;
	tm->size            = page->values[3].i;
	tm->weight          = page->values[4].i;
	//tm->face            = page->values[5].i;
	tm->face            = FONT_MINCHO;
	tm->outline_left    = page->values[6].i;
	tm->outline_up      = page->values[7].i;
	tm->outline_right   = page->values[8].i;
	tm->outline_down    = page->values[9].i;
	tm->outline_color.r = page->values[10].i;
	tm->outline_color.g = page->values[11].i;
	tm->outline_color.b = page->values[12].i;
	tm->outline_color.a = 255;
}

static float boldwidth = 0;

void SengokuRanceFont_SP_SetTextStyle(int sp_no, struct page *ts)
{
	struct text_metrics *tm = get_sp_metrics(sp_no);
	// FontType
	tm->weight = FW_NORMAL;
	tm->face = FONT_MINCHO;
	tm->size            = ts->values[1].f;
	// BoldWidth
	boldwidth = ts->values[2].f;
	tm->color.r         = ts->values[3].i;
	tm->color.g         = ts->values[4].i;
	tm->color.b         = ts->values[5].i;
	tm->color.a         = 255;
	tm->outline_left    = ts->values[6].f;
	tm->outline_up      = ts->values[6].f;
	tm->outline_right   = ts->values[6].f;
	tm->outline_down    = ts->values[6].f;
	tm->outline_color.r = ts->values[7].i;
	tm->outline_color.g = ts->values[8].i;
	tm->outline_color.b = ts->values[9].i;
	tm->outline_color.a = 255;
	// ScaleX
	// SpaceScaleX
	// FontSpacing
}

void SengokuRanceFont_SP_TextDrawClassic(int sp_no, struct string *text)
{
	_sact_SP_TextDraw(sp_no, text, &sr_tm);
}

static struct fnl_font_face *get_font(uint32_t height)
{
	int min_diff = 9999;
	struct fnl_font_face *closest = NULL;
	for (uint32_t i = 0; i < fontlib->fonts[0].nr_faces; i++) {
		if (fontlib->fonts[0].faces[i].height == height)
			return &fontlib->fonts[0].faces[i];
		int diff = abs((int)height - (int)fontlib->fonts[0].faces[i].height);
		if (diff < min_diff) {
			min_diff = diff;
			closest = &fontlib->fonts[0].faces[i];
		}
	}
	return closest;
}

void SengokuRanceFont_SP_TextDraw(int sp_no, struct string *text)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	struct text_metrics *tm = get_sp_metrics(sp_no);
	struct fnl_font_face *font = get_font(tm->size);
	if (!font) {
		WARNING("No font for size %u", tm->size);
		return;
	}

	if (!sp->text.texture.handle) {
		SDL_Color c = tm->color;
		c.a = 0;
		gfx_init_texture_with_color(&sp->text.texture, sp->rect.w, sp->rect.h, c);
	}

	struct fnl_font_inst inst = {
		.font = font,
		.scale = 1.0 / (font->height / tm->size),
		.color = tm->color,
		.outline_color = tm->outline_color,
		.outline_size = tm->outline_left
	};
	sp->text.pos.x += fnl_draw_text(&inst, &sp->text.texture, sp->text.pos.x, sp->text.pos.y, text->text);
}

void SengokuRanceFont_SP_TextDrawPreload(int spriteNumber, struct string *text);
void SengokuRanceFont_SP_SetFontSize(int spriteNumber, float fontSize);
void SengokuRanceFont_SP_SetFontType(int spriteNumber, int fontType);
void SengokuRanceFont_SP_SetFontColor(int spriteNumber, int r, int g, int b);
void SengokuRanceFont_SP_SetBoldWidth(int spriteNumber, float boldWidth);
void SengokuRanceFont_SP_SetEdgeColor(int spriteNumber, int r, int g, int b);
void SengokuRanceFont_SP_SetEdgeWidth(int spriteNumber, float edgeWidth);
void SengokuRanceFont_SP_SetScaleX(int spriteNumber, float scaleX);
void SengokuRanceFont_SP_SetSpaceScaleX(int spriteNumber, float spaceScaleX);
float SengokuRanceFont_SP_GetFontSize(int spriteNumber);
int SengokuRanceFont_SP_GetFontType(int spriteNumber);
void SengokuRanceFont_SP_GetFontColor(int spriteNumber, int *r, int *g, int *b);
float SengokuRanceFont_SP_GetBoldWidth(int spriteNumber);
void SengokuRanceFont_SP_GetEdgeColor(int spriteNumber, int *r, int *g, int *b);
float SengokuRanceFont_SP_GetEdgeWidth(int spriteNumber);
float SengokuRanceFont_SP_GetScaleX(int spriteNumber);
float SengokuRanceFont_SP_GetSpaceScaleX(int spriteNumber);

float SengokuRanceFont_GetActualFontSize(int fontType, float fontSize)
{
	return fontSize;
}

float SengokuRanceFont_GetActualFontSizeRoundDown(int fontType, float fontSize);
int SengokuRanceFont_SP_GetTextLastCharCode(int spriteNumber);
void SengokuRanceFont_SP_SetTextLastCharCode(int spriteNumber, int lastChar);
int SengokuRanceFont_SP_GetReduceDescender(int spriteNumber);

void SengokuRanceFont_SP_SetReduceDescender(int spriteNumber, int reduceDescender)
{
	
}

void SengokuRanceFont_ModuleInit(void)
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
		break;
	}

	closedir(dir);
}

HLL_LIBRARY(SengokuRanceFont,
	    HLL_EXPORT(_ModuleInit, SengokuRanceFont_ModuleInit),
	    //HLL_EXPORT(AlphaComposite, SengokuRanceFont_AlphaComposite),
	    HLL_EXPORT(SetFontType, SengokuRanceFont_SetFontType),
	    HLL_EXPORT(SetFontSize, SengokuRanceFont_SetFontSize),
	    //HLL_EXPORT(SetFontColor, SengokuRanceFont_SetFontColor),
	    //HLL_EXPORT(SetBoldWidth, SengokuRanceFont_SetBoldWidth),
	    //HLL_EXPORT(SetEdgeColor, SengokuRanceFont_SetEdgeColor),
	    //HLL_EXPORT(SetEdgeWidth, SengokuRanceFont_SetEdgeWidth),
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
	    HLL_EXPORT(SP_TextDrawClassic, SengokuRanceFont_SP_TextDrawClassic),
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
