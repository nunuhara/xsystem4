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

#include "hll.h"

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

static float Math_Cos(float x)
{
	return cosf(deg2rad(x));
}

static float Math_Sin(float x)
{
	return sinf(deg2rad(x));
}

static int Math_Min(int a, int b)
{
	return a < b ? a : b;
}

static float Math_MinF(float a, float b)
{
	return a < b ? a : b;
}

static int Math_Max(int a, int b)
{
	return a > b ? a : b;
}

static float Math_MaxF(float a, float b)
{
	return a > b ? a : b;
}

static void Math_Swap(int *a, int *b)
{
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

static void Math_SwapF(float *a, float *b)
{
	float tmp = *a;
	*a = *b;
	*b = tmp;
}

//void Math_SetRandMode(int mode);
//float Math_RandF(void);
//void Math_RandTableInit(int num, int size);
//int Math_RandTable(int num);
//void Math_RandTable2Init(int num, struct page *array);
//int Math_RandTable2(int num);

HLL_LIBRARY(Math,
	    HLL_EXPORT(Cos, Math_Cos),
	    HLL_EXPORT(Sin, Math_Sin),
	    HLL_EXPORT(Sqrt, sqrtf),
	    HLL_EXPORT(Atan, atanf),
	    HLL_EXPORT(Atan2, atan2f),
	    HLL_EXPORT(Abs, abs),
	    HLL_EXPORT(AbsF, fabsf),
	    HLL_EXPORT(Pow, powf),
	    HLL_EXPORT(SetSeed, srand),
	    //HLL_EXPORT(SetRandMode, Math_SetRandMode),
	    HLL_EXPORT(Rand, rand),
	    //HLL_EXPORT(RandF, Math_RandF),
	    //HLL_EXPORT(RandTableInit, Math_RandTableInit),
	    //HLL_EXPORT(RandTable, Math_RandTable),
	    //HLL_EXPORT(RandTable2Init, Math_RandTable2Init),
	    //HLL_EXPORT(RandTable2, Math_RandTable2),
	    HLL_EXPORT(Min, Math_Min),
	    HLL_EXPORT(MinF, Math_MinF),
	    HLL_EXPORT(Max, Math_Max),
	    HLL_EXPORT(MaxF, Math_MaxF),
	    HLL_EXPORT(Swap, Math_Swap),
	    HLL_EXPORT(SwapF, Math_SwapF));

