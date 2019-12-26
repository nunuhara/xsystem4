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
#include <time.h>
#include "hll.h"
#include "../audio.h"
#include "../cg.h"
#include "../input.h"
#include "../queue.h"
#include "../gfx_core.h"
#include "../sact.h"
#include "../system4.h"
#include "../vm.h"

// int Init(imain_system pIMainSystem, int nCGCacheSize)
hll_defun_inline(Init, sact_Init());
// int Error(string strErr)
hll_unimplemented(SACT2, Error);
// int SetWP(int nCG)
hll_defun_inline(SetWP, sact_SetWP(a[0].i));
// int SetWP_Color(int nR, int nG, int nB)
hll_defun_inline(SetWP_Color, (gfx_set_clear_color(a[0].i, a[1].i, a[2].i, 255), 1));
// int WP_GetSP(int nSP)
hll_unimplemented(SACT2, WP_GetSP);
// int WP_SetSP(int nSP)
hll_unimplemented(SACT2, WP_SetSP);
// int GetScreenWidth(void)
hll_defun_inline(GetScreenWidth, config.view_width);
// int GetScreenHeight(void)
hll_defun_inline(GetScreenHeight, config.view_height);
// int GetMainSurfaceNumber(void)
hll_defun_inline(GetMainSurfaceNumber, -1);
// int Update(void)
hll_defun_inline(Update, sact_Update());
// int Effect(int nType, int nTime, int nfKey)
hll_defun_inline(Effect, sact_Effect(a[0].i, a[1].i, a[2].i));
// int EffectSetMask(int nCG)
hll_warn_unimplemented(SACT2, EffectSetMask, 1);
// int EffectSetMaskSP(int nSP)
hll_unimplemented(SACT2, EffectSetMaskSP);
// void QuakeScreen(int nAmplitudeX, int nAmplitudeY, int nTime, int nfKey)
hll_warn_unimplemented(SACT2, QuakeScreen, 0);
// void QUAKE_SET_CROSS(int nAmpX, int nAmpY)
hll_unimplemented(SACT2, QUAKE_SET_CROSS);
// void QUAKE_SET_ROTATION(int nAmp, int nCycle)
hll_unimplemented(SACT2, QUAKE_SET_ROTATION);
// int SP_GetUnuseNum(int nMin)
hll_defun_inline(SP_GetUnuseNum, sact_SP_GetUnuseNum(a[0].i));
// int SP_Count(void)
hll_defun_inline(SP_Count, sact_SP_Count());

// int SP_Enum(ref array@int anSP)
hll_defun(SP_Enum, args)
{
	int size;
	union vm_value *array = hll_array_ref(args[0].i, &size);
	hll_return(sact_SP_Enum(array, size));
}

// int SP_GetMaxZ(void)
hll_defun_inline(SP_GetMaxZ, sact_SP_GetMaxZ());
// int SP_SetCG(int nSP, int nCG)
hll_defun_inline(SP_SetCG, sact_SP_SetCG(a[0].i, a[1].i));
// int SP_SetCGFromFile(int nSP, string pIStringFileName)
hll_unimplemented(SACT2, SP_SetCGFromFile);
// int SP_SaveCG(int nSP, string pIStringFileName)
hll_unimplemented(SACT2, SP_SaveCG);
// int SP_Create(int nSP, int nWidth, int nHeight, int nR, int nG, int nB, int nBlendRate)
hll_defun_inline(SP_Create, sact_SP_Create(a[0].i, a[1].i, a[2].i, a[3].i, a[4].i, a[5].i, a[6].i));
// int SP_CreatePixelOnly(int nSP, int nWidth, int nHeight)
hll_defun_inline(SP_CreatePixelOnly, sact_SP_CreatePixelOnly(a[0].i, a[1].i, a[2].i));
// int SP_CreateCustom(int nSP)
hll_unimplemented(SACT2, SP_CreateCustom);
// int SP_Delete(int nSP)
hll_defun_inline(SP_Delete, sact_SP_Delete(a[0].i));

// int SP_SetPos(int nSP, int nX, int nY)
hll_defun(SP_SetPos, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(1);
	sp->rect.x = args[1].i;
	sp->rect.y = args[2].i;
	hll_return(1);
}

// int SP_SetX(int nSP, int nX)
hll_defun(SP_SetX, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	sp->rect.x = args[1].i;
	hll_return(1);
}

// int SP_SetY(int nSP, int nY)
hll_defun(SP_SetY, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	sp->rect.y = args[1].i;
	hll_return(1);
}

// int SP_SetZ(int nSP, int nZ)
hll_defun_inline(SP_SetZ, sact_SP_SetZ(a[0].i, a[1].i));

// int SP_SetBlendRate(int nSP, int nBlendRate)
hll_defun_inline(SP_SetBlendRate, sact_SP_SetBlendRate(a[0].i, a[1].i));

// int SP_SetShow(int nSP, int nfShow)
hll_defun_inline(SP_SetShow, sact_SP_SetShow(a[0].i, a[1].i));
// int SP_SetDrawMethod(int nSP, int nMethod)
hll_defun_inline(SP_SetDrawMethod, sact_SP_SetDrawMethod(a[0].i, a[1].i));

// int SP_IsUsing(int nSP)
hll_defun_inline(SP_IsUsing, sact_get_sprite(a[0].i) != NULL);

// int SP_ExistAlpha(int nSP)
hll_defun_inline(SP_ExistAlpha, sact_SP_ExistsAlpha(a[0].i));

// int SP_GetPosX(int nSP)
hll_defun(SP_GetPosX, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->rect.x);
}

// int SP_GetPosY(int nSP)
hll_defun(SP_GetPosY, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->rect.y);
}

// int SP_GetWidth(int nSP)
hll_defun(SP_GetWidth, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->rect.w);
}

// int SP_GetHeight(int nSP)
hll_defun(SP_GetHeight, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->rect.h);
}

// int SP_GetZ(int nSP)
hll_defun(SP_GetZ, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->z);
}

// int SP_GetBlendRate(int nSP)
hll_defun_inline(SP_GetBlendRate, sact_SP_GetBlendRate(a[0].i));

// int SP_GetShow(int nSP)
hll_defun(SP_GetShow, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->show);
}

// int SP_GetDrawMethod(int nSP)
hll_defun_inline(SP_GetDrawMethod, sact_SP_GetDrawMethod(a[0].i));

// int SP_SetTextHome(int nSP, int nX, int nY)
hll_defun(SP_SetTextHome, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	sp->text.home = (Point) { .x = args[1].i, .y = args[2].i };
	hll_return(1);
}

// int SP_SetTextLineSpace(int nSP, int nPx)
hll_defun(SP_SetTextLineSpace, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(1);
	sp->text.line_space = args[1].i;
	hll_return(1);
}

// int SP_SetTextCharSpace(int nSP, int nPx)
hll_defun(SP_SetTextCharSpace, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(1);
	sp->text.char_space = args[1].i;
	hll_return(1);
}

// int SP_SetTextPos(int nSP, int nX, int nY)
hll_defun(SP_SetTextPos, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	sp->text.pos = (Point) { .x = args[1].i, .y = args[2].i };
	hll_return(1);
}

// int SP_TextDraw(int nSP, string text, struct tm)
hll_defun_inline(SP_TextDraw, sact_SP_TextDraw(a[0].i, heap[a[1].i].s, heap[a[2].i].page->values));
// int SP_TextClear(int nSP)
hll_defun_inline(SP_TextClear, sact_SP_TextClear(a[0].i));

// int SP_TextHome(int nSP, int nTextSize)
hll_defun(SP_TextHome, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	// FIXME: do something with nTextSize
	sp->text.pos = sp->text.home;
	hll_return(1);
}

// int SP_TextNewLine(int nSP, int nTextSize)
hll_defun(SP_TextNewLine, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	sp->text.pos = POINT(sp->text.home.x, sp->text.home.y + args[0].i);
	hll_return(1);
}

// int SP_TextBackSpace(int nSP)
hll_unimplemented(SACT2, SP_TextBackSpace);
// int SP_TextCopy(int nDstSP, int nSrcSP)
hll_defun_inline(SP_TextCopy, sact_SP_TextCopy(a[0].i, a[1].i));

// int SP_GetTextHomeX(int nSP)
hll_defun(SP_GetTextHomeX, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->text.home.x);
}

// int SP_GetTextHomeY(int nSP)
hll_defun(SP_GetTextHomeY, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->text.home.y);
}

// int SP_GetTextCharSpace(int nSP)
hll_defun(SP_GetTextCharSpace, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->text.char_space);
}

// int SP_GetTextPosX(int nSP)
hll_defun(SP_GetTextPosX, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->text.pos.x);
}

// int SP_GetTextPosY(int nSP)
hll_defun(SP_GetTextPosY, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->text.pos.y);
}

// int SP_GetTextLineSpace(int nSP)
hll_defun(SP_GetTextLineSpace, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	if (!sp) hll_return(0);
	hll_return(sp->text.line_space);
}
// int SP_IsPtIn(int nSP, int nX, int nY)
hll_defun_inline(SP_IsPtIn, sact_SP_IsPtIn(a[0].i, a[1].i, a[2].i));

// int SP_IsPtInRect(int nSP, int nX, int nY)
hll_defun(SP_IsPtInRect, args)
{
	struct sact_sprite *sp = sact_get_sprite(args[0].i);
	Point p = { .x = args[1].i, .y = args[2].i };
	if (!sp) hll_return(0);
	hll_return(!!SDL_PointInRect(&p, &sp->rect));
}

// int GAME_MSG_GetNumof(void)
hll_defun_inline(GAME_MSG_GetNumof, ain->nr_messages);
// void GAME_MSG_Get(int nIndex, ref string text)
hll_unimplemented(SACT2, GAME_MSG_Get);

// void IntToZenkaku(ref string s, int nValue, int nFigures, int nfZeroPadding)
hll_defun(IntToZenkaku, args)
{
	int slot = args[0].i;
	int val = args[1].i;
	int fig = args[2].i;
	bool zero_pad = args[3].i;
	char buf[512];
	int i;

	if (heap[slot].s)
		free_string(heap[slot].s);

	i = int_to_cstr(buf, 1024, val, fig, zero_pad, true);
	heap[slot].s = make_string(buf, i);
	hll_return(0);
}

// void IntToHankaku(ref string s, int nValue, int nFigures, int nfZeroPadding)
hll_defun(IntToHankaku, args)
{
	int slot = args[0].i;
	int val = args[1].i;
	int fig = args[2].i;
	bool zero_pad = args[3].i;
	char buf[512];
	int i;

	if (heap[slot].s)
		free_string(heap[slot].s);

	i = int_to_cstr(buf, 1024, val, fig, zero_pad, false);
	heap[slot].s = make_string(buf, i);
	hll_return(0);
}

// int StringPopFront(ref string sDst, ref string sSrc)
hll_unimplemented(SACT2, StringPopFront);

// int Mouse_GetPos(ref int pnX, ref int pnY)
hll_defun(Mouse_GetPos, args)
{
	handle_events();
	mouse_get_pos(args[0].iref, args[1].iref);
	hll_return(mouse_focus && keyboard_focus);
}

// int Mouse_SetPos(int nX, int nY)
hll_warn_unimplemented(SACT2, Mouse_SetPos, 1);
// void Mouse_ClearWheel(void)
hll_defun_inline(Mouse_ClearWheel, 0);

// void Mouse_GetWheel(ref int pnForward, ref int pnBack)
hll_defun(Mouse_GetWheel, args)
{
	//hll_unimplemented_warning(SACT2, Mouse_GetWheel);
	*args[0].iref = 0;
	*args[1].iref = 0;
	hll_return(0);
}

// void Joypad_ClearKeyDownFlag(int nNum)
hll_defun_inline(Joypad_ClearKeyDownFlag, 0);
// int Joypad_IsKeyDown(int nNum, int nKey)
hll_defun_inline(Joypad_IsKeyDown, 0);
// int Joypad_GetNumof(void)
hll_unimplemented(SACT2, Joypad_GetNumof);
// void JoypadQuake_Set(int nNum, int nType, int nMagnitude)
hll_warn_unimplemented(SACT2, JoypadQuake_Set, 0);

// bool Joypad_GetAnalogStickStatus(int nNum, int nType, ref float pfDegree, ref float pfPower)
hll_defun(Joypad_GetAnalogStickStatus, args)
{
	hll_unimplemented_warning(SACT2, Joypad_GetAnalogStickStatus);
	*args[2].fref = 0.0;
	*args[3].fref = 0.0;
	hll_return(1);
}

// bool Joypad_GetDigitalStickStatus(int nNum, int nType, ref bool pbLeft, ref bool pbRight, ref bool pbUp, ref bool pbDown)
hll_defun(Joypad_GetDigitalStickStatus, args)
{
	//hll_unimplemented_warning(SACT2, Joypad_GetDigitalStickStatus);
	*args[2].iref = 0;
	*args[3].iref = 0;
	*args[4].iref = 0;
	*args[5].iref = 0;
	hll_return(1);
}

// int Key_ClearFlag(void)
hll_defun_inline(Key_ClearFlag, (key_clear_flag(), 1));
// int Key_IsDown(int nKeyCode)
hll_defun_inline(Key_IsDown, (handle_events(), key_is_down(a[0].i)));
// int Timer_Get(void)
hll_defun_inline(Timer_Get, vm_time());
// int CG_IsExist(int nCG)
hll_defun_inline(CG_IsExist, cg_exists(a[0].i - 1));
// int CG_GetMetrics(int nCG, ref struct cm)
hll_defun_inline(CG_GetMetrics, sact_CG_GetMetrics(a[0].i - 1, heap[a[1].i].page->values));
// int CSV_Load(string pIStringFileName)
hll_unimplemented(SACT2, CSV_Load);
// int CSV_Save(void)
hll_unimplemented(SACT2, CSV_Save);
// int CSV_SaveAs(string pIStringFileName)
hll_unimplemented(SACT2, CSV_SaveAs);
// int CSV_CountLines(void)
hll_unimplemented(SACT2, CSV_CountLines);
// int CSV_CountColumns(void)
hll_unimplemented(SACT2, CSV_CountColumns);
// void CSV_Get(ref string pIString, int nLine, int nColumn)
hll_unimplemented(SACT2, CSV_Get);
// int CSV_Set(int nLine, int nColumn, string pIStringData)
hll_unimplemented(SACT2, CSV_Set);
// int CSV_GetInt(int nLine, int nColumn)
hll_unimplemented(SACT2, CSV_GetInt);
// void CSV_SetInt(int nLine, int nColumn, int nData)
hll_unimplemented(SACT2, CSV_SetInt);
// void CSV_Realloc(int nLines, int nColumns)
hll_unimplemented(SACT2, CSV_Realloc);
// int Music_IsExist(int nNum)
hll_defun_inline(Music_IsExist, music_exists(a[0].i - 1));
// int Music_Prepare(int nCh, int nNum)
hll_warn_unimplemented(SACT2, Music_Prepare, 1);
// int Music_Unprepare(int nCh)
hll_warn_unimplemented(SACT2, Music_Unprepare, 1);
// int Music_Play(int nCh)
hll_warn_unimplemented(SACT2, Music_Play, 1);
// int Music_Stop(int nCh)
hll_warn_unimplemented(SACT2, Music_Stop, 1);
// int Music_IsPlay(int nCh)
hll_warn_unimplemented(SACT2, Music_IsPlay, 1);
// int Music_SetLoopCount(int nCh, int nCount)
hll_warn_unimplemented(SACT2, Music_SetLoopCount, 1);
// int Music_GetLoopCount(int nCh)
hll_warn_unimplemented(SACT2, Music_GetLoopCount, 1);
// int Music_SetLoopStartPos(int nCh, int dwPos)
hll_warn_unimplemented(SACT2, Music_SetLoopStartPos, 1);
// int Music_SetLoopEndPos(int nCh, int dwPos)
hll_warn_unimplemented(SACT2, Music_SetLoopEndPos, 1);
// int Music_Fade(int nCh, int nTime, int nVolume, int bStop)
hll_warn_unimplemented(SACT2, Music_Fade, 1);
// int Music_StopFade(int nCh)
hll_warn_unimplemented(SACT2, Music_StopFade, 1);
// int Music_IsFade(int nCh)
hll_warn_unimplemented(SACT2, Music_IsFade, 0);
// int Music_Pause(int nCh)
hll_warn_unimplemented(SACT2, Music_Pause, 1);
// int Music_Restart(int nCh)
hll_warn_unimplemented(SACT2, Music_Restart, 1);
// int Music_IsPause(int nCh)
hll_warn_unimplemented(SACT2, Music_IsPause, 1);
// int Music_GetPos(int nCh)
hll_warn_unimplemented(SACT2, Music_GetPos, 1);
// int Music_GetLength(int nCh)
hll_warn_unimplemented(SACT2, Music_GetLength, 1);
// int Music_GetSamplePos(int nCh)
hll_warn_unimplemented(SACT2, Music_GetSamplePos, 1);
// int Music_GetSampleLength(int nCh)
hll_warn_unimplemented(SACT2, Music_GetSampleLength, 1);
// int Music_Seek(int nCh, int dwPos)
hll_warn_unimplemented(SACT2, Music_Seek, 1);
// int Sound_IsExist(int nNum)
hll_defun_inline(Sound_IsExist, sound_exists(a[0].i - 1));
// int Sound_GetUnuseChannel(void)
hll_unimplemented(SACT2, Sound_GetUnuseChannel);
// int Sound_Prepare(int nCh, int nNum)
hll_unimplemented(SACT2, Sound_Prepare);
// int Sound_Unprepare(int nCh)
hll_warn_unimplemented(SACT2, Sound_Unprepare, 1);
// int Sound_Play(int nCh)
hll_unimplemented(SACT2, Sound_Play);
// int Sound_Stop(int nCh)
hll_unimplemented(SACT2, Sound_Stop);
// int Sound_IsPlay(int nCh)
hll_warn_unimplemented(SACT2, Sound_IsPlay, 0);
// int Sound_SetLoopCount(int nCh, int nCount)
hll_unimplemented(SACT2, Sound_SetLoopCount);
// int Sound_GetLoopCount(int nCh)
hll_unimplemented(SACT2, Sound_GetLoopCount);
// int Sound_Fade(int nCh, int nTime, int nVolume, int bStop)
hll_unimplemented(SACT2, Sound_Fade);
// int Sound_StopFade(int nCh)
hll_unimplemented(SACT2, Sound_StopFade);
// int Sound_IsFade(int nCh)
hll_unimplemented(SACT2, Sound_IsFade);
// int Sound_GetPos(int nCh)
hll_unimplemented(SACT2, Sound_GetPos);
// int Sound_GetLength(int nCh)
hll_unimplemented(SACT2, Sound_GetLength);
// int Sound_ReverseLR(int nCh)
hll_unimplemented(SACT2, Sound_ReverseLR);
// int Sound_GetVolume(int nCh)
hll_unimplemented(SACT2, Sound_GetVolume);
// int Sound_GetTimeLength(int nCh)
hll_unimplemented(SACT2, Sound_GetTimeLength);
// int Sound_GetGroupNum(int nCh)
hll_unimplemented(SACT2, Sound_GetGroupNum);
// bool Sound_PrepareFromFile(int nCh, string szFileName)
hll_unimplemented(SACT2, Sound_PrepareFromFile);

// void System_GetDate(ref int pnYear, ref int pnMonth, ref int pnDay, ref int pnDayOfWeek)
hll_defun(System_GetDate, args)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	*args[0].iref = tm->tm_year;
	*args[1].iref = tm->tm_mon;
	*args[2].iref = tm->tm_mday;
	*args[3].iref = tm->tm_wday;
	hll_return(0);
}

// void System_GetTime(ref int pnHour, ref int pnMinute, ref int pnSecond, ref int pnMilliSeconds)
hll_defun(System_GetTime, args)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	*args[0].iref = tm->tm_hour;
	*args[1].iref = tm->tm_min;
	*args[2].iref = ts.tv_sec;
	*args[3].iref = ts.tv_nsec / 1000000;
	hll_return(0);
}

// void CG_RotateRGB(int nDest, int nDx, int nDy, int nWidth, int nHeight, int nRotateType)
hll_unimplemented(SACT2, CG_RotateRGB);
// void CG_BlendAMapBin(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nBorder)
hll_defun_inline(CG_BlendAMapBin, (gfx_copy_use_amap_border(sact_get_texture(a[0].i), a[1].i, a[2].i, sact_get_texture(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i, a[8].i), 0));
// void Debug_Pause(void)
hll_unimplemented(SACT2, Debug_Pause);
// void Debug_GetFuncStack(ref string sz, int nNest)
hll_unimplemented(SACT2, Debug_GetFuncStack);
// int SP_GetAMapValue(int nSP, int nX, int nY)
hll_defun_inline(SP_GetAMapValue, sact_SP_GetAMapValue(a[0].i, a[1].i, a[2].i));
// bool SP_GetPixelValue(int nSP, int nX, int nY, ref int pnR, ref int pnG, ref int pnB)
hll_defun_inline(SP_GetPixelValue, sact_SP_GetPixelValue(a[0].i, a[1].i, a[2].i, a[3].iref, a[4].iref, a[5].iref));
// int SP_SetBrightness(int nSP, int nBrightness)
hll_unimplemented(SACT2, SP_SetBrightness);
// int SP_GetBrightness(int nSP)
hll_unimplemented(SACT2, SP_GetBrightness);

hll_deflib(SACT2) {
	hll_export(Init),
	hll_export(Error),
	hll_export(SetWP),
	hll_export(SetWP_Color),
	hll_export(WP_GetSP),
	hll_export(WP_SetSP),
	hll_export(GetScreenWidth),
	hll_export(GetScreenHeight),
	hll_export(GetMainSurfaceNumber),
	hll_export(Update),
	hll_export(Effect),
	hll_export(EffectSetMask),
	hll_export(EffectSetMaskSP),
	hll_export(QuakeScreen),
	hll_export(QUAKE_SET_CROSS),
	hll_export(QUAKE_SET_ROTATION),
	hll_export(SP_GetUnuseNum),
	hll_export(SP_Count),
	hll_export(SP_Enum),
	hll_export(SP_GetMaxZ),
	hll_export(SP_SetCG),
	hll_export(SP_SetCGFromFile),
	hll_export(SP_SaveCG),
	hll_export(SP_Create),
	hll_export(SP_CreatePixelOnly),
	hll_export(SP_CreateCustom),
	hll_export(SP_Delete),
	hll_export(SP_SetPos),
	hll_export(SP_SetX),
	hll_export(SP_SetY),
	hll_export(SP_SetZ),
	hll_export(SP_SetBlendRate),
	hll_export(SP_SetShow),
	hll_export(SP_SetDrawMethod),
	hll_export(SP_IsUsing),
	hll_export(SP_ExistAlpha),
	hll_export(SP_GetPosX),
	hll_export(SP_GetPosY),
	hll_export(SP_GetWidth),
	hll_export(SP_GetHeight),
	hll_export(SP_GetZ),
	hll_export(SP_GetBlendRate),
	hll_export(SP_GetShow),
	hll_export(SP_GetDrawMethod),
	hll_export(SP_SetTextHome),
	hll_export(SP_SetTextLineSpace),
	hll_export(SP_SetTextCharSpace),
	hll_export(SP_SetTextPos),
	hll_export(SP_TextDraw),
	hll_export(SP_TextClear),
	hll_export(SP_TextHome),
	hll_export(SP_TextNewLine),
	hll_export(SP_TextBackSpace),
	hll_export(SP_TextCopy),
	hll_export(SP_GetTextHomeX),
	hll_export(SP_GetTextHomeY),
	hll_export(SP_GetTextCharSpace),
	hll_export(SP_GetTextPosX),
	hll_export(SP_GetTextPosY),
	hll_export(SP_GetTextLineSpace),
	hll_export(SP_IsPtIn),
	hll_export(SP_IsPtInRect),
	hll_export(GAME_MSG_GetNumof),
	hll_export(GAME_MSG_Get),
	hll_export(IntToZenkaku),
	hll_export(IntToHankaku),
	hll_export(StringPopFront),
	hll_export(Mouse_GetPos),
	hll_export(Mouse_SetPos),
	hll_export(Mouse_ClearWheel),
	hll_export(Mouse_GetWheel),
	hll_export(Joypad_ClearKeyDownFlag),
	hll_export(Joypad_IsKeyDown),
	hll_export(Joypad_GetNumof),
	hll_export(JoypadQuake_Set),
	hll_export(Joypad_GetAnalogStickStatus),
	hll_export(Joypad_GetDigitalStickStatus),
	hll_export(Key_ClearFlag),
	hll_export(Key_IsDown),
	hll_export(Timer_Get),
	hll_export(CG_IsExist),
	hll_export(CG_GetMetrics),
	hll_export(CSV_Load),
	hll_export(CSV_Save),
	hll_export(CSV_SaveAs),
	hll_export(CSV_CountLines),
	hll_export(CSV_CountColumns),
	hll_export(CSV_Get),
	hll_export(CSV_Set),
	hll_export(CSV_GetInt),
	hll_export(CSV_SetInt),
	hll_export(CSV_Realloc),
	hll_export(Music_IsExist),
	hll_export(Music_Prepare),
	hll_export(Music_Unprepare),
	hll_export(Music_Play),
	hll_export(Music_Stop),
	hll_export(Music_IsPlay),
	hll_export(Music_SetLoopCount),
	hll_export(Music_GetLoopCount),
	hll_export(Music_SetLoopStartPos),
	hll_export(Music_SetLoopEndPos),
	hll_export(Music_Fade),
	hll_export(Music_StopFade),
	hll_export(Music_IsFade),
	hll_export(Music_Pause),
	hll_export(Music_Restart),
	hll_export(Music_IsPause),
	hll_export(Music_GetPos),
	hll_export(Music_GetLength),
	hll_export(Music_GetSamplePos),
	hll_export(Music_GetSampleLength),
	hll_export(Music_Seek),
	hll_export(Sound_IsExist),
	hll_export(Sound_GetUnuseChannel),
	hll_export(Sound_Prepare),
	hll_export(Sound_Unprepare),
	hll_export(Sound_Play),
	hll_export(Sound_Stop),
	hll_export(Sound_IsPlay),
	hll_export(Sound_SetLoopCount),
	hll_export(Sound_GetLoopCount),
	hll_export(Sound_Fade),
	hll_export(Sound_StopFade),
	hll_export(Sound_IsFade),
	hll_export(Sound_GetPos),
	hll_export(Sound_GetLength),
	hll_export(Sound_ReverseLR),
	hll_export(Sound_GetVolume),
	hll_export(Sound_GetTimeLength),
	hll_export(Sound_GetGroupNum),
	hll_export(Sound_PrepareFromFile),
	hll_export(System_GetDate),
	hll_export(System_GetTime),
	hll_export(CG_RotateRGB),
	hll_export(CG_BlendAMapBin),
	hll_export(Debug_Pause),
	hll_export(Debug_GetFuncStack),
	hll_export(SP_GetAMapValue),
	hll_export(SP_GetPixelValue),
	hll_export(SP_SetBrightness),
	hll_export(SP_GetBrightness),
	NULL
};
