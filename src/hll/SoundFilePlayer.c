/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include "hll.h"

//bool SoundFilePlayer_Load(struct string *FileName);
//void SoundFilePlayer_Release(void);
//bool SoundFilePlayer_Play(int PlayCount);
HLL_WARN_UNIMPLEMENTED(false, bool, SoundFilePlayer, Stop, void);
//bool SoundFilePlayer_IsPlay(void);
//int SoundFilePlayer_GetDuration(void);
//int SoundFilePlayer_GetPosition(void);
//bool SoundFilePlayer_BeginFadeVolume(int Volume, int Time);
//bool SoundFilePlayer_StopFadeVolume(void);
//bool SoundFilePlayer_IsFadeVolume(void);
//bool SoundFilePlayer_Update(void);

HLL_LIBRARY(SoundFilePlayer,
	    HLL_TODO_EXPORT(Load, SoundFilePlayer_Load),
	    HLL_TODO_EXPORT(Release, SoundFilePlayer_Release),
	    HLL_TODO_EXPORT(Play, SoundFilePlayer_Play),
	    HLL_EXPORT(Stop, SoundFilePlayer_Stop),
	    HLL_TODO_EXPORT(IsPlay, SoundFilePlayer_IsPlay),
	    HLL_TODO_EXPORT(GetDuration, SoundFilePlayer_GetDuration),
	    HLL_TODO_EXPORT(GetPosition, SoundFilePlayer_GetPosition),
	    HLL_TODO_EXPORT(BeginFadeVolume, SoundFilePlayer_BeginFadeVolume),
	    HLL_TODO_EXPORT(StopFadeVolume, SoundFilePlayer_StopFadeVolume),
	    HLL_TODO_EXPORT(IsFadeVolume, SoundFilePlayer_IsFadeVolume),
	    HLL_TODO_EXPORT(Update, SoundFilePlayer_Update)
	    );
