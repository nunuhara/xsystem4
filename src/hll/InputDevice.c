/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4/string.h"

#include "hll.h"
#include "input.h"
#include "vm.h"

static void handle_events_throttled(void)
{
	// Big Bang Age uses busy loops to wait input events. To prevent taking 100%
	// CPU time, sleep 10ms every 30 input event checks.
	static int count;
	if (++count == 30) {
		count = 0;
		vm_sleep(10);
	}
	handle_events();
}

HLL_WARN_UNIMPLEMENTED( , void, InputDevice, Init, void *imainsystem);

void InputDevice_ClearKeyDownFlag(void)
{
	key_clear_flag(false);
}

static int InputDevice_IsKeyDown(int key)
{
	handle_events_throttled();
	return key_is_down(key);
}

static int InputDevice_GetCsrPos(int *x, int *y)
{
	handle_events_throttled();
	mouse_get_pos(x, y);
	return mouse_focus && keyboard_focus;
}

static int InputDevice_SetCsrPos(int x, int y)
{
	handle_events_throttled();
	mouse_set_pos(x, y);
	return mouse_focus && keyboard_focus;
}

static void InputDevice_ClearWheelCount(void)
{
	mouse_clear_wheel();
}

static int InputDevice_GetWheelCountForward(void)
{
	int forward, back;
	handle_events_throttled();
	mouse_get_wheel(&forward, &back);
	return forward;
}

static int InputDevice_GetWheelCountBack(void)
{
	int forward, back;
	handle_events_throttled();
	mouse_get_wheel(&forward, &back);
	return back;
}

HLL_LIBRARY(InputDevice,
			HLL_EXPORT(Init, InputDevice_Init),
			HLL_EXPORT(ClearKeyDownFlag, InputDevice_ClearKeyDownFlag),
			HLL_EXPORT(IsKeyDown, InputDevice_IsKeyDown),
			HLL_EXPORT(GetCsrPos, InputDevice_GetCsrPos),
			HLL_EXPORT(SetCsrPos, InputDevice_SetCsrPos),
			HLL_EXPORT(ClearWheelCount, InputDevice_ClearWheelCount),
			HLL_EXPORT(GetWheelCountForward, InputDevice_GetWheelCountForward),
			HLL_EXPORT(GetWheelCountBack, InputDevice_GetWheelCountBack));
