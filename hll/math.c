/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * Credit to SLC for reverse engineering AIN formats.
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

#include <stdlib.h>
#include <math.h>

#include "../system4.h"
#include "hll.h"

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

hll_defun(Cos) {
	hll_return(cosf(deg2rad(hll_arg(0).f)));
}

hll_defun(Sin) {
	hll_return(sinf(deg2rad(hll_arg(0).f)));
}

hll_defun(Sqrt) {
	hll_return(sqrtf(hll_arg(0).f));
}

hll_defun(Atan) {
	hll_return(atanf(hll_arg(0).f));
}

hll_defun(Atan2) {
	hll_return(atan2f(hll_arg(0).f, hll_arg(1).f));
}

hll_defun(Abs) {
	hll_return(abs(hll_arg(0).i));
}

hll_defun(AbsF) {
	hll_return(fabsf(hll_arg(0).f));
}

hll_defun(Pow) {
	hll_return(powf(hll_arg(0).f, hll_arg(1).f));
}

hll_defun(SetSeed) {
	srand(hll_arg(0).i);
	hll_return(0);
}

hll_unimplemented(SetRandMode)

hll_defun(Rand) {
	hll_return(rand());
}

// TODO
hll_unimplemented(RandF)
hll_unimplemented(RandTableInit)
hll_unimplemented(RandTable)
hll_unimplemented(RandTable2Init)
hll_unimplemented(RandTable2)

hll_defun(Min) {
	int a = hll_arg(0).i, b = hll_arg(1).i;
	hll_return(a < b ? a : b);
}

hll_defun(MinF) {
	float a = hll_arg(0).f, b = hll_arg(1).f;
	hll_return(a < b ? a : b);
}

hll_defun(Max) {
	int a = hll_arg(0).i, b = hll_arg(1).i;
	hll_return(a > b ? a : b);
}

hll_defun(MaxF) {
	float a = hll_arg(0).f, b = hll_arg(1).f;
	hll_return(a > b ? a : b);
}

hll_defun(Swap) {
	int tmp = *hll_arg(0).iref;
	*hll_arg(0).iref = *hll_arg(1).iref;
	*hll_arg(1).iref = tmp;
}

hll_defun(SwapF) {
	float tmp = *hll_arg(0).fref;
	*hll_arg(0).fref = *hll_arg(1).fref;
	*hll_arg(1).fref = tmp;
}

hll_deflib(Math) {
	hll_export(Cos),
	hll_export(Sin),
	hll_export(Sqrt),
	hll_export(Atan),
	hll_export(Atan2),
	hll_export(Abs),
	hll_export(AbsF),
	hll_export(Pow),
	hll_export(SetSeed),
	hll_export(SetRandMode),
	hll_export(Rand),
	hll_export(RandF),
	hll_export(RandTableInit),
	hll_export(RandTable),
	hll_export(RandTable2Init),
	hll_export(RandTable2),
	hll_export(Min),
	hll_export(MinF),
	hll_export(Max),
	hll_export(MaxF),
	hll_export(Swap),
	hll_export(SwapF),
	NULL
};
