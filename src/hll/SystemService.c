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
//void SystemService_GetGameName(ref string GameName);
//bool SystemService_AddURLMenu(string szTitle, string szURL);

static bool SystemService_IsFullScreen(void)
{
	return false;
}

//bool SystemService_ChangeNormalScreen(void);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, ChangeFullScreen);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, InitMainWindowPosAndSize);
//bool SystemService_MoveMouseCursorPosImmediately(int nX, int nY);
//bool SystemService_SetHideMouseCursorByGame(bool bHide);
//bool SystemService_GetHideMouseCursorByGame(void);
//bool SystemService_SetUsePower2Texture(bool bUse);
//bool SystemService_GetUsePower2Texture(void);

// XXX: 5 settings in Haru Urare, may be different in other games
#define NR_WINDOW_SETTINGS 5
static int window_settings[NR_WINDOW_SETTINGS] = {
	// defaults based on Haru Urare
	0,
	0,
	0,
	0,
	1,
};

static int get_window_setting(int type)
{
	// XXX: only type 1 is non-boolean (Haru Urare)
	if (type == 1)
		return window_settings[1];
	return !!window_settings[type];
}

static bool SystemService_SetWindowSetting(int type, int value)
{
	if (type < 0 || type >= NR_WINDOW_SETTINGS) {
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	window_settings[type] = value;
	return true;
}

static bool SystemService_GetWindowSetting(int type, int *value)
{
	if (type < 0 || type >= NR_WINDOW_SETTINGS) {
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	*value = get_window_setting(type);
	return true;
}

// XXX: Values for 'type' above 1 are invalid in Haru Urare, may be different in other games
#define NR_MOUSE_CURSOR_CONFIG 1
static int mouse_cursor_config[NR_MOUSE_CURSOR_CONFIG] = {0};

static bool SystemService_SetMouseCursorConfig(int type, int value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	mouse_cursor_config[type] = value;
	return true;
}

static bool SystemService_GetMouseCursorConfig(int type, int *value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	// XXX: the value returned here is always 1 or 0
	*value = !!mouse_cursor_config[type];
	return true;
}

//bool SystemService_RunProgram(struct string *program_file_name, struct string *parameter);
//bool SystemService_IsOpenedMutex(struct string *mutex_name);
//void SystemService_GetGameFolderPath(struct string **folder_path);

static void SystemService_RestrainScreensaver(void) { }

HLL_LIBRARY(SystemService,
	    HLL_EXPORT(GetMixerNumof, SystemService_GetMixerNumof),
	    HLL_EXPORT(GetMixerName, SystemService_GetMixerName),
	    HLL_EXPORT(GetMixerVolume, SystemService_GetMixerVolume),
	    HLL_EXPORT(GetMixerMute, SystemService_GetMixerMute),
	    HLL_TODO_EXPORT(SetMixerName, SystemService_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, SystemService_SetMixerVolume),
	    HLL_EXPORT(SetMixerMute, SystemService_SetMixerMute),
	    HLL_TODO_EXPORT(GetGameVersion, SystemService_GetGameVersion),
	    HLL_TODO_EXPORT(GetGameName, SystemService_GetGameName),
	    HLL_TODO_EXPORT(AddURLMenu, SystemService_AddURLMenu),
	    HLL_EXPORT(IsFullScreen, SystemService_IsFullScreen),
	    HLL_TODO_EXPORT(ChangeNormalScreen, SystemService_ChangeNormalScreen),
	    HLL_EXPORT(ChangeFullScreen, SystemService_ChangeFullScreen),
	    HLL_EXPORT(InitMainWindowPosAndSize, SystemService_InitMainWindowPosAndSize),
	    HLL_TODO_EXPORT(MoveMouseCursorPosImmediately, SystemService_MoveMouseCursorPosImmediately),
	    HLL_TODO_EXPORT(SetHideMouseCursorByGame, SystemService_SetHideMouseCursorByGame),
	    HLL_TODO_EXPORT(GetHideMouseCursorByGame, SystemService_GetHideMouseCursorByGame),
	    HLL_TODO_EXPORT(SetUsePower2Texture, SystemService_SetUsePower2Texture),
	    HLL_TODO_EXPORT(GetUsePower2Texture, SystemService_GetUsePower2Texture),
	    HLL_EXPORT(SetWindowSetting, SystemService_SetWindowSetting),
	    HLL_EXPORT(GetWindowSetting, SystemService_GetWindowSetting),
	    HLL_EXPORT(SetMouseCursorConfig, SystemService_SetMouseCursorConfig),
	    HLL_EXPORT(GetMouseCursorConfig, SystemService_GetMouseCursorConfig),
	    HLL_TODO_EXPORT(RunProgram, SystemService_RunProgram),
	    HLL_TODO_EXPORT(IsOpenedMutex, SystemService_IsOpenedMutex),
	    HLL_TODO_EXPORT(GetGameFolderPath, SystemService_GetGameFolderPath),
	    HLL_EXPORT(RestrainScreensaver, SystemService_RestrainScreensaver)
	);
