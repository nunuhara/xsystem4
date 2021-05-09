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

static void IbisInputEngine_Joystick_ClearCaptureFlag(void)
{
	// TODO
}

//int IbisInputEngine_Joystick_GetNumofDevice(void);
HLL_WARN_UNIMPLEMENTED(0, int,  IbisInputEngine, Joystick_GetNumofDevice);

bool IbisInputEngine_Joystick_IsKeyDown(int DeviceNumber, int JoystickCode)
{
	// TODO
	return false;
}

//float IbisInputEngine_Joystick_GetAxis(int DeviceNumber, int AxisType);
HLL_WARN_UNIMPLEMENTED(0.0, float, IbisInputEngine, Joystick_GetAxis, int dev, int axis);

HLL_LIBRARY(IbisInputEngine,
	    HLL_EXPORT(Joystick_ClearCaptureFlag, IbisInputEngine_Joystick_ClearCaptureFlag),
	    HLL_EXPORT(Joystick_GetNumofDevice, IbisInputEngine_Joystick_GetNumofDevice),
	    HLL_EXPORT(Joystick_IsKeyDown, IbisInputEngine_Joystick_IsKeyDown),
	    HLL_EXPORT(Joystick_GetAxis, IbisInputEngine_Joystick_GetAxis));
