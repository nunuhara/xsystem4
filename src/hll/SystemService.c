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

#include "mixer.h"
#include "xsystem4.h"
#include "hll.h"

static int SystemService_GetMixerName(int n, struct string **name)
{
	const char *r = mixer_get_name(n);
	if (!r)
		return 0;
	*name = make_string(r, strlen(r));
	return 1;
}

static bool SystemService_GetMixerDefaultVolume(int n, int *volume)
{
	if (n < 0 || (unsigned)n >= config.mixer_nr_channels)
		return false;
	*volume = config.mixer_volumes[n];
	return true;
}

//bool SystemService_SetMixerName(int nNum, string szName);

//int SystemService_GetGameVersion(void);
static void SystemService_GetGameName(struct string **game_name)
{
	if (*game_name)
		free_string(*game_name);
	*game_name = cstr_to_string(config.game_name);
}

//bool SystemService_AddURLMenu(string szTitle, string szURL);

static bool SystemService_IsFullScreen(void)
{
	return false;
}

//bool SystemService_ChangeNormalScreen(void);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, ChangeFullScreen);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, InitMainWindowPosAndSize);

//static bool SystemService_UpdateView(void);
static int SystemService_GetViewWidth(void)
{
	return config.view_width;
}

static int SystemService_GetViewHeight(void)
{
	return config.view_height;
}

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

static void SystemService_GetTime(int *hour, int *min, int *sec)
{
	int ms;
	get_time(hour, min, sec, &ms);
}

//static bool SystemService_IsResetOnce(void);
//static void SystemService_OpenPlayingManual(void);
//static bool SystemService_IsExistPlayingManual(void);
//static bool SystemService_IsExistSystemMessage(void);
//static bool SystemService_PopSystemMessage(int *message);

static void SystemService_RestrainScreensaver(void) { }

//static int SystemService_Debug_GetUseVideoMemorySize(void);

static void SystemService_Rance0123456789(struct string **text)
{
	*text = cstr_to_string("-RANCE010ECNAR-"); // ???
}

HLL_LIBRARY(SystemService,
	    HLL_EXPORT(GetMixerNumof, mixer_get_numof),
	    HLL_EXPORT(GetMixerName, SystemService_GetMixerName),
	    HLL_EXPORT(GetMixerVolume, mixer_get_volume),
	    HLL_EXPORT(GetMixerDefaultVolume, SystemService_GetMixerDefaultVolume),
	    HLL_EXPORT(GetMixerMute, mixer_get_mute),
	    HLL_TODO_EXPORT(SetMixerName, SystemService_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, mixer_set_volume),
	    HLL_EXPORT(SetMixerMute, mixer_set_mute),
	    HLL_TODO_EXPORT(GetGameVersion, SystemService_GetGameVersion),
	    HLL_EXPORT(GetGameName, SystemService_GetGameName),
	    HLL_TODO_EXPORT(AddURLMenu, SystemService_AddURLMenu),
	    HLL_EXPORT(IsFullScreen, SystemService_IsFullScreen),
	    HLL_TODO_EXPORT(ChangeNormalScreen, SystemService_ChangeNormalScreen),
	    HLL_EXPORT(ChangeFullScreen, SystemService_ChangeFullScreen),
	    HLL_EXPORT(InitMainWindowPosAndSize, SystemService_InitMainWindowPosAndSize),
	    HLL_TODO_EXPORT(UpdateView, SystemService_UpdateView),
	    HLL_EXPORT(GetViewWidth, SystemService_GetViewWidth),
	    HLL_EXPORT(GetViewHeight, SystemService_GetViewHeight),
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
	    HLL_EXPORT(GetDate, get_date),
	    HLL_EXPORT(GetTime, SystemService_GetTime),
	    HLL_TODO_EXPORT(IsResetOnce, SystemService_IsResetOnce),
	    HLL_TODO_EXPORT(OpenPlayingManual, SystemService_OpenPlayingManual),
	    HLL_TODO_EXPORT(IsExistPlayingManual, SystemService_IsExistPlayingManual),
	    HLL_TODO_EXPORT(IsExistSystemMessage, SystemService_IsExistSystemMessage),
	    HLL_TODO_EXPORT(PopSystemMessage, SystemService_PopSystemMessage),
	    HLL_EXPORT(RestrainScreensaver, SystemService_RestrainScreensaver),
	    HLL_TODO_EXPORT(Debug_GetUseVideoMemorySize, SystemService_Debug_GetUseVideoMemorySize),
	    HLL_EXPORT(Rance0123456789, SystemService_Rance0123456789)
	);
