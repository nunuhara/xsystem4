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
#include "audio.h"
#include "input.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/ald.h"
#include "system4/cg.h"
#include "system4/string.h"

int sact_GAME_MSG_GetNumOf(void)
{
	return ain->nr_messages;
}

void sact_IntToZenkaku(struct string **s, int value, int figures, int zero_pad)
{
	int i;
	char buf[512];

	string_clear(*s);
	i = int_to_cstr(buf, 512, value, figures, zero_pad, true);
	string_append_cstr(s, buf, i);
}

void sact_IntToHankaku(struct string **s, int value, int figures, int zero_pad)
{
	int i;
	char buf[512];

	string_clear(*s);
	i = int_to_cstr(buf, 512, value, figures, zero_pad, false);
	string_append_cstr(s, buf, i);
}

int sact_Mouse_GetPos(int *x, int *y)
{
	handle_events();
	mouse_get_pos(x, y);
	return mouse_focus && keyboard_focus;
}

void sact_Mouse_ClearWheel(void)
{
	// TODO
}

void sact_Mouse_GetWheel(int *forward, int *back)
{
	// TODO
	*forward = 0;
	*back    = 0;
}

void sact_Joypad_ClearKeyDownFlag(int n)
{
	// TODO
}

int sact_Joypad_IsKeyDown(int num, int key)
{
	return 0; // TODO
}

bool sact_Joypad_GetAnalogStickStatus(int num, int type, float *degree, float *power)
{
	//hll_unimplemented_warning(SACT2, Joypad_GetAnalogStickStatus);
	*degree = 0.0;
	*power  = 0.0;
	return true;
}

bool sact_Joypad_GetDigitalStickStatus(int num, int type, bool *left, bool *right, bool *up, bool *down)
{
	//hll_unimplemented_warning(SACT2, Joypad_GetDigitalStickStatus);
	*left  = false;
	*right = false;
	*up    = false;
	*down  = false;
	return 1;
}

int sact_Key_ClearFlag(void)
{
	key_clear_flag();
	return 1;
}

int sact_Key_IsDown(int keycode)
{
	handle_events();
	return key_is_down(keycode);
}

int sact_CG_IsExist(int cg_no)
{
	return ald[ALDFILE_CG] && archive_exists(ald[ALDFILE_CG], cg_no - 1);
}

int sact_Music_IsExist(int n)
{
	return audio_exists(AUDIO_MUSIC, n - 1);
}

int sact_Music_Prepare(int ch, int n)
{
	return audio_prepare(AUDIO_MUSIC, ch, n - 1);
}

int sact_Music_Unprepare(int ch)
{
	return audio_unprepare(AUDIO_MUSIC, ch);
}

int sact_Music_Play(int ch)
{
	return audio_play(AUDIO_MUSIC, ch);
}

int sact_Music_Stop(int ch)
{
	return audio_stop(AUDIO_MUSIC, ch);
}

int sact_Music_IsPlay(int ch)
{
	return audio_is_playing(AUDIO_MUSIC, ch);
}

int sact_Music_Fade(int ch, int time, int volume, int stop)
{
	return audio_fade(AUDIO_MUSIC, ch, time, volume, stop);
}

int sact_Sound_IsExist(int n)
{
	return audio_exists(AUDIO_SOUND, n - 1);
}

int sact_Sound_GetUnuseChannel(void)
{
	return audio_get_unused_channel(AUDIO_SOUND);
}

int sact_Sound_Prepare(int ch, int n)
{
	return audio_prepare(AUDIO_SOUND, ch, n - 1);
}

int sact_Sound_Unprepare(int ch)
{
	return audio_unprepare(AUDIO_SOUND, ch);
}

int sact_Sound_Play(int ch)
{
	return audio_play(AUDIO_SOUND, ch);
}

int sact_Sound_Stop(int ch)
{
	return audio_stop(AUDIO_SOUND, ch);
}

int sact_Sound_IsPlay(int ch)
{
	return audio_is_playing(AUDIO_SOUND, ch);
}

int sact_Sound_Fade(int ch, int time, int volume, int stop)
{
	return audio_fade(AUDIO_SOUND, ch, time, volume, stop);
}

int sact_Sound_GetTimeLength(int ch)
{
	return audio_get_time_length(AUDIO_SOUND, ch);
}

void sact_System_GetDate(int *year, int *month, int *mday, int *wday)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	*year  = tm->tm_year;
	*month = tm->tm_mon;
	*mday  = tm->tm_mday;
	*wday  = tm->tm_wday;
}

void sact_System_GetTime(int *hour, int *min, int *sec, int *ms)
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

void sact_CG_BlendAMapBin(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int border)
{
	gfx_copy_use_amap_border(sact_get_texture(dst), dx, dy, sact_get_texture(src), sx, sy, w, h, border);
}

HLL_UNIMPLEMENTED(int, SACT2, Error, struct string *err);
HLL_UNIMPLEMENTED(int, SACT2, WP_GetSP, int sp);
HLL_UNIMPLEMENTED(int, SACT2, WP_SetSP, int sp);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, EffectSetMask, int cg);
HLL_UNIMPLEMENTED(int, SACT2, EffectSetMaskSP, int sp);
HLL_WARN_UNIMPLEMENTED( , void, SACT2, QuakeScreen, int amp_x, int amp_y, int time, int key);
HLL_UNIMPLEMENTED(void, SACT2, QUAKE_SET_CROSS, int amp_x, int amp_y);
HLL_UNIMPLEMENTED(void, SACT2, QUAKE_SET_ROTATION, int amp, int cycle);
HLL_UNIMPLEMENTED(int, SACT2, SP_SetCGFromFile, int sp, struct string *filename);
HLL_UNIMPLEMENTED(int, SACT2, SP_SaveCG, int sp, struct string *filename);
HLL_UNIMPLEMENTED(int, SACT2, SP_CreateCustom, int sp);
HLL_UNIMPLEMENTED(int, SACT2, SP_TextBackSpace, int sp_no);
HLL_UNIMPLEMENTED(void, SACT2, GAME_MSG_Get, int index, struct string **text);
HLL_UNIMPLEMENTED(int, SACT2, StringPopFront, struct string **dst, struct string **src);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Mouse_SetPos, int x, int y);
HLL_UNIMPLEMENTED(int, SACT2, Joypad_GetNumof, void);
HLL_WARN_UNIMPLEMENTED( , void, SACT2, JoypadQuake_Set, int num, int type, int magnitude);
HLL_UNIMPLEMENTED(int,  SACT2, CSV_Load, struct string *filename);
HLL_UNIMPLEMENTED(int,  SACT2, CSV_Save, void);
HLL_UNIMPLEMENTED(int,  SACT2, CSV_SaveAs, struct string *filename);
HLL_UNIMPLEMENTED(int,  SACT2, CSV_CountLines, void);
HLL_UNIMPLEMENTED(int,  SACT2, CSV_CountColumns, void);
HLL_UNIMPLEMENTED(void, SACT2, CSV_Get, struct string **s, int line, int column);
HLL_UNIMPLEMENTED(int,  SACT2, CSV_Set, int line, int column, struct string *data);
HLL_UNIMPLEMENTED(int,  SACT2, CSV_GetInt, int line, int column);
HLL_UNIMPLEMENTED(void, SACT2, CSV_SetInt, int line, int column, int data);
HLL_UNIMPLEMENTED(void, SACT2, CSV_Realloc, int lines, int columns);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_SetLoopCount, int ch, int count);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Music_GetLoopCount, int ch);
HLL_UNIMPLEMENTED(int, SACT2, Music_SetLoopStartPos, int ch, int pos);
HLL_UNIMPLEMENTED(int, SACT2, Music_SetLoopEndPos, int ch, int pos);
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
HLL_UNIMPLEMENTED(int, SACT2, Sound_SetLoopCount, int ch, int count);
HLL_UNIMPLEMENTED(int, SACT2, Sound_GetLoopCount, int ch);
HLL_UNIMPLEMENTED(int, SACT2, Sound_StopFade, int ch);
HLL_UNIMPLEMENTED(int, SACT2, Sound_IsFade, int ch);
HLL_UNIMPLEMENTED(int, SACT2, Sound_GetPos, int ch);
HLL_UNIMPLEMENTED(int, SACT2, Sound_GetLength, int ch);
HLL_WARN_UNIMPLEMENTED(1, int, SACT2, Sound_ReverseLR, int ch);
HLL_UNIMPLEMENTED(int, SACT2, Sound_GetVolume, int ch);
HLL_UNIMPLEMENTED(int, SACT2, Sound_GetGroupNum, int ch);
HLL_UNIMPLEMENTED(bool, SACT2, Sound_PrepareFromFile, int ch, struct string *filename);
HLL_UNIMPLEMENTED(void, SACT2, CG_RotateRGB, int dst, int dx, int dy, int w, int h, int rotate_type);
HLL_UNIMPLEMENTED(void, SACT2, Debug_Pause, void);
HLL_UNIMPLEMENTED(void, SACT2, Debug_GetFuncStack, struct string **s, int nest);
HLL_UNIMPLEMENTED(int, SACT2, SP_SetBrightness, int sp_no, int brightness);
HLL_UNIMPLEMENTED(int, SACT2, SP_GetBrightness, int sp_no);

HLL_LIBRARY(SACT2,
	    HLL_EXPORT(Init, sact_Init),
	    HLL_EXPORT(Error, SACT2_Error),
	    HLL_EXPORT(SetWP, sact_SetWP),
	    HLL_EXPORT(SetWP_Color, sact_SetWP_Color),
	    HLL_EXPORT(WP_GetSP, SACT2_WP_GetSP),
	    HLL_EXPORT(WP_SetSP, SACT2_WP_SetSP),
	    HLL_EXPORT(GetScreenWidth, sact_GetScreenWidth),
	    HLL_EXPORT(GetScreenHeight, sact_GetScreenHeight),
	    HLL_EXPORT(GetMainSurfaceNumber, sact_GetMainSurfaceNumber),
	    HLL_EXPORT(Update, sact_Update),
	    HLL_EXPORT(Effect, sact_Effect),
	    HLL_EXPORT(EffectSetMask, SACT2_EffectSetMask),
	    HLL_EXPORT(EffectSetMaskSP, SACT2_EffectSetMaskSP),
	    HLL_EXPORT(QuakeScreen, SACT2_QuakeScreen),
	    HLL_EXPORT(QUAKE_SET_CROSS, SACT2_QUAKE_SET_CROSS),
	    HLL_EXPORT(QUAKE_SET_ROTATION, SACT2_QUAKE_SET_ROTATION),
	    HLL_EXPORT(SP_GetUnuseNum, sact_SP_GetUnuseNum),
	    HLL_EXPORT(SP_Count, sact_SP_Count),
	    HLL_EXPORT(SP_Enum, sact_SP_Enum),
	    HLL_EXPORT(SP_GetMaxZ, sact_SP_GetMaxZ),
	    HLL_EXPORT(SP_SetCG, sact_SP_SetCG),
	    HLL_EXPORT(SP_SetCGFromFile, SACT2_SP_SetCGFromFile),
	    HLL_EXPORT(SP_SaveCG, SACT2_SP_SaveCG),
	    HLL_EXPORT(SP_Create, sact_SP_Create),
	    HLL_EXPORT(SP_CreatePixelOnly, sact_SP_CreatePixelOnly),
	    HLL_EXPORT(SP_CreateCustom, SACT2_SP_CreateCustom),
	    HLL_EXPORT(SP_Delete, sact_SP_Delete),
	    HLL_EXPORT(SP_SetPos, sact_SP_SetPos),
	    HLL_EXPORT(SP_SetX, sact_SP_SetX),
	    HLL_EXPORT(SP_SetY, sact_SP_SetY),
	    HLL_EXPORT(SP_SetZ, sact_SP_SetZ),
	    HLL_EXPORT(SP_SetBlendRate, sact_SP_SetBlendRate),
	    HLL_EXPORT(SP_SetShow, sact_SP_SetShow),
	    HLL_EXPORT(SP_SetDrawMethod, sact_SP_SetDrawMethod),
	    HLL_EXPORT(SP_IsUsing, sact_SP_IsUsing),
	    HLL_EXPORT(SP_ExistAlpha, sact_SP_ExistsAlpha),
	    HLL_EXPORT(SP_GetPosX, sact_SP_GetPosX),
	    HLL_EXPORT(SP_GetPosY, sact_SP_GetPosY),
	    HLL_EXPORT(SP_GetWidth, sact_SP_GetWidth),
	    HLL_EXPORT(SP_GetHeight, sact_SP_GetHeight),
	    HLL_EXPORT(SP_GetZ, sact_SP_GetZ),
	    HLL_EXPORT(SP_GetBlendRate, sact_SP_GetBlendRate),
	    HLL_EXPORT(SP_GetShow, sact_SP_GetShow),
	    HLL_EXPORT(SP_GetDrawMethod, sact_SP_GetDrawMethod),
	    HLL_EXPORT(SP_SetTextHome, sact_SP_SetTextHome),
	    HLL_EXPORT(SP_SetTextLineSpace, sact_SP_SetTextLineSpace),
	    HLL_EXPORT(SP_SetTextCharSpace, sact_SP_SetTextCharSpace),
	    HLL_EXPORT(SP_SetTextPos, sact_SP_SetTextPos),
	    HLL_EXPORT(SP_TextDraw, sact_SP_TextDraw),
	    HLL_EXPORT(SP_TextClear, sact_SP_TextClear),
	    HLL_EXPORT(SP_TextHome, sact_SP_TextHome),
	    HLL_EXPORT(SP_TextNewLine, sact_SP_TextNewLine),
	    HLL_EXPORT(SP_TextBackSpace, SACT2_SP_TextBackSpace),
	    HLL_EXPORT(SP_TextCopy, sact_SP_TextCopy),
	    HLL_EXPORT(SP_GetTextHomeX, sact_SP_GetTextHomeX),
	    HLL_EXPORT(SP_GetTextHomeY, sact_SP_GetTextHomeY),
	    HLL_EXPORT(SP_GetTextCharSpace, sact_SP_GetTextCharSpace),
	    HLL_EXPORT(SP_GetTextPosX, sact_SP_GetTextPosX),
	    HLL_EXPORT(SP_GetTextPosY, sact_SP_GetTextPosY),
	    HLL_EXPORT(SP_GetTextLineSpace, sact_SP_GetTextLineSpace),
	    HLL_EXPORT(SP_IsPtIn, sact_SP_IsPtIn),
	    HLL_EXPORT(SP_IsPtInRect, sact_SP_IsPtInRect),
	    HLL_EXPORT(GAME_MSG_GetNumof, sact_GAME_MSG_GetNumOf),
	    HLL_EXPORT(GAME_MSG_Get, SACT2_GAME_MSG_Get),
	    HLL_EXPORT(IntToZenkaku, sact_IntToZenkaku),
	    HLL_EXPORT(IntToHankaku, sact_IntToHankaku),
	    HLL_EXPORT(StringPopFront, SACT2_StringPopFront),
	    HLL_EXPORT(Mouse_GetPos, sact_Mouse_GetPos),
	    HLL_EXPORT(Mouse_SetPos, SACT2_Mouse_SetPos),
	    HLL_EXPORT(Mouse_ClearWheel, sact_Mouse_ClearWheel),
	    HLL_EXPORT(Mouse_GetWheel, sact_Mouse_GetWheel),
	    HLL_EXPORT(Joypad_ClearKeyDownFlag, sact_Joypad_ClearKeyDownFlag),
	    HLL_EXPORT(Joypad_IsKeyDown, sact_Joypad_IsKeyDown),
	    HLL_EXPORT(Joypad_GetNumof, SACT2_Joypad_GetNumof),
	    HLL_EXPORT(JoypadQuake_Set, SACT2_JoypadQuake_Set),
	    HLL_EXPORT(Joypad_GetAnalogStickStatus, sact_Joypad_GetAnalogStickStatus),
	    HLL_EXPORT(Joypad_GetDigitalStickStatus, sact_Joypad_GetDigitalStickStatus),
	    HLL_EXPORT(Key_ClearFlag, sact_Key_ClearFlag),
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown),
	    HLL_EXPORT(Timer_Get, vm_time),
	    HLL_EXPORT(CG_IsExist, sact_CG_IsExist),
	    HLL_EXPORT(CG_GetMetrics, sact_CG_GetMetrics),
	    HLL_EXPORT(CSV_Load, SACT2_CSV_Load),
	    HLL_EXPORT(CSV_Save, SACT2_CSV_Save),
	    HLL_EXPORT(CSV_SaveAs, SACT2_CSV_SaveAs),
	    HLL_EXPORT(CSV_CountLines, SACT2_CSV_CountLines),
	    HLL_EXPORT(CSV_CountColumns, SACT2_CSV_CountColumns),
	    HLL_EXPORT(CSV_Get, SACT2_CSV_Get),
	    HLL_EXPORT(CSV_Set, SACT2_CSV_Set),
	    HLL_EXPORT(CSV_GetInt, SACT2_CSV_GetInt),
	    HLL_EXPORT(CSV_SetInt, SACT2_CSV_SetInt),
	    HLL_EXPORT(CSV_Realloc, SACT2_CSV_Realloc),
	    HLL_EXPORT(Music_IsExist, sact_Music_IsExist),
	    HLL_EXPORT(Music_Prepare, sact_Music_Prepare),
	    HLL_EXPORT(Music_Unprepare, sact_Music_Unprepare),
	    HLL_EXPORT(Music_Play, sact_Music_Play),
	    HLL_EXPORT(Music_Stop, sact_Music_Stop),
	    HLL_EXPORT(Music_IsPlay, sact_Music_IsPlay),
	    HLL_EXPORT(Music_SetLoopCount, SACT2_Music_SetLoopCount),
	    HLL_EXPORT(Music_GetLoopCount, SACT2_Music_GetLoopCount),
	    HLL_EXPORT(Music_SetLoopStartPos, SACT2_Music_SetLoopStartPos),
	    HLL_EXPORT(Music_SetLoopEndPos, SACT2_Music_SetLoopEndPos),
	    HLL_EXPORT(Music_Fade, sact_Music_Fade),
	    HLL_EXPORT(Music_StopFade, SACT2_Music_StopFade),
	    HLL_EXPORT(Music_IsFade, SACT2_Music_IsFade),
	    HLL_EXPORT(Music_Pause, SACT2_Music_Pause),
	    HLL_EXPORT(Music_Restart, SACT2_Music_Restart),
	    HLL_EXPORT(Music_IsPause, SACT2_Music_IsPause),
	    HLL_EXPORT(Music_GetPos, SACT2_Music_GetPos),
	    HLL_EXPORT(Music_GetLength, SACT2_Music_GetLength),
	    HLL_EXPORT(Music_GetSamplePos, SACT2_Music_GetSamplePos),
	    HLL_EXPORT(Music_GetSampleLength, SACT2_Music_GetSampleLength),
	    HLL_EXPORT(Music_Seek, SACT2_Music_Seek),
	    HLL_EXPORT(Sound_IsExist, sact_Sound_IsExist),
	    HLL_EXPORT(Sound_GetUnuseChannel, sact_Sound_GetUnuseChannel),
	    HLL_EXPORT(Sound_Prepare, sact_Sound_Prepare),
	    HLL_EXPORT(Sound_Unprepare, sact_Sound_Unprepare),
	    HLL_EXPORT(Sound_Play, sact_Sound_Play),
	    HLL_EXPORT(Sound_Stop, sact_Sound_Stop),
	    HLL_EXPORT(Sound_IsPlay, sact_Sound_IsPlay),
	    HLL_EXPORT(Sound_SetLoopCount, SACT2_Sound_SetLoopCount),
	    HLL_EXPORT(Sound_GetLoopCount, SACT2_Sound_GetLoopCount),
	    HLL_EXPORT(Sound_Fade, sact_Sound_Fade),
	    HLL_EXPORT(Sound_StopFade, SACT2_Sound_StopFade),
	    HLL_EXPORT(Sound_IsFade, SACT2_Sound_IsFade),
	    HLL_EXPORT(Sound_GetPos, SACT2_Sound_GetPos),
	    HLL_EXPORT(Sound_GetLength, SACT2_Sound_GetLength),
	    HLL_EXPORT(Sound_ReverseLR, SACT2_Sound_ReverseLR),
	    HLL_EXPORT(Sound_GetVolume, SACT2_Sound_GetVolume),
	    HLL_EXPORT(Sound_GetTimeLength, sact_Sound_GetTimeLength),
	    HLL_EXPORT(Sound_GetGroupNum, SACT2_Sound_GetGroupNum),
	    HLL_EXPORT(Sound_PrepareFromFile, SACT2_Sound_PrepareFromFile),
	    HLL_EXPORT(System_GetDate, sact_System_GetDate),
	    HLL_EXPORT(System_GetTime, sact_System_GetTime),
	    HLL_EXPORT(CG_RotateRGB, SACT2_CG_RotateRGB),
	    HLL_EXPORT(CG_BlendAMapBin, sact_CG_BlendAMapBin),
	    HLL_EXPORT(Debug_Pause, SACT2_Debug_Pause),
	    HLL_EXPORT(Debug_GetFuncStack, SACT2_Debug_GetFuncStack),
	    HLL_EXPORT(SP_GetAMapValue, sact_SP_GetAMapValue),
	    HLL_EXPORT(SP_GetPixelValue, sact_SP_GetPixelValue),
	    HLL_EXPORT(SP_SetBrightness, SACT2_SP_SetBrightness),
	    HLL_EXPORT(SP_GetBrightness, SACT2_SP_GetBrightness));
