/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

static int max_channel;
static int *current;

static void vmMusic_Init(int max_channel_, possibly_unused void *imainsystem)
{
	audio_init();
	max_channel = max_channel_;
	current = xcalloc(max_channel, sizeof(int));
}

static int vmMusic_Prepare(int channel, int track)
{
	if (channel < 0 || channel >= max_channel)
		return 0;
	if (current[channel] == track)
		return 1;  // Keep playing current music.
	int r = bgm_prepare(channel, track);
	if (r)
		current[channel] = track;
	return r;
}

static int vmMusic_Play(int channel)
{
	return bgm_play(channel);
}

static int vmMusic_Stop(int channel)
{
	return bgm_stop(channel);
}

static int vmMusic_IsPlay(int channel)
{
	return bgm_is_playing(channel);
}

//int vmMusic_SetLoop(int channel, int nLoop);
//int vmMusic_GetLoop(int channel);

static int vmMusic_Fade(int channel, int time, int volume, int stop)
{
	return bgm_fade(channel, time, volume, stop);
}

//int vmMusic_StopFade(int channel);
//int vmMusic_IsFade(int channel);
//int vmMusic_GetPos(int channel);
//int vmMusic_GetLength(int channel);

HLL_LIBRARY(vmMusic,
	    HLL_EXPORT(Init, vmMusic_Init),
	    HLL_EXPORT(Prepare, vmMusic_Prepare),
	    HLL_EXPORT(Play, vmMusic_Play),
	    HLL_EXPORT(Stop, vmMusic_Stop),
	    HLL_EXPORT(IsPlay, vmMusic_IsPlay),
	    HLL_TODO_EXPORT(SetLoop, vmMusic_SetLoop),
	    HLL_TODO_EXPORT(GetLoop, vmMusic_GetLoop),
	    HLL_EXPORT(Fade, vmMusic_Fade),
	    HLL_TODO_EXPORT(StopFade, vmMusic_StopFade),
	    HLL_TODO_EXPORT(IsFade, vmMusic_IsFade),
	    HLL_TODO_EXPORT(GetPos, vmMusic_GetPos),
	    HLL_TODO_EXPORT(GetLength, vmMusic_GetLength)
	    );
