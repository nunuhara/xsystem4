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

#include "hll.h"
#include "input.h"
#include "sact.h"

bool IbisInputEngine_Mouse_MovePosImmediately(int x, int y)
{
	mouse_set_pos(x, y);
	return true;
}

static bool hide_cursor_by_game = false;
static bool IbisInputEngine_Mouse_IsHideCursorByGame(void) { return hide_cursor_by_game; }
bool IbisInputEngine_Mouse_HideCursorByGame(bool hide)
{
	hide_cursor_by_game = hide;
	return mouse_show_cursor(!hide);
}

//bool Mouse_IsHideCursorByGame(void);
//void Mouse_HideByStepMessage(void);
HLL_QUIET_UNIMPLEMENTED(, void, IbisInputEngine, Mouse_HideByStepMessage);

static void IbisInputEngine_Joystick_ClearCaptureFlag(void)
{
	joy_clear_flag();
}

static int IbisInputEngine_Joystick_GetNumofDevice(void)
{
	handle_events();
	return joy_get_num_controllers();
}

bool IbisInputEngine_Joystick_IsKeyDown(int DeviceNumber, int JoystickCode)
{
	handle_events();
	return joy_is_button_down(DeviceNumber, JoystickCode);
}

static float IbisInputEngine_Joystick_GetAxis(int DeviceNumber, int AxisType)
{
	handle_events();
	return joy_get_axis_value(DeviceNumber, AxisType);
}

HLL_LIBRARY(IbisInputEngine,
	    HLL_EXPORT(Mouse_GetPos, sact_Mouse_GetPos),
	    HLL_EXPORT(Mouse_MovePosImmediately, IbisInputEngine_Mouse_MovePosImmediately),
	    HLL_EXPORT(Mouse_HideCursorByGame, IbisInputEngine_Mouse_HideCursorByGame),
	    HLL_EXPORT(Mouse_IsHideCursorByGame, IbisInputEngine_Mouse_IsHideCursorByGame),
	    HLL_EXPORT(Mouse_HideByStepMessage, IbisInputEngine_Mouse_HideByStepMessage),
	    HLL_EXPORT(MouseWheel_ClearCount, mouse_clear_wheel),
	    HLL_EXPORT(MouseWheel_GetCount, mouse_get_wheel),
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown),
	    HLL_EXPORT(Joystick_ClearCaptureFlag, IbisInputEngine_Joystick_ClearCaptureFlag),
	    HLL_EXPORT(Joystick_GetNumofDevice, IbisInputEngine_Joystick_GetNumofDevice),
	    HLL_EXPORT(Joystick_IsKeyDown, IbisInputEngine_Joystick_IsKeyDown),
	    HLL_EXPORT(Joystick_GetAxis, IbisInputEngine_Joystick_GetAxis));
