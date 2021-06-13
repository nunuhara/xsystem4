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

HLL_WARN_UNIMPLEMENTED( , void, KiwiSoundEngine, SetGlobalFocus, int nNum);
int KiwiSoundEngine_Music_IsExist(int nNum)
{
	return bgm_exists(nNum-1);
}
int KiwiSoundEngine_Music_Prepare(int nCh, int nNum)
{
	return bgm_prepare(nCh, nNum-1);
}
int KiwiSoundEngine_Music_Unprepare(int nCh)
{
	return bgm_unprepare(nCh);
}
int KiwiSoundEngine_Music_Play(int nCh)
{
	return bgm_play(nCh);
}
int KiwiSoundEngine_Music_Stop(int nCh)
{
	return bgm_stop(nCh);
}
int KiwiSoundEngine_Music_IsPlay(int nCh)
{
	return bgm_is_playing(nCh);
}
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_SetLoopCount, int nCh, int nCount);
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_GetLoopCount, int nCh);
//int KiwiSoundEngine_Music_SetLoopStartPos(int nCh, int dwPos);
//int KiwiSoundEngine_Music_SetLoopEndPos(int nCh, int dwPos);
int KiwiSoundEngine_Music_Fade(int nCh, int nTime, int nVolume, int bStop)
{
	return bgm_fade(nCh, nTime, nVolume, bStop);
}
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_StopFade, int nCh);
HLL_WARN_UNIMPLEMENTED(0, int, KiwiSoundEngine, Music_IsFade, int nCh);
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_Pause, int nCh);
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_Restart, int nCh);
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_IsPause, int nCh);
HLL_QUIET_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_GetPos, int nCh);
HLL_QUIET_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_GetLength, int nCh);
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_GetSamplePos, int nCh);
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_GetSampleLength, int nCh);
HLL_WARN_UNIMPLEMENTED(1, int, KiwiSoundEngine, Music_Seek, int nCh, int dwPos);
//int KiwiSoundEngine_Music_MillisecondsToSamples(int nMillisec, int nSamplesPerSec);
//int KiwiSoundEngine_Music_GetFormat(int nCh, int *pnSamplesPerSec, int *pnBitsPerSample, int *pnChannels);
int KiwiSoundEngine_Sound_IsExist(int nNum)
{
	return wav_exists(nNum-1);
}
int KiwiSoundEngine_Sound_Prepare(int nCh, int nNum)
{
	return wav_prepare(nCh, nNum-1);
}
int KiwiSoundEngine_Sound_Unprepare(int nCh)
{
	return wav_unprepare(nCh);
}
int KiwiSoundEngine_Sound_Play(int nCh)
{
	return wav_play(nCh);
}
int KiwiSoundEngine_Sound_Stop(int nCh)
{
	return wav_stop(nCh);
}
int KiwiSoundEngine_Sound_IsPlay(int nCh)
{
	return wav_is_playing(nCh);
}
int KiwiSoundEngine_Sound_Fade(int nCh, int nTime, int nVolume, int bStop)
{
	return wav_fade(nCh, nTime, nVolume, bStop);
}
//int KiwiSoundEngine_Sound_StopFade(int nCh);
//int KiwiSoundEngine_Sound_IsFade(int nCh);
int KiwiSoundEngine_Sound_GetTimeLength(int nCh)
{
	return wav_get_time_length(nCh);
}
//int KiwiSoundEngine_Sound_GetGroupNum(int nCh);
static int KiwiSoundEngine_Sound_GetGroupNumFromDataNum(int no)
{
	return 0;
}

//bool KiwiSoundEngine_Sound_PrepareFromFile(int nCh, struct string *szFileName);

HLL_LIBRARY(KiwiSoundEngine,
	    HLL_EXPORT(SetGlobalFocus, KiwiSoundEngine_SetGlobalFocus),
	    HLL_EXPORT(Music_IsExist, KiwiSoundEngine_Music_IsExist),
	    HLL_EXPORT(Music_Prepare, KiwiSoundEngine_Music_Prepare),
	    HLL_EXPORT(Music_Unprepare, KiwiSoundEngine_Music_Unprepare),
	    HLL_EXPORT(Music_Play, KiwiSoundEngine_Music_Play),
	    HLL_EXPORT(Music_Stop, KiwiSoundEngine_Music_Stop),
	    HLL_EXPORT(Music_IsPlay, KiwiSoundEngine_Music_IsPlay),
	    HLL_EXPORT(Music_SetLoopCount, KiwiSoundEngine_Music_SetLoopCount),
	    HLL_EXPORT(Music_GetLoopCount, KiwiSoundEngine_Music_GetLoopCount),
	    //HLL_EXPORT(Music_SetLoopStartPos, KiwiSoundEngine_Music_SetLoopStartPos),
	    //HLL_EXPORT(Music_SetLoopEndPos, KiwiSoundEngine_Music_SetLoopEndPos),
	    HLL_EXPORT(Music_Fade, KiwiSoundEngine_Music_Fade),
	    HLL_EXPORT(Music_StopFade, KiwiSoundEngine_Music_StopFade),
	    HLL_EXPORT(Music_IsFade, KiwiSoundEngine_Music_IsFade),
	    HLL_EXPORT(Music_Pause, KiwiSoundEngine_Music_Pause),
	    HLL_EXPORT(Music_Restart, KiwiSoundEngine_Music_Restart),
	    HLL_EXPORT(Music_IsPause, KiwiSoundEngine_Music_IsPause),
	    HLL_EXPORT(Music_GetPos, KiwiSoundEngine_Music_GetPos),
	    HLL_EXPORT(Music_GetLength, KiwiSoundEngine_Music_GetLength),
	    HLL_EXPORT(Music_GetSamplePos, KiwiSoundEngine_Music_GetSamplePos),
	    HLL_EXPORT(Music_GetSampleLength, KiwiSoundEngine_Music_GetSampleLength),
	    HLL_EXPORT(Music_Seek, KiwiSoundEngine_Music_Seek),
	    //HLL_EXPORT(Music_MillisecondsToSamples, KiwiSoundEngine_Music_MillisecondsToSamples),
	    //HLL_EXPORT(Music_GetFormat, KiwiSoundEngine_Music_GetFormat),
	    HLL_EXPORT(Sound_IsExist, KiwiSoundEngine_Sound_IsExist),
	    HLL_EXPORT(Sound_Prepare, KiwiSoundEngine_Sound_Prepare),
	    HLL_EXPORT(Sound_Unprepare, KiwiSoundEngine_Sound_Unprepare),
	    HLL_EXPORT(Sound_Play, KiwiSoundEngine_Sound_Play),
	    HLL_EXPORT(Sound_Stop, KiwiSoundEngine_Sound_Stop),
	    HLL_EXPORT(Sound_IsPlay, KiwiSoundEngine_Sound_IsPlay),
	    HLL_EXPORT(Sound_Fade, KiwiSoundEngine_Sound_Fade),
	    //HLL_EXPORT(Sound_StopFade, KiwiSoundEngine_Sound_StopFade),
	    //HLL_EXPORT(Sound_IsFade, KiwiSoundEngine_Sound_IsFade),
	    HLL_EXPORT(Sound_GetTimeLength, KiwiSoundEngine_Sound_GetTimeLength),
	    //HLL_EXPORT(Sound_GetGroupNum, KiwiSoundEngine_Sound_GetGroupNum),
	    HLL_EXPORT(Sound_GetGroupNumFromDataNum, KiwiSoundEngine_Sound_GetGroupNumFromDataNum)
	    //HLL_EXPORT(Sound_PrepareFromFile, KiwiSoundEngine_Sound_PrepareFromFile)
	);
