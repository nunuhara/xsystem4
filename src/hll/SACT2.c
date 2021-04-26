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

#include "system4.h"
#include "system4/ain.h"
#include "system4/ald.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "hll.h"
#include "audio.h"
#include "input.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "xsystem4.h"

static int sact_GAME_MSG_GetNumOf(void)
{
	return ain->nr_messages;
}

static void sact_IntToZenkaku(struct string **s, int value, int figures, int zero_pad)
{
	int i;
	char buf[512];

	string_clear(*s);
	i = int_to_cstr(buf, 512, value, figures, zero_pad, true);
	string_append_cstr(s, buf, i);
}

static void sact_IntToHankaku(struct string **s, int value, int figures, int zero_pad)
{
	int i;
	char buf[512];

	string_clear(*s);
	i = int_to_cstr(buf, 512, value, figures, zero_pad, false);
	string_append_cstr(s, buf, i);
}

static int sact_Mouse_GetPos(int *x, int *y)
{
	handle_events();
	mouse_get_pos(x, y);
	return mouse_focus && keyboard_focus;
}

static int sact_Mouse_SetPos(int x, int y)
{
	handle_events();
	mouse_set_pos(x, y);
	return mouse_focus && keyboard_focus;
}

static void sact_Mouse_ClearWheel(void)
{
	mouse_clear_wheel();
}

static void sact_Mouse_GetWheel(int *forward, int *back)
{
	mouse_get_wheel(forward, back);
}

static void sact_Joypad_ClearKeyDownFlag(int n)
{
	// TODO
}

static int sact_Joypad_IsKeyDown(int num, int key)
{
	return 0; // TODO
}

static bool sact_Joypad_GetAnalogStickStatus(int num, int type, float *degree, float *power)
{
	//hll_unimplemented_warning(SACT2, Joypad_GetAnalogStickStatus);
	*degree = 0.0;
	*power  = 0.0;
	return true;
}

static bool sact_Joypad_GetDigitalStickStatus(int num, int type, bool *left, bool *right, bool *up, bool *down)
{
	//hll_unimplemented_warning(SACT2, Joypad_GetDigitalStickStatus);
	*left  = false;
	*right = false;
	*up    = false;
	*down  = false;
	return 1;
}

static int sact_Key_ClearFlag(void)
{
	key_clear_flag();
	return 1;
}

static int sact_Key_IsDown(int keycode)
{
	handle_events();
	return key_is_down(keycode);
}

static int sact_CG_IsExist(int cg_no)
{
	return ald[ALDFILE_CG] && archive_exists(ald[ALDFILE_CG], cg_no - 1);
}

static int sact_Music_IsExist(int n)
{
	return bgm_exists(n - 1);
}

static int sact_Music_Prepare(int ch, int n)
{
	return bgm_prepare(ch, n - 1);
}

static int sact_Music_Unprepare(int ch)
{
	return bgm_unprepare(ch);
}

static int sact_Music_Play(int ch)
{
	return bgm_play(ch);
}

static int sact_Music_Stop(int ch)
{
	return bgm_stop(ch);
}

static int sact_Music_IsPlay(int ch)
{
	return bgm_is_playing(ch);
}

static int sact_Music_Fade(int ch, int time, int volume, int stop)
{
	return bgm_fade(ch, time, volume, stop);
}

static int sact_Sound_IsExist(int n)
{
	return wav_exists(n - 1);
}

static int sact_Sound_GetUnuseChannel(void)
{
	return wav_get_unused_channel();
}

static int sact_Sound_Prepare(int ch, int n)
{
	return wav_prepare(ch, n - 1);
}

static int sact_Sound_Unprepare(int ch)
{
	return wav_unprepare(ch);
}

static int sact_Sound_Play(int ch)
{
	return wav_play(ch);
}

static int sact_Sound_Stop(int ch)
{
	return wav_stop(ch);
}

static int sact_Sound_IsPlay(int ch)
{
	return wav_is_playing(ch);
}

static int sact_Sound_Fade(int ch, int time, int volume, int stop)
{
	return wav_fade(ch, time, volume, stop);
}

static int sact_Sound_ReverseLR(int ch)
{
	return wav_reverse_LR(ch);
}

static int sact_Sound_GetTimeLength(int ch)
{
	return wav_get_time_length(ch);
}

static void sact_System_GetDate(int *year, int *month, int *mday, int *wday)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	*year  = tm->tm_year;
	*month = tm->tm_mon;
	*mday  = tm->tm_mday;
	*wday  = tm->tm_wday;
}

static void sact_System_GetTime(int *hour, int *min, int *sec, int *ms)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	*hour = tm->tm_hour;
	*min  = tm->tm_min;
	*sec  = ts.tv_sec;
	*ms   = ts.tv_nsec / 1000000;
}

static void sact_CG_BlendAMapBin(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int border)
{
	gfx_copy_use_amap_border(sact_get_texture_dirty(dst), dx, dy, sact_get_texture(src), sx, sy, w, h, border);
}

//int SACT2_Error(struct string *err);
//int SACT2_WP_GetSP(int sp);
//int SACT2_WP_SetSP(int sp);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, EffectSetMask, int cg);
//int SACT2_EffectSetMaskSP(int sp);
HLL_WARN_UNIMPLEMENTED( , void, SACT2, QuakeScreen, int amp_x, int amp_y, int time, int key);
//void SACT2_QUAKE_SET_CROSS(int amp_x, int amp_y);
//void SACT2_QUAKE_SET_ROTATION(int amp, int cycle);
//int SACT2_SP_SetCGFromFile(int sp, struct string *filename);
//int SACT2_SP_SaveCG(int sp, struct string *filename);
//int SACT2_SP_CreateCustom(int sp);
//int SACT2_SP_TextBackSpace(int sp_no);
//void SACT2_GAME_MSG_Get(int index, struct string **text);
//int SACT2_StringPopFront(struct string **dst, struct string **src);
//int SACT2_Joypad_GetNumof(void);
HLL_WARN_UNIMPLEMENTED( , void, SACT2, JoypadQuake_Set, int num, int type, int magnitude);
//int  SACT2_CSV_Load(struct string *filename);
//int  SACT2_CSV_Save(void);
//int  SACT2_CSV_SaveAs(struct string *filename);
//int  SACT2_CSV_CountLines(void);
//int  SACT2_CSV_CountColumns(void);
//void SACT2_CSV_Get(struct string **s, int line, int column);
//int  SACT2_CSV_Set(int line, int column, struct string *data);
//int  SACT2_CSV_GetInt(int line, int column);
//void SACT2_CSV_SetInt(int line, int column, int data);
//void SACT2_CSV_Realloc(int lines, int columns);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_SetLoopCount, int ch, int count);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_GetLoopCount, int ch);
//int SACT2_Music_SetLoopStartPos(int ch, int pos);
//int SACT2_Music_SetLoopEndPos(int ch, int pos);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_StopFade, int ch);
HLL_WARN_UNIMPLEMENTED(0, int, SACT2, Music_IsFade, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_Pause, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_Restart, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_IsPause, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_GetPos, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_GetLength, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_GetSamplePos, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_GetSampleLength, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_Seek, int ch, int pos);
//int SACT2_Sound_SetLoopCount(int ch, int count);
//int SACT2_Sound_GetLoopCount(int ch);
//int SACT2_Sound_StopFade(int ch);
//int SACT2_Sound_IsFade(int ch);
//int SACT2_Sound_GetPos(int ch);
//int SACT2_Sound_GetLength(int ch);
//int SACT2_Sound_GetVolume(int ch);
//int SACT2_Sound_GetGroupNum(int ch);
//bool SACT2_Sound_PrepareFromFile(int ch, struct string *filename);
//void SACT2_CG_RotateRGB(int dst, int dx, int dy, int w, int h, int rotate_type);
//void SACT2_Debug_Pause(void);
//void SACT2_Debug_GetFuncStack(struct string **s, int nest);
HLL_WARN_UNIMPLEMENTED(0, int, SACT2, SP_SetBrightness, int sp_no, int brightness);
HLL_WARN_UNIMPLEMENTED(0, int, SACT2, SP_GetBrightness, int sp_no);

#define SACT_EXPORTS \
	    HLL_EXPORT(_ModuleFini, sact_ModuleFini), \
	    HLL_EXPORT(Init, sact_Init), \
	    HLL_TODO_EXPORT(Error, SACT2_Error), \
	    HLL_EXPORT(SetWP, sact_SetWP), \
	    HLL_EXPORT(SetWP_Color, sact_SetWP_Color), \
	    HLL_TODO_EXPORT(WP_GetSP, SACT2_WP_GetSP), \
	    HLL_TODO_EXPORT(WP_SetSP, SACT2_WP_SetSP), \
	    HLL_EXPORT(GetScreenWidth, sact_GetScreenWidth), \
	    HLL_EXPORT(GetScreenHeight, sact_GetScreenHeight), \
	    HLL_EXPORT(GetMainSurfaceNumber, sact_GetMainSurfaceNumber), \
	    HLL_EXPORT(Update, sact_Update), \
	    HLL_EXPORT(Effect, sact_Effect), \
	    HLL_EXPORT(EffectSetMask, SACT2_EffectSetMask), \
	    HLL_TODO_EXPORT(EffectSetMaskSP, SACT2_EffectSetMaskSP), \
	    HLL_EXPORT(QuakeScreen, SACT2_QuakeScreen), \
	    HLL_TODO_EXPORT(QUAKE_SET_CROSS, SACT2_QUAKE_SET_CROSS), \
	    HLL_TODO_EXPORT(QUAKE_SET_ROTATION, SACT2_QUAKE_SET_ROTATION), \
	    HLL_EXPORT(SP_GetUnuseNum, sact_SP_GetUnuseNum), \
	    HLL_EXPORT(SP_Count, sact_SP_Count), \
	    HLL_EXPORT(SP_Enum, sact_SP_Enum), \
	    HLL_EXPORT(SP_GetMaxZ, sact_SP_GetMaxZ), \
	    HLL_EXPORT(SP_SetCG, sact_SP_SetCG), \
	    HLL_TODO_EXPORT(SP_SetCGFromFile, SACT2_SP_SetCGFromFile), \
	    HLL_TODO_EXPORT(SP_SaveCG, SACT2_SP_SaveCG), \
	    HLL_EXPORT(SP_Create, sact_SP_Create), \
	    HLL_EXPORT(SP_CreatePixelOnly, sact_SP_CreatePixelOnly), \
	    HLL_TODO_EXPORT(SP_CreateCustom, SACT2_SP_CreateCustom), \
	    HLL_EXPORT(SP_Delete, sact_SP_Delete), \
	    HLL_EXPORT(SP_SetPos, sact_SP_SetPos), \
	    HLL_EXPORT(SP_SetX, sact_SP_SetX), \
	    HLL_EXPORT(SP_SetY, sact_SP_SetY), \
	    HLL_EXPORT(SP_SetZ, sact_SP_SetZ), \
	    HLL_EXPORT(SP_SetBlendRate, sact_SP_SetBlendRate), \
	    HLL_EXPORT(SP_SetShow, sact_SP_SetShow), \
	    HLL_EXPORT(SP_SetDrawMethod, sact_SP_SetDrawMethod), \
	    HLL_EXPORT(SP_IsUsing, sact_SP_IsUsing), \
	    HLL_EXPORT(SP_ExistAlpha, sact_SP_ExistsAlpha), \
	    HLL_EXPORT(SP_GetPosX, sact_SP_GetPosX), \
	    HLL_EXPORT(SP_GetPosY, sact_SP_GetPosY), \
	    HLL_EXPORT(SP_GetWidth, sact_SP_GetWidth), \
	    HLL_EXPORT(SP_GetHeight, sact_SP_GetHeight), \
	    HLL_EXPORT(SP_GetZ, sact_SP_GetZ), \
	    HLL_EXPORT(SP_GetBlendRate, sact_SP_GetBlendRate), \
	    HLL_EXPORT(SP_GetShow, sact_SP_GetShow), \
	    HLL_EXPORT(SP_GetDrawMethod, sact_SP_GetDrawMethod), \
	    HLL_EXPORT(SP_SetTextHome, sact_SP_SetTextHome), \
	    HLL_EXPORT(SP_SetTextLineSpace, sact_SP_SetTextLineSpace), \
	    HLL_EXPORT(SP_SetTextCharSpace, sact_SP_SetTextCharSpace), \
	    HLL_EXPORT(SP_SetTextPos, sact_SP_SetTextPos), \
	    HLL_EXPORT(SP_TextDraw, sact_SP_TextDraw), \
	    HLL_EXPORT(SP_TextClear, sact_SP_TextClear), \
	    HLL_EXPORT(SP_TextHome, sact_SP_TextHome), \
	    HLL_EXPORT(SP_TextNewLine, sact_SP_TextNewLine), \
	    HLL_TODO_EXPORT(SP_TextBackSpace, SACT2_SP_TextBackSpace), \
	    HLL_EXPORT(SP_TextCopy, sact_SP_TextCopy), \
	    HLL_EXPORT(SP_GetTextHomeX, sact_SP_GetTextHomeX), \
	    HLL_EXPORT(SP_GetTextHomeY, sact_SP_GetTextHomeY), \
	    HLL_EXPORT(SP_GetTextCharSpace, sact_SP_GetTextCharSpace), \
	    HLL_EXPORT(SP_GetTextPosX, sact_SP_GetTextPosX), \
	    HLL_EXPORT(SP_GetTextPosY, sact_SP_GetTextPosY), \
	    HLL_EXPORT(SP_GetTextLineSpace, sact_SP_GetTextLineSpace), \
	    HLL_EXPORT(SP_IsPtIn, sact_SP_IsPtIn), \
	    HLL_EXPORT(SP_IsPtInRect, sact_SP_IsPtInRect), \
	    HLL_EXPORT(GAME_MSG_GetNumof, sact_GAME_MSG_GetNumOf), \
	    HLL_TODO_EXPORT(GAME_MSG_Get, SACT2_GAME_MSG_Get), \
	    HLL_EXPORT(IntToZenkaku, sact_IntToZenkaku), \
	    HLL_EXPORT(IntToHankaku, sact_IntToHankaku), \
	    HLL_TODO_EXPORT(StringPopFront, SACT2_StringPopFront), \
	    HLL_EXPORT(Mouse_GetPos, sact_Mouse_GetPos), \
	    HLL_EXPORT(Mouse_SetPos, sact_Mouse_SetPos), \
	    HLL_EXPORT(Mouse_ClearWheel, sact_Mouse_ClearWheel), \
	    HLL_EXPORT(Mouse_GetWheel, sact_Mouse_GetWheel), \
	    HLL_EXPORT(Joypad_ClearKeyDownFlag, sact_Joypad_ClearKeyDownFlag), \
	    HLL_EXPORT(Joypad_IsKeyDown, sact_Joypad_IsKeyDown), \
	    HLL_TODO_EXPORT(Joypad_GetNumof, SACT2_Joypad_GetNumof), \
	    HLL_EXPORT(JoypadQuake_Set, SACT2_JoypadQuake_Set), \
	    HLL_EXPORT(Joypad_GetAnalogStickStatus, sact_Joypad_GetAnalogStickStatus), \
	    HLL_EXPORT(Joypad_GetDigitalStickStatus, sact_Joypad_GetDigitalStickStatus), \
	    HLL_EXPORT(Key_ClearFlag, sact_Key_ClearFlag), \
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown), \
	    HLL_EXPORT(Timer_Get, vm_time), \
	    HLL_EXPORT(CG_IsExist, sact_CG_IsExist), \
	    HLL_EXPORT(CG_GetMetrics, sact_CG_GetMetrics), \
	    HLL_TODO_EXPORT(CSV_Load, SACT2_CSV_Load), \
	    HLL_TODO_EXPORT(CSV_Save, SACT2_CSV_Save), \
	    HLL_TODO_EXPORT(CSV_SaveAs, SACT2_CSV_SaveAs), \
	    HLL_TODO_EXPORT(CSV_CountLines, SACT2_CSV_CountLines), \
	    HLL_TODO_EXPORT(CSV_CountColumns, SACT2_CSV_CountColumns), \
	    HLL_TODO_EXPORT(CSV_Get, SACT2_CSV_Get), \
	    HLL_TODO_EXPORT(CSV_Set, SACT2_CSV_Set), \
	    HLL_TODO_EXPORT(CSV_GetInt, SACT2_CSV_GetInt), \
	    HLL_TODO_EXPORT(CSV_SetInt, SACT2_CSV_SetInt), \
	    HLL_TODO_EXPORT(CSV_Realloc, SACT2_CSV_Realloc), \
	    HLL_EXPORT(Music_IsExist, sact_Music_IsExist), \
	    HLL_EXPORT(Music_Prepare, sact_Music_Prepare), \
	    HLL_EXPORT(Music_Unprepare, sact_Music_Unprepare), \
	    HLL_EXPORT(Music_Play, sact_Music_Play), \
	    HLL_EXPORT(Music_Stop, sact_Music_Stop), \
	    HLL_EXPORT(Music_IsPlay, sact_Music_IsPlay), \
	    HLL_EXPORT(Music_SetLoopCount, SACT2_Music_SetLoopCount), \
	    HLL_EXPORT(Music_GetLoopCount, SACT2_Music_GetLoopCount), \
	    HLL_TODO_EXPORT(Music_SetLoopStartPos, SACT2_Music_SetLoopStartPos), \
	    HLL_TODO_EXPORT(Music_SetLoopEndPos, SACT2_Music_SetLoopEndPos), \
	    HLL_EXPORT(Music_Fade, sact_Music_Fade), \
	    HLL_EXPORT(Music_StopFade, SACT2_Music_StopFade), \
	    HLL_EXPORT(Music_IsFade, SACT2_Music_IsFade), \
	    HLL_EXPORT(Music_Pause, SACT2_Music_Pause), \
	    HLL_EXPORT(Music_Restart, SACT2_Music_Restart), \
	    HLL_EXPORT(Music_IsPause, SACT2_Music_IsPause), \
	    HLL_EXPORT(Music_GetPos, SACT2_Music_GetPos), \
	    HLL_EXPORT(Music_GetLength, SACT2_Music_GetLength), \
	    HLL_EXPORT(Music_GetSamplePos, SACT2_Music_GetSamplePos), \
	    HLL_EXPORT(Music_GetSampleLength, SACT2_Music_GetSampleLength), \
	    HLL_EXPORT(Music_Seek, SACT2_Music_Seek), \
	    HLL_EXPORT(Sound_IsExist, sact_Sound_IsExist), \
	    HLL_EXPORT(Sound_GetUnuseChannel, sact_Sound_GetUnuseChannel), \
	    HLL_EXPORT(Sound_Prepare, sact_Sound_Prepare), \
	    HLL_EXPORT(Sound_Unprepare, sact_Sound_Unprepare), \
	    HLL_EXPORT(Sound_Play, sact_Sound_Play), \
	    HLL_EXPORT(Sound_Stop, sact_Sound_Stop), \
	    HLL_EXPORT(Sound_IsPlay, sact_Sound_IsPlay), \
	    HLL_TODO_EXPORT(Sound_SetLoopCount, SACT2_Sound_SetLoopCount), \
	    HLL_TODO_EXPORT(Sound_GetLoopCount, SACT2_Sound_GetLoopCount), \
	    HLL_EXPORT(Sound_Fade, sact_Sound_Fade), \
	    HLL_TODO_EXPORT(Sound_StopFade, SACT2_Sound_StopFade), \
	    HLL_TODO_EXPORT(Sound_IsFade, SACT2_Sound_IsFade), \
	    HLL_TODO_EXPORT(Sound_GetPos, SACT2_Sound_GetPos), \
	    HLL_TODO_EXPORT(Sound_GetLength, SACT2_Sound_GetLength), \
	    HLL_EXPORT(Sound_ReverseLR, sact_Sound_ReverseLR), \
	    HLL_TODO_EXPORT(Sound_GetVolume, SACT2_Sound_GetVolume), \
	    HLL_EXPORT(Sound_GetTimeLength, sact_Sound_GetTimeLength), \
	    HLL_TODO_EXPORT(Sound_GetGroupNum, SACT2_Sound_GetGroupNum), \
	    HLL_TODO_EXPORT(Sound_PrepareFromFile, SACT2_Sound_PrepareFromFile), \
	    HLL_EXPORT(System_GetDate, sact_System_GetDate), \
	    HLL_EXPORT(System_GetTime, sact_System_GetTime), \
	    HLL_TODO_EXPORT(CG_RotateRGB, SACT2_CG_RotateRGB), \
	    HLL_EXPORT(CG_BlendAMapBin, sact_CG_BlendAMapBin), \
	    HLL_TODO_EXPORT(Debug_Pause, SACT2_Debug_Pause), \
	    HLL_TODO_EXPORT(Debug_GetFuncStack, SACT2_Debug_GetFuncStack), \
	    HLL_EXPORT(SP_GetAMapValue, sact_SP_GetAMapValue), \
	    HLL_EXPORT(SP_GetPixelValue, sact_SP_GetPixelValue), \
	    HLL_EXPORT(SP_SetBrightness, SACT2_SP_SetBrightness), \
	    HLL_EXPORT(SP_GetBrightness, SACT2_SP_GetBrightness)

static void SACTDX_SetVolumeMixerMasterGroupNum(int n)
{
	// TODO
}

static void SACTDX_SetVolumeMixerSEGroupNum(int n)
{
	// TODO
}

static void SACTDX_SetVolumeMixerBGMGroupNum(int n)
{
	// TODO
}

static int SACTDX_Sound_GetGroupNumFromDataNum(int n)
{
	return 0;
}

//static int SACTDX_SP_CreateCopy(int nSP, int nSrcSp);
//static bool SACTDX_Joypad_GetAnalogStickStatus(int nNum, int nType, ref float pfDegree, ref float pfPower);
//static bool SACTDX_Joypad_GetDigitalStickStatus(int nNum, int nType, ref bool pbLeft, ref bool pbRight, ref bool pbUp, ref bool pbDown);
//static void SACTDX_FFT_rdft(ref array@float a);
//static void SACTDX_FFT_hanning_window(ref array@float a);
//static int SACTDX_Music_AnalyzeSampleData(ref array@float l, ref array@float r, ref array@int src, int chns, int bps);
//static void SACTDX_Key_ClearFlagNoCtrl(void);
//static void SACTDX_Key_ClearFlagOne(int nKeyCode);

static int SACTDX_TRANS_Begin(int nNum)
{
	// TODO
	return 1;
}

static int SACTDX_TRANS_Update(float fRate)
{
	// TODO
	return 1;
}

static int SACTDX_TRANS_End(void)
{
	// TODO
	return 1;
}

//static bool SACTDX_VIEW_SetMode(int nMode);
//static int SACTDX_VIEW_GetMode(void);
//static bool SACTDX_DX_GetUsePower2Texture(void);
//static void SACTDX_DX_SetUsePower2Texture(bool bUse);

#define SACTDX_EXPORTS \
	HLL_EXPORT(SetVolumeMixerMasterGroupNum, SACTDX_SetVolumeMixerMasterGroupNum), \
	HLL_EXPORT(SetVolumeMixerSEGroupNum, SACTDX_SetVolumeMixerSEGroupNum), \
	HLL_EXPORT(SetVolumeMixerBGMGroupNum, SACTDX_SetVolumeMixerBGMGroupNum), \
	HLL_EXPORT(Sound_GetGroupNumFromDataNum, SACTDX_Sound_GetGroupNumFromDataNum), \
	HLL_TODO_EXPORT(SP_CreateCopy, SACTDX_SP_CreateCopy),	\
	HLL_TODO_EXPORT(Joypad_GetAnalogStickStatus, SACTDX_Joypad_GetAnalogStickStatus), \
	HLL_TODO_EXPORT(GetDigitalStickStatus, SACTDX_GetDigitalStickStatus), \
	HLL_TODO_EXPORT(FFT_rdft, SACTDX_FFT_rdft),		\
	HLL_TODO_EXPORT(FFT_hanning_window, SACTDX_FFT_hanning_window),	\
	HLL_TODO_EXPORT(Music_AnalyzeSampleData, SACTDX_Music_AnalyzeSampleData), \
	HLL_TODO_EXPORT(Key_ClearFlagNoCtrl, SACTDX_Key_ClearFlagNoCtrl), \
	HLL_TODO_EXPORT(Key_ClearFlagOne, SACTDX_Key_ClearFlagOne), \
	HLL_EXPORT(TRANS_Begin, SACTDX_TRANS_Begin),	    \
	HLL_EXPORT(TRANS_Update, SACTDX_TRANS_Update),	    \
	HLL_EXPORT(TRANS_End, SACTDX_TRANS_End),	\
	HLL_TODO_EXPORT(VIEW_SetMode, SACTDX_VIEW_SetMode),	\
	HLL_TODO_EXPORT(VIEW_GetMode, SACTDX_VIEW_GetMode),	\
	HLL_TODO_EXPORT(DX_GetUsePower2Texture, SACTDX_DX_GetUsePower2Texture),	\
	HLL_TODO_EXPORT(DX_SetUsePower2Texture, SACTDX_DX_SetUsePower2Texture)

HLL_LIBRARY(SACT2, SACT_EXPORTS);
HLL_LIBRARY(SACTDX, SACT_EXPORTS, SACTDX_EXPORTS);

//static bool StoatSpriteEngine_SP_SetTextSprite(int nSprite, string Text);
//static void StoatSpriteEngine_SP_SetTextSpriteType(int nType);
//static void StoatSpriteEngine_SP_SetTextSpriteSize(int nSize);
//static void StoatSpriteEngine_SP_SetTextSpriteColor(int nR, int nG, int nB);
//static void StoatSpriteEngine_SP_SetTextSpriteBoldWeight(float fBoldWeight);
//static void StoatSpriteEngine_SP_SetTextSpriteEdgeWeight(float fEdgeWeight);
//static void StoatSpriteEngine_SP_SetTextSpriteEdgeColor(int nR, int nG, int nB);
//static bool StoatSpriteEngine_SP_SetDashTextSprite(int nSprite, int nWidth, int nHeight);
//static void StoatSpriteEngine_FPS_SetShow(bool bShow);
//static bool StoatSpriteEngine_KEY_GetState(int nKey);

//static bool StoatSpriteEngine_MultiSprite_SetCG(int nType, int nNum, int nCG);
//static bool StoatSpriteEngine_MultiSprite_SetPos(int nType, int nNum, int nX, int nY);
//static bool StoatSpriteEngine_MultiSprite_SetDefaultPos(int nType, int nNum, int nDefaultX, int nDefaultY);
//static bool StoatSpriteEngine_MultiSprite_SetZ(int nType, int nNum, int nZ);
//static bool StoatSpriteEngine_MultiSprite_SetAlpha(int nType, int nNum, int nAlpha);
//static bool StoatSpriteEngine_MultiSprite_SetTransitionInfo(int nType, int nNum, int nTransitionNumber, int nTransitionTime);
//static bool StoatSpriteEngine_MultiSprite_SetLinkedMessageFrame(int nType, int nNum, bool bLink);
//static bool StoatSpriteEngine_MultiSprite_SetParentMessageFrameNum(int nType, int nNum, int nMessageAreaNum);
//static bool StoatSpriteEngine_MultiSprite_SetOriginPosMode(int nType, int nNum, int nMode);
//static bool StoatSpriteEngine_MultiSprite_SetText(int nType, int nNum, string Text);
//static bool StoatSpriteEngine_MultiSprite_SetCharSpace(int nType, int nNum, int nCharSpace);
//static bool StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetSize(int nType, int nNum, int nSize);
//static bool StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetColor(int nType, int nNum, int nR, int nG, int nB);
//static bool StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetBoldWeight(int nType, int nNum, float fBoldWeight);
//static bool StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetEdgeWeight(int nType, int nNum, float fEdgeWeight);
//static bool StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetEdgeColor(int nType, int nNum, int nR, int nG, int nB);
//static bool StoatSpriteEngine_MultiSprite_GetTransitionInfo(int nType, int nNum, ref int nTransitionNumber, ref int nTransitionTime);
//static int StoatSpriteEngine_MultiSprite_GetCG(int nType, int nNum);
//static bool StoatSpriteEngine_MultiSprite_GetPos(int nType, int nNum, ref int nX, ref int nY);
//static bool StoatSpriteEngine_MultiSprite_GetAlpha(int nType, int nNum, ref int nAlpha);
//static bool StoatSpriteEngine_MultiSprite_ResetDefaultPos(int nType, int nNum);
//static bool StoatSpriteEngine_MultiSprite_ResetAllDefaultPos(int nType);
//static void StoatSpriteEngine_MultiSprite_ResetAllCG(void);
//static void StoatSpriteEngine_MultiSprite_SetAllShowMessageFrame(bool bShow);
//static bool StoatSpriteEngine_MultiSprite_BeginMove(int nType, int nNum, int nX0, int nY0, int nX1, int nY1, int nTime, int nBeginWaitTime, float fMoveAcceleration);
//static bool StoatSpriteEngine_MultiSprite_BeginMoveWithAlpha(int nType, int nNum, int nX0, int nY0, int nAlpha0, int nX1, int nY1, int nAlpha1, int nTime, int nBeginWaitTime, float fMoveAcceleration);
//static int StoatSpriteEngine_MultiSprite_GetMaxMoveTotalTime(void);
//static void StoatSpriteEngine_MultiSprite_SetAllMoveCurrentTime(int nTime);
//static void StoatSpriteEngine_MultiSprite_EndAllMove(void);
//static bool StoatSpriteEngine_MultiSprite_UpdateView(void);
//static void StoatSpriteEngine_MultiSprite_Rebuild(void);
//static bool StoatSpriteEngine_MultiSprite_Encode(ref array@int pIEncodeData);
//static bool StoatSpriteEngine_MultiSprite_Decode(ref array@int pIEncodeData);

static int StoatSpriteEngine_SYSTEM_IsResetOnce(void)
{
	return 0;
}

HLL_LIBRARY(StoatSpriteEngine,
	    HLL_EXPORT(SetVolumeMixerMasterGroupNum, SACTDX_SetVolumeMixerMasterGroupNum), \
	    HLL_EXPORT(SetVolumeMixerSEGroupNum, SACTDX_SetVolumeMixerSEGroupNum), \
	    HLL_EXPORT(SetVolumeMixerBGMGroupNum, SACTDX_SetVolumeMixerBGMGroupNum), \
	    HLL_EXPORT(Init, sact_Init), \
	    HLL_TODO_EXPORT(Error, SACT2_Error), \
	    HLL_EXPORT(SetWP_Color, sact_SetWP_Color), \
	    HLL_EXPORT(GetScreenWidth, sact_GetScreenWidth), \
	    HLL_EXPORT(GetScreenHeight, sact_GetScreenHeight), \
	    HLL_EXPORT(GetMainSurfaceNumber, sact_GetMainSurfaceNumber), \
	    HLL_EXPORT(Update, sact_Update), \
	    HLL_EXPORT(SP_GetUnuseNum, sact_SP_GetUnuseNum), \
	    HLL_EXPORT(SP_Count, sact_SP_Count), \
	    HLL_EXPORT(SP_Enum, sact_SP_Enum), \
	    HLL_EXPORT(SP_GetMaxZ, sact_SP_GetMaxZ), \
	    HLL_EXPORT(SP_SetCG, sact_SP_SetCG), \
	    HLL_TODO_EXPORT(SP_SetCGFromFile, SACT2_SP_SetCGFromFile), \
	    HLL_TODO_EXPORT(SP_SaveCG, SACT2_SP_SaveCG), \
	    HLL_EXPORT(SP_Create, sact_SP_Create), \
	    HLL_EXPORT(SP_CreatePixelOnly, sact_SP_CreatePixelOnly), \
	    HLL_TODO_EXPORT(SP_CreateCustom, SACT2_SP_CreateCustom), \
	    HLL_EXPORT(SP_Delete, sact_SP_Delete), \
	    HLL_EXPORT(SP_SetPos, sact_SP_SetPos), \
	    HLL_EXPORT(SP_SetX, sact_SP_SetX), \
	    HLL_EXPORT(SP_SetY, sact_SP_SetY), \
	    HLL_EXPORT(SP_SetZ, sact_SP_SetZ), \
	    HLL_EXPORT(SP_SetBlendRate, sact_SP_SetBlendRate), \
	    HLL_EXPORT(SP_SetShow, sact_SP_SetShow), \
	    HLL_EXPORT(SP_SetDrawMethod, sact_SP_SetDrawMethod), \
	    HLL_EXPORT(SP_IsUsing, sact_SP_IsUsing), \
	    HLL_EXPORT(SP_ExistAlpha, sact_SP_ExistsAlpha), \
	    HLL_EXPORT(SP_GetPosX, sact_SP_GetPosX), \
	    HLL_EXPORT(SP_GetPosY, sact_SP_GetPosY), \
	    HLL_EXPORT(SP_GetWidth, sact_SP_GetWidth), \
	    HLL_EXPORT(SP_GetHeight, sact_SP_GetHeight), \
	    HLL_EXPORT(SP_GetZ, sact_SP_GetZ), \
	    HLL_EXPORT(SP_GetBlendRate, sact_SP_GetBlendRate), \
	    HLL_EXPORT(SP_GetShow, sact_SP_GetShow), \
	    HLL_EXPORT(SP_GetDrawMethod, sact_SP_GetDrawMethod), \
	    HLL_EXPORT(SP_SetTextHome, sact_SP_SetTextHome), \
	    HLL_EXPORT(SP_SetTextLineSpace, sact_SP_SetTextLineSpace), \
	    HLL_EXPORT(SP_SetTextCharSpace, sact_SP_SetTextCharSpace), \
	    HLL_EXPORT(SP_SetTextPos, sact_SP_SetTextPos), \
	    HLL_EXPORT(SP_TextDraw, sact_SP_TextDraw), \
	    HLL_EXPORT(SP_TextClear, sact_SP_TextClear), \
	    HLL_EXPORT(SP_TextHome, sact_SP_TextHome), \
	    HLL_EXPORT(SP_TextNewLine, sact_SP_TextNewLine), \
	    HLL_TODO_EXPORT(SP_TextBackSpace, SACT2_SP_TextBackSpace), \
	    HLL_EXPORT(SP_TextCopy, sact_SP_TextCopy), \
	    HLL_EXPORT(SP_GetTextHomeX, sact_SP_GetTextHomeX), \
	    HLL_EXPORT(SP_GetTextHomeY, sact_SP_GetTextHomeY), \
	    HLL_EXPORT(SP_GetTextCharSpace, sact_SP_GetTextCharSpace), \
	    HLL_EXPORT(SP_GetTextPosX, sact_SP_GetTextPosX), \
	    HLL_EXPORT(SP_GetTextPosY, sact_SP_GetTextPosY), \
	    HLL_EXPORT(SP_GetTextLineSpace, sact_SP_GetTextLineSpace), \
	    HLL_EXPORT(SP_IsPtIn, sact_SP_IsPtIn), \
	    HLL_EXPORT(SP_IsPtInRect, sact_SP_IsPtInRect), \
	    HLL_TODO_EXPORT(SP_SetTextSprite, StoatSpriteEngine_SP_SetTextSprite), \
	    HLL_TODO_EXPORT(SP_SetTextSpriteType, StoatSpriteEngine_SP_SetTextSpriteType), \
	    HLL_TODO_EXPORT(SP_SetTextSpriteSize, StoatSpriteEngine_SP_SetTextSpriteSize), \
	    HLL_TODO_EXPORT(SP_SetTextSpriteColor, StoatSpriteEngine_SP_SetTextSpriteColor), \
	    HLL_TODO_EXPORT(SP_SetTextSpriteBoldWeight, StoatSpriteEngine_SP_SetTextSpriteBoldWeight), \
	    HLL_TODO_EXPORT(SP_SetTextSpriteEdgeWeight, StoatSpriteEngine_SP_SetTextSpriteEdgeWeight), \
	    HLL_TODO_EXPORT(SP_SetTextSpriteEdgeColor, StoatSpriteEngine_SP_SetTextSpriteEdgeColor), \
	    HLL_TODO_EXPORT(SP_SetDashTextSprite, StoatSpriteEngine_SP_SetDashTextSprite), \
	    HLL_TODO_EXPORT(FPS_SetShow, StoatSpriteEngine_FPS_SetShow), \
	    HLL_EXPORT(GAME_MSG_GetNumof, sact_GAME_MSG_GetNumOf), \
	    HLL_TODO_EXPORT(GAME_MSG_Get, SACT2_GAME_MSG_Get), \
	    HLL_EXPORT(IntToZenkaku, sact_IntToZenkaku), \
	    HLL_EXPORT(IntToHankaku, sact_IntToHankaku), \
	    HLL_TODO_EXPORT(StringPopFront, SACT2_StringPopFront), \
	    HLL_EXPORT(Mouse_GetPos, sact_Mouse_GetPos), \
	    HLL_EXPORT(Mouse_SetPos, sact_Mouse_SetPos), \
	    HLL_EXPORT(Mouse_ClearWheel, sact_Mouse_ClearWheel), \
	    HLL_EXPORT(Mouse_GetWheel, sact_Mouse_GetWheel), \
	    HLL_EXPORT(Joypad_ClearKeyDownFlag, sact_Joypad_ClearKeyDownFlag), \
	    HLL_EXPORT(Joypad_IsKeyDown, sact_Joypad_IsKeyDown), \
	    HLL_TODO_EXPORT(Joypad_GetNumof, SACT2_Joypad_GetNumof), \
	    HLL_EXPORT(JoypadQuake_Set, SACT2_JoypadQuake_Set), \
	    HLL_EXPORT(Joypad_GetAnalogStickStatus, sact_Joypad_GetAnalogStickStatus), \
	    HLL_EXPORT(Joypad_GetDigitalStickStatus, sact_Joypad_GetDigitalStickStatus), \
	    HLL_EXPORT(Key_ClearFlag, sact_Key_ClearFlag), \
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown), \
	    HLL_TODO_EXPORT(KEY_GetState, StoatSpriteEngine_KEY_GetState), \
	    HLL_EXPORT(Timer_Get, vm_time), \
	    HLL_EXPORT(CG_IsExist, sact_CG_IsExist), \
	    HLL_EXPORT(CG_GetMetrics, sact_CG_GetMetrics), \
	    HLL_TODO_EXPORT(CSV_Load, SACT2_CSV_Load), \
	    HLL_TODO_EXPORT(CSV_Save, SACT2_CSV_Save), \
	    HLL_TODO_EXPORT(CSV_SaveAs, SACT2_CSV_SaveAs), \
	    HLL_TODO_EXPORT(CSV_CountLines, SACT2_CSV_CountLines), \
	    HLL_TODO_EXPORT(CSV_CountColumns, SACT2_CSV_CountColumns), \
	    HLL_TODO_EXPORT(CSV_Get, SACT2_CSV_Get), \
	    HLL_TODO_EXPORT(CSV_Set, SACT2_CSV_Set), \
	    HLL_TODO_EXPORT(CSV_GetInt, SACT2_CSV_GetInt), \
	    HLL_TODO_EXPORT(CSV_SetInt, SACT2_CSV_SetInt), \
	    HLL_TODO_EXPORT(CSV_Realloc, SACT2_CSV_Realloc), \
	    HLL_EXPORT(Music_IsExist, sact_Music_IsExist), \
	    HLL_EXPORT(Music_Prepare, sact_Music_Prepare), \
	    HLL_EXPORT(Music_Unprepare, sact_Music_Unprepare), \
	    HLL_EXPORT(Music_Play, sact_Music_Play), \
	    HLL_EXPORT(Music_Stop, sact_Music_Stop), \
	    HLL_EXPORT(Music_IsPlay, sact_Music_IsPlay), \
	    HLL_EXPORT(Music_SetLoopCount, SACT2_Music_SetLoopCount), \
	    HLL_EXPORT(Music_GetLoopCount, SACT2_Music_GetLoopCount), \
	    HLL_TODO_EXPORT(Music_SetLoopStartPos, SACT2_Music_SetLoopStartPos), \
	    HLL_TODO_EXPORT(Music_SetLoopEndPos, SACT2_Music_SetLoopEndPos), \
	    HLL_EXPORT(Music_Fade, sact_Music_Fade), \
	    HLL_EXPORT(Music_StopFade, SACT2_Music_StopFade), \
	    HLL_EXPORT(Music_IsFade, SACT2_Music_IsFade), \
	    HLL_EXPORT(Music_Pause, SACT2_Music_Pause), \
	    HLL_EXPORT(Music_Restart, SACT2_Music_Restart), \
	    HLL_EXPORT(Music_IsPause, SACT2_Music_IsPause), \
	    HLL_EXPORT(Music_GetPos, SACT2_Music_GetPos), \
	    HLL_EXPORT(Music_GetLength, SACT2_Music_GetLength), \
	    HLL_EXPORT(Music_GetSamplePos, SACT2_Music_GetSamplePos), \
	    HLL_EXPORT(Music_GetSampleLength, SACT2_Music_GetSampleLength), \
	    HLL_EXPORT(Music_Seek, SACT2_Music_Seek), \
	    HLL_EXPORT(Sound_IsExist, sact_Sound_IsExist), \
	    HLL_EXPORT(Sound_GetUnuseChannel, sact_Sound_GetUnuseChannel), \
	    HLL_EXPORT(Sound_Prepare, sact_Sound_Prepare), \
	    HLL_EXPORT(Sound_Unprepare, sact_Sound_Unprepare), \
	    HLL_EXPORT(Sound_Play, sact_Sound_Play), \
	    HLL_EXPORT(Sound_Stop, sact_Sound_Stop), \
	    HLL_EXPORT(Sound_IsPlay, sact_Sound_IsPlay), \
	    HLL_TODO_EXPORT(Sound_SetLoopCount, SACT2_Sound_SetLoopCount), \
	    HLL_TODO_EXPORT(Sound_GetLoopCount, SACT2_Sound_GetLoopCount), \
	    HLL_EXPORT(Sound_Fade, sact_Sound_Fade), \
	    HLL_TODO_EXPORT(Sound_StopFade, SACT2_Sound_StopFade), \
	    HLL_TODO_EXPORT(Sound_IsFade, SACT2_Sound_IsFade), \
	    HLL_TODO_EXPORT(Sound_GetPos, SACT2_Sound_GetPos), \
	    HLL_TODO_EXPORT(Sound_GetLength, SACT2_Sound_GetLength), \
	    HLL_EXPORT(Sound_ReverseLR, sact_Sound_ReverseLR), \
	    HLL_TODO_EXPORT(Sound_GetVolume, SACT2_Sound_GetVolume), \
	    HLL_EXPORT(Sound_GetTimeLength, sact_Sound_GetTimeLength), \
	    HLL_TODO_EXPORT(Sound_GetGroupNum, SACT2_Sound_GetGroupNum), \
	    HLL_EXPORT(Sound_GetGroupNumFromDataNum, SACTDX_Sound_GetGroupNumFromDataNum), \
	    HLL_TODO_EXPORT(Sound_PrepareFromFile, SACT2_Sound_PrepareFromFile), \
	    HLL_EXPORT(System_GetDate, sact_System_GetDate), \
	    HLL_EXPORT(System_GetTime, sact_System_GetTime), \
	    HLL_TODO_EXPORT(CG_RotateRGB, SACT2_CG_RotateRGB), \
	    HLL_EXPORT(CG_BlendAMapBin, sact_CG_BlendAMapBin), \
	    HLL_TODO_EXPORT(Debug_Pause, SACT2_Debug_Pause), \
	    HLL_TODO_EXPORT(Debug_GetFuncStack, SACT2_Debug_GetFuncStack), \
	    HLL_EXPORT(SP_GetAMapValue, sact_SP_GetAMapValue), \
	    HLL_EXPORT(SP_GetPixelValue, sact_SP_GetPixelValue), \
	    HLL_EXPORT(SP_SetBrightness, SACT2_SP_SetBrightness), \
	    HLL_EXPORT(SP_GetBrightness, SACT2_SP_GetBrightness), \
	    HLL_TODO_EXPORT(SP_CreateCopy, SACTDX_SP_CreateCopy),	\
	    HLL_TODO_EXPORT(Joypad_GetAnalogStickStatus, SACTDX_Joypad_GetAnalogStickStatus), \
	    HLL_TODO_EXPORT(GetDigitalStickStatus, SACTDX_GetDigitalStickStatus), \
	    HLL_TODO_EXPORT(FFT_rdft, SACTDX_FFT_rdft),		\
	    HLL_TODO_EXPORT(FFT_hanning_window, SACTDX_FFT_hanning_window),	\
	    HLL_TODO_EXPORT(Music_AnalyzeSampleData, SACTDX_Music_AnalyzeSampleData), \
	    HLL_TODO_EXPORT(Key_ClearFlagNoCtrl, SACTDX_Key_ClearFlagNoCtrl), \
	    HLL_TODO_EXPORT(Key_ClearFlagOne, SACTDX_Key_ClearFlagOne), \
	    HLL_EXPORT(TRANS_Begin, SACTDX_TRANS_Begin),	    \
	    HLL_EXPORT(TRANS_Update, SACTDX_TRANS_Update),	    \
	    HLL_EXPORT(TRANS_End, SACTDX_TRANS_End),	\
	    HLL_TODO_EXPORT(VIEW_SetMode, SACTDX_VIEW_SetMode),	\
	    HLL_TODO_EXPORT(VIEW_GetMode, SACTDX_VIEW_GetMode),	\
	    HLL_TODO_EXPORT(DX_GetUsePower2Texture, SACTDX_DX_GetUsePower2Texture),	\
	    HLL_TODO_EXPORT(DX_SetUsePower2Texture, SACTDX_DX_SetUsePower2Texture), \
	    HLL_TODO_EXPORT(MultiSprite_SetCG, StoatSpriteEngine_MultiSprite_SetCG), \
	    HLL_TODO_EXPORT(MultiSprite_SetPos, StoatSpriteEngine_MultiSprite_SetPos), \
	    HLL_TODO_EXPORT(MultiSprite_SetDefaultPos, StoatSpriteEngine_MultiSprite_SetDefaultPos), \
	    HLL_TODO_EXPORT(MultiSprite_SetZ, StoatSpriteEngine_MultiSprite_SetZ),	\
	    HLL_TODO_EXPORT(MultiSprite_SetAlpha, StoatSpriteEngine_MultiSprite_SetAlpha), \
	    HLL_TODO_EXPORT(MultiSprite_SetTransitionInfo, StoatSpriteEngine_MultiSprite_SetTransitionInfo), \
	    HLL_TODO_EXPORT(MultiSprite_SetLinkedMessageFrame, StoatSpriteEngine_MultiSprite_SetLinkedMessageFrame), \
	    HLL_TODO_EXPORT(MultiSprite_SetParentMessageFrameNum, StoatSpriteEngine_MultiSprite_SetParentMessageFrameNum), \
	    HLL_TODO_EXPORT(MultiSprite_SetOriginPosMode, StoatSpriteEngine_MultiSprite_SetOriginPosMode), \
	    HLL_TODO_EXPORT(MultiSprite_SetText, StoatSpriteEngine_MultiSprite_SetText), \
	    HLL_TODO_EXPORT(MultiSprite_SetCharSpace, StoatSpriteEngine_MultiSprite_SetCharSpace), \
	    HLL_TODO_EXPORT(MultiSprite_CharSpriteProperty_SetSize, StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetSize), \
	    HLL_TODO_EXPORT(MultiSprite_CharSpriteProperty_SetColor, StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetColor), \
	    HLL_TODO_EXPORT(MultiSprite_CharSpriteProperty_SetBoldWeight, StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetBoldWeight), \
	    HLL_TODO_EXPORT(MultiSprite_CharSpriteProperty_SetEdgeWeight, StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetEdgeWeight), \
	    HLL_TODO_EXPORT(MultiSprite_CharSpriteProperty_SetEdgeColor, StoatSpriteEngine_MultiSprite_CharSpriteProperty_SetEdgeColor), \
	    HLL_TODO_EXPORT(MultiSprite_GetTransitionInfo, StoatSpriteEngine_MultiSprite_GetTransitionInfo), \
	    HLL_TODO_EXPORT(MultiSprite_GetCG, StoatSpriteEngine_MultiSprite_GetCG), \
	    HLL_TODO_EXPORT(MultiSprite_GetPos, StoatSpriteEngine_MultiSprite_GetPos), \
	    HLL_TODO_EXPORT(MultiSprite_GetAlpha, StoatSpriteEngine_MultiSprite_GetAlpha), \
	    HLL_TODO_EXPORT(MultiSprite_ResetDefaultPos, StoatSpriteEngine_MultiSprite_ResetDefaultPos), \
	    HLL_TODO_EXPORT(MultiSprite_ResetAllDefaultPos, StoatSpriteEngine_MultiSprite_ResetAllDefaultPos), \
	    HLL_TODO_EXPORT(MultiSprite_ResetAllCG, StoatSpriteEngine_MultiSprite_ResetAllCG), \
	    HLL_TODO_EXPORT(MultiSprite_SetAllShowMessageFrame, StoatSpriteEngine_MultiSprite_SetAllShowMessageFrame), \
	    HLL_TODO_EXPORT(MultiSprite_BeginMove, StoatSpriteEngine_MultiSprite_BeginMove), \
	    HLL_TODO_EXPORT(MultiSprite_BeginMoveWithAlpha, StoatSpriteEngine_MultiSprite_BeginMoveWithAlpha), \
	    HLL_TODO_EXPORT(MultiSprite_GetMaxMoveTotalTime, StoatSpriteEngine_MultiSprite_GetMaxMoveTotalTime), \
	    HLL_TODO_EXPORT(MultiSprite_SetAllMoveCurrentTime, StoatSpriteEngine_MultiSprite_SetAllMoveCurrentTime), \
	    HLL_TODO_EXPORT(MultiSprite_EndAllMove, StoatSpriteEngine_MultiSprite_EndAllMove), \
	    HLL_TODO_EXPORT(MultiSprite_UpdateView, StoatSpriteEngine_MultiSprite_UpdateView), \
	    HLL_TODO_EXPORT(MultiSprite_Rebuild, StoatSpriteEngine_MultiSprite_Rebuild), \
	    HLL_TODO_EXPORT(MultiSprite_Encode, StoatSpriteEngine_MultiSprite_Encode), \
	    HLL_TODO_EXPORT(MultiSprite_Decode, StoatSpriteEngine_MultiSprite_Decode), \
	    HLL_EXPORT(SYSTEM_IsResetOnce, StoatSpriteEngine_SYSTEM_IsResetOnce));

