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

// int GetUpdateResult()
hll_unimplemented(SystemServiceEx, GetUpdateResult);
// int GetOverlayPresent()
hll_warn_unimplemented(SystemServiceEx, GetOverlayPresent, 0);
// int GetOverlayActive()
hll_warn_unimplemented(SystemServiceEx, GetOverlayActive, 0);
// void UpdateMainSurface(int nX, int nY, int nWidth, int nHeight)
hll_unimplemented(SystemServiceEx, UpdateMainSurface);
// void SetWaitVSync(int bWaitVSync)
hll_defun_inline(SetWaitVSync, (gfx_set_wait_vsync(a[0].i), 0));
// int GetHasPresented()
hll_unimplemented(SystemServiceEx, GetHasPresented);
// void ClearHasPresented()
hll_unimplemented(SystemServiceEx, ClearHasPresented);

hll_deflib(SystemServiceEx) {
	hll_export(GetUpdateResult),
	hll_export(GetOverlayPresent),
	hll_export(GetOverlayActive),
	hll_export(UpdateMainSurface),
	hll_export(SetWaitVSync),
	hll_export(GetHasPresented),
	hll_export(ClearHasPresented),
	NULL
};

