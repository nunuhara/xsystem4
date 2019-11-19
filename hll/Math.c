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

#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#include "../system4.h"
#include "hll.h"

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

hll_defun(Cos, args)
{
	hll_return(cosf(deg2rad(args[0].f)));
}

hll_defun(Sin, args)
{
	hll_return(sinf(deg2rad(args[0].f)));
}

hll_defun(Sqrt, args)
{
	hll_return(sqrtf(args[0].f));
}

hll_defun(Atan, args)
{
	hll_return(atanf(args[0].f));
}

hll_defun(Atan2, args)
{
	hll_return(atan2f(args[0].f, args[1].f));
}

hll_defun(Abs, args)
{
	hll_return(abs(args[0].i));
}

hll_defun(AbsF, args)
{
	hll_return(fabsf(args[0].f));
}

hll_defun(Pow, args)
{
	hll_return(powf(args[0].f, args[1].f));
}

hll_defun(SetSeed, args)
{
	srand(args[0].i);
	hll_return(0);
}

hll_unimplemented(Math, SetRandMode)

hll_defun(Rand, args)
{
	hll_return(rand());
}

// TODO
hll_unimplemented(Math, RandF)
hll_unimplemented(Math, RandTableInit)
hll_unimplemented(Math, RandTable)
hll_unimplemented(Math, RandTable2Init)
hll_unimplemented(Math, RandTable2)

hll_defun(Min, args)
{
	int a = args[0].i, b = args[1].i;
	hll_return(a < b ? a : b);
}

hll_defun(MinF, args)
{
	float a = args[0].f, b = args[1].f;
	hll_return(a < b ? a : b);
}

hll_defun(Max, args)
{
	int a = args[0].i, b = args[1].i;
	hll_return(a > b ? a : b);
}

hll_defun(MaxF, args)
{
	float a = args[0].f, b = args[1].f;
	hll_return(a > b ? a : b);
}

hll_defun(Swap, args)
{
	int tmp = *args[0].iref;
	*args[0].iref = *args[1].iref;
	*args[1].iref = tmp;
	hll_return(0);
}

hll_defun(SwapF, args)
{
	float tmp = *args[0].fref;
	*args[0].fref = *args[1].fref;
	*args[1].fref = tmp;
	hll_return(0);
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
