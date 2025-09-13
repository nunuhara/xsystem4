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

#ifndef SYSTEM4_SACT_H
#define SYSTEM4_SACT_H

#include <stdbool.h>
#include "sprite.h"

enum sprite_engine_type {
	UNINITIALIZED_SPRITE_ENGINE,
	SACT2_SPRITE_ENGINE,
	SACTDX_SPRITE_ENGINE,
	STOAT_SPRITE_ENGINE,
	CHIPMUNK_SPRITE_ENGINE,
};

struct sact_sprite *sact_get_sprite(int sp);
struct sact_sprite *sact_try_get_sprite(int sp);
struct sact_sprite *sact_create_sprite(int sp_no, int width, int height, int r, int g, int b, int a);
void sact_ModuleFini(void);
int sact_init(int cg_cache_size, enum sprite_engine_type engine);
#define sact_SetWP scene_set_wp
#define sact_SetWP_Color scene_set_wp_color
int sact_GetScreenWidth(void);
int sact_GetScreenHeight(void);
int sact_GetMainSurfaceNumber(void);
int sact_Update(void);
int sact_Effect(int type, int time, int key);
int sact_SP_GetUnuseNum(int min);
int sact_SP_Count(void);
int sact_SP_Enum(struct page **array);
int sact_SP_GetMaxZ(void);
int sact_SP_SetCG(int sp, int cg);
int sact_SP_SetCG2X(int sp_no, int cg_no);
int sact_SP_SetCGFromFile(int sp, struct string *filename);
int sact_SP_SaveCG(int sp, struct string *filename);
int sact_SP_Create(int sp, int width, int height, int r, int g, int b, int a);
int sact_SP_CreatePixelOnly(int sp, int width, int height);
int sact_SP_CreateCustom(int sp);
int sact_SP_Delete(int sp);
int sact_SP_DeleteAll(void);
int sact_SP_SetPos(int sp_no, int x, int y);
int sact_SP_SetX(int sp_no, int x);
int sact_SP_SetY(int sp_no, int y);
int sact_SP_SetZ(int sp, int z);
int sact_SP_GetBlendRate(int sp_no);
int sact_SP_SetBlendRate(int sp_no, int rate);
int sact_SP_SetShow(int sp_no, int show);
int sact_SP_SetDrawMethod(int sp_no, int method);
int sact_SP_GetDrawMethod(int sp_no);
int sact_SP_IsUsing(int sp_no);
int sact_SP_ExistsAlpha(int sp_no);
int sact_SP_GetPosX(int sp_no);
int sact_SP_GetPosY(int sp_no);
int sact_SP_GetWidth(int sp_no);
int sact_SP_GetHeight(int sp_no);
int sact_SP_GetZ(int sp_no);
int sact_SP_GetShow(int sp_no);
int sact_SP_SetTextHome(int sp_no, int x, int y);
int sact_SP_SetTextLineSpace(int sp_no, int px);
int sact_SP_SetTextCharSpace(int sp_no, int px);
int sact_SP_SetTextPos(int sp_no, int x, int y);
int _sact_SP_TextDraw(int sp_no, struct string *text, struct text_style *tm);
int sact_SP_TextDraw(int sp_no, struct string *text, struct page *tm);
int sact_SP_TextClear(int sp_no);
int sact_SP_TextHome(int sp_no, int size);
int sact_SP_TextNewLine(int sp_no, int size);
int sact_SP_TextCopy(int dno, int sno);
int sact_SP_GetTextHomeX(int sp_no);
int sact_SP_GetTextHomeY(int sp_no);
int sact_SP_GetTextCharSpace(int sp_no);
int sact_SP_GetTextPosX(int sp_no);
int sact_SP_GetTextPosY(int sp_no);
int sact_SP_GetTextLineSpace(int sp_no);
int sact_SP_IsPtIn(int sp_no, int x, int y);
int sact_SP_IsPtInRect(int sp_no, int x, int y);
int sact_CG_GetMetrics(int cg_no, struct page **page);
int sact_SP_GetAMapValue(int sp_no, int x, int y);
int sact_SP_GetPixelValue(int sp_no, int x, int y, int *r, int *g, int *b);
int sact_SP_SetBrightness(int sp_no, int brightness);
int sact_SP_GetBrightness(int sp_no);
int sact_GAME_MSG_GetNumOf(void);
void sact_IntToZenkaku(struct string **s, int value, int figures, int zero_pad);
void sact_IntToHankaku(struct string **s, int value, int figures, int zero_pad);
int sact_Mouse_GetPos(int *x, int *y);
int sact_Mouse_SetPos(int x, int y);
void sact_Joypad_ClearKeyDownFlag(int n);
int sact_Joypad_IsKeyDown(int num, int key);
bool sact_Joypad_GetAnalogStickStatus(int num, int type, float *degree, float *power);
bool sact_Joypad_GetDigitalStickStatus(int num, int type, bool *left, bool *right, bool *up, bool *down);
int sact_Key_ClearFlag(void);
void sact_Key_ClearFlagNoCtrl(void);
int sact_Key_IsDown(int keycode);
int sact_CG_IsExist(int cg_no);
void sact_System_GetDate(int *year, int *month, int *mday, int *wday);
void sact_System_GetTime(int *hour, int *min, int *sec, int *ms);
void sact_CG_BlendAMapBin(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int border);
int sact_TRANS_Begin(int type);
int sact_TRANS_Update(float rate);
int sact_TRANS_End(void);
bool sact_VIEW_SetMode(int mode);
int sact_VIEW_GetMode(void);
bool sact_DX_GetUsePower2Texture(void);
void sact_DX_SetUsePower2Texture(bool use);

extern struct text_style text_sprite_ts;
bool StoatSpriteEngine_SP_SetTextSprite(int sp_no, struct string *text);
void StoatSpriteEngine_SP_SetTextSpriteType(int type);
void StoatSpriteEngine_SP_SetTextSpriteSize(int size);
void StoatSpriteEngine_SP_SetTextSpriteColor(int r, int g, int b);
void StoatSpriteEngine_SP_SetTextSpriteBoldWeight(float weight);
void StoatSpriteEngine_SP_SetTextSpriteEdgeWeight(float weight);
void StoatSpriteEngine_SP_SetTextSpriteEdgeColor(int r, int g, int b);
bool StoatSpriteEngine_SP_SetDashTextSprite(int sp_no, int width, int height);
void StoatSpriteEngine_FPS_SetShow(bool show);
bool StoatSpriteEngine_FPS_GetShow(void);
float StoatSpriteEngine_FPS_Get(void);
//int StoatSpriteEngine_SP_SetBrightness(int sp_no, int brightness);
//int StoatSpriteEngine_SP_GetBrightness(int sp_no);
int StoatSpriteEngine_SYSTEM_IsResetOnce(void);
void StoatSpriteEngine_SYSTEM_SetConfigOverFrameRateSleep(bool on);
bool StoatSpriteEngine_SYSTEM_GetConfigOverFrameRateSleep(void);
void StoatSpriteEngine_SYSTEM_SetConfigSleepByInactiveWindow(bool on);
bool StoatSpriteEngine_SYSTEM_GetConfigSleepByInactiveWindow(void);
void StoatSpriteEngine_SYSTEM_SetReadMessageSkipping(bool on);
bool StoatSpriteEngine_SYSTEM_GetReadMessageSkipping(void);
void StoatSpriteEngine_SYSTEM_SetConfigFrameSkipWhileMessageSkip(bool on);
bool StoatSpriteEngine_SYSTEM_GetConfigFrameSkipWhileMessageSkip(void);
void StoatSpriteEngine_SYSTEM_SetInvalidateFrameSkipWhileMessageSkip(bool on);
bool StoatSpriteEngine_SYSTEM_GetInvalidateFrameSkipWhileMessageSkip(void);

#endif /* SYSTEM4_SACT_H */
