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

#include "system4/string.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "hll.h"

struct text_metrics sr_tm = {
	.color = { 255, 255, 255, 255 },
	.outline_color = { 0, 0, 0, 255 },
	.size = 16,
	.weight = FW_NORMAL,
	.face = FONT_MINCHO,
};

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
	// FIXME: use sdl_ttf
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

void SengokuRanceFont_SP_ClearState(int spriteNumber)
{
	
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
//void SengokuRanceFont_SP_SetTextPos(int spriteNumber, float xPos, int yPos) {}
//void SengokuRanceFont_SP_TextClear(int spriteNumber) {}
//void SengokuRanceFont_SP_TextCopy(int destSpriteNumber, int sourceSpriteNumber) {}

void SengokuRanceFont_SP_TextHome(int sp_no, float size)
{
	sact_SP_TextHome(sp_no, size);
}

void SengokuRanceFont_SP_TextNewLine(int spriteNumber, float fontSize);
void SengokuRanceFont_SP_SetTextMetricsClassic(int spriteNumber, struct page *tm);

void SengokuRanceFont_SP_SetTextStyle(int spriteNumber, struct page *textStyle)
{
	// ???
}

void SengokuRanceFont_SP_TextDrawClassic(int spriteNumber, struct string *text);

void SengokuRanceFont_SP_TextDraw(int sp_no, struct string *text)
{
	_sact_SP_TextDraw(sp_no, text, &sr_tm);
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

HLL_LIBRARY(SengokuRanceFont,
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
	    HLL_EXPORT(SP_SetTextPos, sact_SP_SetTextPos),
	    HLL_EXPORT(SP_TextClear, sact_SP_TextClear),
	    HLL_EXPORT(SP_TextCopy, sact_SP_TextCopy),
	    HLL_EXPORT(SP_TextHome, SengokuRanceFont_SP_TextHome),
	    //HLL_EXPORT(SP_TextNewLine, SengokuRanceFont_SP_TextNewLine),
	    //HLL_EXPORT(SP_SetTextMetricsClassic, SengokuRanceFont_SP_SetTextMetricsClassic),
	    HLL_EXPORT(SP_SetTextStyle, SengokuRanceFont_SP_SetTextStyle),
	    //HLL_EXPORT(SP_TextDrawClassic, SengokuRanceFont_SP_TextDrawClassic),
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
