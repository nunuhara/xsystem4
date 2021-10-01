/* Copyright (C) 2021 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include "hll.h"
#include "audio.h"

HLL_WARN_UNIMPLEMENTED( , void, KiwiSoundEngine, SetGlobalFocus, possibly_unused int nNum);

int KiwiSoundEngine_Music_IsExist(int nNum)
{
	return bgm_exists(nNum-1);
}

int KiwiSoundEngine_Music_Prepare(int nCh, int nNum)
{
	return bgm_prepare(nCh, nNum-1);
}

int KiwiSoundEngine_Sound_IsExist(int nNum)
{
	return wav_exists(nNum-1);
}
int KiwiSoundEngine_Sound_Prepare(int nCh, int nNum)
{
	return wav_prepare(nCh, nNum-1);
}

//int KiwiSoundEngine_Sound_GetGroupNum(int nCh);
static int KiwiSoundEngine_Sound_GetGroupNumFromDataNum(possibly_unused int no)
{
	return 0;
}

//bool KiwiSoundEngine_Sound_PrepareFromFile(int nCh, struct string *szFileName);

HLL_LIBRARY(KiwiSoundEngine,
	    HLL_EXPORT(SetGlobalFocus, KiwiSoundEngine_SetGlobalFocus),
	    HLL_EXPORT(Music_IsExist, KiwiSoundEngine_Music_IsExist),
	    HLL_EXPORT(Music_Prepare, KiwiSoundEngine_Music_Prepare),
	    HLL_EXPORT(Music_Unprepare, bgm_unprepare),
	    HLL_EXPORT(Music_Play, bgm_play),
	    HLL_EXPORT(Music_Stop, bgm_stop),
	    HLL_EXPORT(Music_IsPlay, bgm_is_playing),
	    HLL_EXPORT(Music_SetLoopCount, bgm_set_loop_count),
	    HLL_EXPORT(Music_GetLoopCount, bgm_get_loop_count),
	    HLL_EXPORT(Music_SetLoopStartPos, bgm_set_loop_start_pos),
	    HLL_EXPORT(Music_SetLoopEndPos, bgm_set_loop_end_pos),
	    HLL_EXPORT(Music_Fade, bgm_fade),
	    HLL_EXPORT(Music_StopFade, bgm_stop_fade),
	    HLL_EXPORT(Music_IsFade, bgm_is_fading),
	    HLL_EXPORT(Music_Pause, bgm_pause),
	    HLL_EXPORT(Music_Restart, bgm_restart),
	    HLL_EXPORT(Music_IsPause, bgm_is_paused),
	    HLL_EXPORT(Music_GetPos, bgm_get_pos),
	    HLL_EXPORT(Music_GetLength, bgm_get_length),
	    HLL_EXPORT(Music_GetSamplePos, bgm_get_sample_pos),
	    HLL_EXPORT(Music_GetSampleLength, bgm_get_sample_length),
	    HLL_EXPORT(Music_Seek, bgm_seek),
	    //HLL_EXPORT(Music_MillisecondsToSamples, KiwiSoundEngine_Music_MillisecondsToSamples),
	    //HLL_EXPORT(Music_GetFormat, KiwiSoundEngine_Music_GetFormat),
	    HLL_EXPORT(Sound_IsExist, KiwiSoundEngine_Sound_IsExist),
	    HLL_EXPORT(Sound_Prepare, KiwiSoundEngine_Sound_Prepare),
	    HLL_EXPORT(Sound_Unprepare, wav_unprepare),
	    HLL_EXPORT(Sound_Play, wav_play),
	    HLL_EXPORT(Sound_Stop, wav_stop),
	    HLL_EXPORT(Sound_IsPlay, wav_is_playing),
	    HLL_EXPORT(Sound_Fade, wav_fade),
	    HLL_EXPORT(Sound_StopFade, wav_stop_fade),
	    HLL_EXPORT(Sound_IsFade, wav_is_fading),
	    HLL_EXPORT(Sound_GetTimeLength, wav_get_time_length),
	    //HLL_EXPORT(Sound_GetGroupNum, KiwiSoundEngine_Sound_GetGroupNum),
	    HLL_EXPORT(Sound_GetGroupNumFromDataNum, KiwiSoundEngine_Sound_GetGroupNumFromDataNum)
	    //HLL_EXPORT(Sound_PrepareFromFile, KiwiSoundEngine_Sound_PrepareFromFile)
	);
