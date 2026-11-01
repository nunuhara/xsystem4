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

#include <stdio.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"

#include "vm.h"
#include "hll.h"
#include "asset_manager.h"
#include "audio.h"
#include "audio_meta.h"

HLL_WARN_UNIMPLEMENTED( , void, KiwiSoundEngine, SetGlobalFocus, possibly_unused int nNum);

//int KiwiSoundEngine_Sound_GetGroupNum(int nCh);
//static bool KiwiSoundEngine_Sound_PrepareFromFile(int ch, struct string *filename);

static int KiwiSoundEngine_GetMasterGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetBGMGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetSEGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetVoiceGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetGimicSEGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetBackVoiceGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_MillisecondsToSamples(int ms, int samples_per_sec)
{
	return (int)((int64_t)ms * samples_per_sec / 1000);
}

static int KiwiSoundEngine_GetSoundFileName(int num, struct string **file_name)
{
	struct string *name = audio_meta_get_sound_name_by_number(num);
	if (*file_name)
		free_string(*file_name);
	*file_name = string_ref(name ? name : &EMPTY_STRING);
	return name != NULL;
}

static bool rance9_bgm_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_BGM, name, NULL);
}

static int rance9_bgm_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_BGM, name, NULL);
	if (!dfile)
		return 0;
	return bgm_prepare_from_archive_data(ch, num, dfile);
}

static bool rance9_wav_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_SOUND, name, NULL);
}

static int rance9_wav_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_SOUND, name, NULL);
	if (!dfile)
		return 0;
	return wav_prepare_from_archive_data(ch, num, dfile);
}

static void KiwiSoundEngine_PreLink(void);

HLL_LIBRARY(KiwiSoundEngine,
	    HLL_EXPORT(_PreLink, KiwiSoundEngine_PreLink),
	    HLL_EXPORT(SetGlobalFocus, KiwiSoundEngine_SetGlobalFocus),
	    HLL_EXPORT(Music_IsExist, bgm_exists),
	    HLL_EXPORT(Music_Prepare, bgm_prepare),
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
	    HLL_EXPORT(Sound_IsExist, wav_exists),
	    HLL_EXPORT(Sound_Prepare, wav_prepare),
	    HLL_EXPORT(Sound_Unprepare, wav_unprepare),
	    HLL_EXPORT(Sound_Play, wav_play),
	    HLL_EXPORT(Sound_Stop, wav_stop),
	    HLL_EXPORT(Sound_IsPlay, wav_is_playing),
	    HLL_EXPORT(Sound_Fade, wav_fade),
	    HLL_EXPORT(Sound_StopFade, wav_stop_fade),
	    HLL_EXPORT(Sound_IsFade, wav_is_fading),
	    HLL_EXPORT(Sound_GetTimeLength, wav_get_time_length),
	    HLL_TODO_EXPORT(Sound_GetGroupNum, KiwiSoundEngine_Sound_GetGroupNum),
	    HLL_EXPORT(Sound_GetGroupNumFromDataNum, wav_get_group_num_from_data_num),
	    HLL_TODO_EXPORT(Sound_PrepareFromFile, KiwiSoundEngine_Sound_PrepareFromFile),
	    HLL_EXPORT(Sound_GetDataLength, wav_get_data_length),
	    HLL_EXPORT(GetMasterGroup, KiwiSoundEngine_GetMasterGroup),
	    HLL_EXPORT(GetBGMGroup, KiwiSoundEngine_GetBGMGroup),
	    HLL_EXPORT(GetSEGroup, KiwiSoundEngine_GetSEGroup),
	    HLL_EXPORT(GetVoiceGroup, KiwiSoundEngine_GetVoiceGroup),
	    HLL_EXPORT(GetGimicSEGroup, KiwiSoundEngine_GetGimicSEGroup),
	    HLL_EXPORT(GetBackVoiceGroup, KiwiSoundEngine_GetBackVoiceGroup),
	    HLL_TODO_EXPORT(GetFreeSeID, KiwiSoundEngine_GetFreeSeID),
	    HLL_EXPORT(IsExistSeID, audio_se_exists),
	    HLL_EXPORT(SetSeParam, audio_se_set_params),
	    HLL_EXPORT(PlaySe, audio_se_play),
	    HLL_EXPORT(StopSe, audio_se_stop),
	    HLL_EXPORT(IsPlaySe, audio_se_is_playing),
	    HLL_TODO_EXPORT(GetFreeID, KiwiSoundEngine_GetFreeID),
	    HLL_EXPORT(IsExistID, audio_id_exists),
	    HLL_EXPORT(Prepare, audio_prepare),
	    HLL_EXPORT(Unprepare, audio_unprepare),
	    HLL_EXPORT(Play, audio_play),
	    HLL_EXPORT(Stop, audio_stop),
	    HLL_EXPORT(IsPlay, audio_is_playing),
	    HLL_EXPORT(SetLoopCount, audio_set_loop_count),
	    HLL_EXPORT(GetLoopCount, audio_get_loop_count),
	    HLL_EXPORT(Pause, audio_pause),
	    HLL_EXPORT(Restart, audio_restart),
	    HLL_EXPORT(IsPause, audio_is_paused),
	    HLL_EXPORT(Fade, audio_fade),
	    HLL_EXPORT(StopFade, audio_stop_fade),
	    HLL_EXPORT(IsFade, audio_is_fading),
	    HLL_EXPORT(Seek, audio_seek),
	    HLL_EXPORT(GetPos, audio_get_pos),
	    HLL_EXPORT(GetLength, audio_get_length),
	    HLL_EXPORT(GetGroupNum, audio_get_group),
	    HLL_EXPORT(IsExistFile, audio_exists_by_name),
	    HLL_EXPORT(GetLengthFromFile, audio_get_length_by_name),
	    HLL_EXPORT(GetGroupNumFromFile, audio_get_group_by_name),
	    HLL_EXPORT(GetGroupVolume, audio_get_group_volume),
	    HLL_EXPORT(MillisecondsToSamples, KiwiSoundEngine_MillisecondsToSamples),
	    HLL_EXPORT(GetSoundFileName, KiwiSoundEngine_GetSoundFileName)
	);

static void KiwiSoundEngine_PreLink(void)
{
	int libno = ain_get_library(ain, "KiwiSoundEngine");
	if (libno < 0)
		return;
	if (ain_get_library_function(ain, libno, "Sound_Seek") >= 0) {
		static_library_replace(&lib_KiwiSoundEngine, "Music_IsExist", rance9_bgm_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Music_Prepare", rance9_bgm_prepare);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_IsExist", rance9_wav_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_Prepare", rance9_wav_prepare);
	}
	if (ain_get_library_function(ain, libno, "Prepare") >= 0)
		audio_meta_load_timemap();
}
