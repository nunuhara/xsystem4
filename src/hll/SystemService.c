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

#include <string.h>

#include "system4/string.h"

#include "xsystem4.h"
#include "hll.h"

static int SystemService_GetMixerNumof(void)
{
	return config.mixer_nr_channels;
}

static int SystemService_GetMixerName(int n, struct string **name)
{
	if (n < 0 || (size_t)n >= config.mixer_nr_channels)
		return 0;
	*name = make_string(config.mixer_channels[n], strlen(config.mixer_channels[n]));
	return 1;
}

static int SystemService_GetMixerVolume(int n, int *volume)
{
	if (n < 0 || (size_t)n >= config.mixer_nr_channels)
		return 0;
	*volume = 100;
	return 1;
}

static int SystemService_GetMixerMute(int n, int *mute)
{
	if (n < 0 || (size_t)n >= config.mixer_nr_channels)
		return 0;
	*mute = 0;
	return true;
}

//bool SystemService_SetMixerName(int nNum, string szName);

static int SystemService_SetMixerVolume(int n, possibly_unused int volume)
{
	if (n < 0 || (size_t)n >= config.mixer_nr_channels)
		return 0;
	// TODO
	return 1;
}

static int SystemService_SetMixerMute(int n, possibly_unused int mute)
{
	if (n < 0 || (size_t)n >= config.mixer_nr_channels)
		return 0;
	// TODO
	return 1;
}

//int SystemService_GetGameVersion(void);
//bool SystemService_AddURLMenu(string szTitle, string szURL);
static bool SystemService_IsFullScreen(void)
{
	return false;
}
//bool SystemService_ChangeNormalScreen(void);
//bool SystemService_ChangeFullScreen(void);
static void SystemService_RestrainScreensaver(void) { }

HLL_LIBRARY(SystemService,
	    HLL_EXPORT(GetMixerNumof, SystemService_GetMixerNumof),
	    HLL_EXPORT(GetMixerName, SystemService_GetMixerName),
	    HLL_EXPORT(GetMixerVolume, SystemService_GetMixerVolume),
	    HLL_EXPORT(GetMixerMute, SystemService_GetMixerMute),
	    //HLL_EXPORT(SetMixerName, SystemService_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, SystemService_SetMixerVolume),
	    HLL_EXPORT(SetMixerMute, SystemService_SetMixerMute),
	    //HLL_EXPORT(GetGameVersion, SystemService_GetGameVersion),
	    //HLL_EXPORT(AddURLMenu, SystemService_AddURLMenu),
	    HLL_EXPORT(IsFullScreen, SystemService_IsFullScreen),
	    //HLL_EXPORT(ChangeNormalScreen, SystemService_ChangeNormalScreen),
	    //HLL_EXPORT(ChangeFullScreen, SystemService_ChangeFullScreen),
	    HLL_EXPORT(RestrainScreensaver, SystemService_RestrainScreensaver)
	);
