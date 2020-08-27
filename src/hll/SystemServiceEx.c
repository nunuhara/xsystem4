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
#include "gfx/gfx.h"

static int SystemServiceEx_GetOverlayPresent(void)
{
	return 0;
}

//int SystemServiceEx_GetUpdateResult(void);
HLL_WARN_UNIMPLEMENTED(0, int, SystemServiceEx, GetOverlayActive, void);
//void SystemServiceEx_UpdateMainSurface(int x, int y, int w, int h);
//int SystemServiceEx_GetHasPresented(void);
//void SystemServiceEx_ClearHasPresented(void);

HLL_LIBRARY(SystemServiceEx,
	    //HLL_EXPORT(GetUpdateResult, SystemServiceEx_GetUpdateResult),
	    HLL_EXPORT(GetOverlayPresent, SystemServiceEx_GetOverlayPresent),
	    HLL_EXPORT(GetOverlayActive, SystemServiceEx_GetOverlayActive),
	    //HLL_EXPORT(UpdateMainSurface, SystemServiceEx_UpdateMainSurface),
	    HLL_EXPORT(SetWaitVSync, gfx_set_wait_vsync)
	    //HLL_EXPORT(GetHasPresented, SystemServiceEx_GetHasPresented),
	    //HLL_EXPORT(ClearHasPresented, SystemServiceEx_ClearHasPresented)
	);
