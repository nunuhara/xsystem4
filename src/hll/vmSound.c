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

#include "audio.h"
#include "hll.h"
#include "ChrLoader.h"

static int ring_pos = 0;

static void vmSound_Init(possibly_unused void *imainsystem)
{
	audio_init();
}

//void vmSound_Enable(int nEnable);

HLL_QUIET_UNIMPLEMENTED(, void, vmSound, SetType, int type);

static int vmSound_Load(int channel, int num)
{
	return wav_prepare(channel, num);
}

static int vmSound_LoadMemory(int channel, int memory)
{
	struct archive_data *dfile = chr_loader_get_blob_as_archive_data(memory);
	if (!dfile)
		return 0;
	return wav_prepare_from_archive_data(channel, dfile);
}

static void vmSound_Unload(int channel, int length)
{
	for (int i = 0; i < length; i++)
		wav_unprepare(channel + i);
}

static int vmSound_Play(int channel, possibly_unused int length)
{
	wav_stop(channel);
	return wav_play(channel);
}

static int vmSound_RingPlay(int channel, int ring)
{
	channel += ring_pos++ % ring;
	wav_stop(channel);
	return wav_play(channel);
}

//int vmSound_Stop(int channel, int nLength);

static int vmSound_IsPlay(int channel, int length)
{
	for (int i = 0; i < length; i++) {
		if (wav_is_playing(channel + i))
			return 1;
	}
	return 0;
}

//void vmSound_Wait(int channel, int nLength);
//int vmSound_Fade(int channel, int nLength, int nTime, int nVolume, int nStopFlag);
//int vmSound_StopFade(int channel, int nLength);
//int vmSound_IsFade(int channel, int nLength);
//int vmSound_ReverseLR(int channel, int nLength);

HLL_LIBRARY(vmSound,
	    HLL_EXPORT(Init, vmSound_Init),
	    HLL_TODO_EXPORT(Enable, vmSound_Enable),
	    HLL_EXPORT(SetType, vmSound_SetType),
	    HLL_EXPORT(Load, vmSound_Load),
	    HLL_EXPORT(LoadMemory, vmSound_LoadMemory),
	    HLL_EXPORT(Unload, vmSound_Unload),
	    HLL_EXPORT(Play, vmSound_Play),
	    HLL_EXPORT(RingPlay, vmSound_RingPlay),
	    HLL_TODO_EXPORT(Stop, vmSound_Stop),
	    HLL_EXPORT(IsPlay, vmSound_IsPlay),
	    HLL_TODO_EXPORT(Wait, vmSound_Wait),
	    HLL_TODO_EXPORT(Fade, vmSound_Fade),
	    HLL_TODO_EXPORT(StopFade, vmSound_StopFade),
	    HLL_TODO_EXPORT(IsFade, vmSound_IsFade),
	    HLL_TODO_EXPORT(ReverseLR, vmSound_ReverseLR)
	    );
