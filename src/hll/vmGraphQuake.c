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

#include "hll.h"

HLL_WARN_UNIMPLEMENTED(, void, vmGraphQuake, SetMode, int mode);
//void vmGraphQuake_SetAmp(int nAmp);
HLL_WARN_UNIMPLEMENTED(, void, vmGraphQuake, SetAmpX, int amp_x);
HLL_WARN_UNIMPLEMENTED(, void, vmGraphQuake, SetAmpY, int amp_y);
//void vmGraphQuake_SetCycle(int nCycle);
//int vmGraphQuake_GetMode(void);
//int vmGraphQuake_GetAmp(void);
//int vmGraphQuake_GetAmpX(void);
//int vmGraphQuake_GetAmpY(void);
//int vmGraphQuake_GetCycle(void);

HLL_LIBRARY(vmGraphQuake,
	    HLL_EXPORT(SetMode, vmGraphQuake_SetMode),
	    HLL_TODO_EXPORT(SetAmp, vmGraphQuake_SetAmp),
	    HLL_EXPORT(SetAmpX, vmGraphQuake_SetAmpX),
	    HLL_EXPORT(SetAmpY, vmGraphQuake_SetAmpY),
	    HLL_TODO_EXPORT(SetCycle, vmGraphQuake_SetCycle),
	    HLL_TODO_EXPORT(GetMode, vmGraphQuake_GetMode),
	    HLL_TODO_EXPORT(GetAmp, vmGraphQuake_GetAmp),
	    HLL_TODO_EXPORT(GetAmpX, vmGraphQuake_GetAmpX),
	    HLL_TODO_EXPORT(GetAmpY, vmGraphQuake_GetAmpY),
	    HLL_TODO_EXPORT(GetCycle, vmGraphQuake_GetCycle)
	    );
