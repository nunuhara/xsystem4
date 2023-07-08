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
#include <time.h>
#include <cglm/cglm.h>

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#include "hll.h"
#include "vm/page.h"

struct shuffle_table {
	int id;
	struct shuffle_table *next;
	int *elems;
	int size;
	int i;
};
static struct shuffle_table *shuffle_tables;

static struct shuffle_table *find_shuffle_table(int id)
{
	for (struct shuffle_table *tbl = shuffle_tables; tbl; tbl = tbl->next) {
		if (tbl->id == id)
			return tbl;
	}
	return NULL;
}

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

static float Math_Tan(float x)
{
	return tanf(deg2rad(x));
}

static void Math_SetSeedByCurrentTime(void)
{
	srand(time(NULL));
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

static float Math_RandF(void)
{
	return rand() * (1.0 / (RAND_MAX + 1U));
}

static void shuffle_array(int *a, int len)
{
	for (int i = len - 1; i > 0; i--) {
		int j = Math_RandF() * i;
		int tmp = a[j];
		a[j] = a[i];
		a[i] = tmp;
	}
}

static void Math_RandTableInit(int num, int size)
{
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (tbl) {
		free(tbl->elems);
	} else {
		tbl = xmalloc(sizeof(struct shuffle_table));
		tbl->id = num;
		tbl->next = shuffle_tables;
		shuffle_tables = tbl;
	}
	tbl->elems = xmalloc(size * sizeof(int));
	tbl->size = size;
	tbl->i = 0;
	for (int i = 0; i < size; i++)
		tbl->elems[i] = i;
	shuffle_array(tbl->elems, size);
}

static int Math_RandTable(int num)
{
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (!tbl) {
		WARNING("Invalid rand table id %d", num);
		return 0;
	}
	if (tbl->i >= tbl->size) {
		// reshuffle
		shuffle_array(tbl->elems, tbl->size);
		tbl->i = 0;
	}
	return tbl->elems[tbl->i++];
}

//void Math_RandTable2Init(int num, struct page *array);
//int Math_RandTable2(int num);

static int Math_Ceil(float f)
{
	return (int)ceilf(f);
}

static bool Math_BezierCurve(struct page **x_array, struct page **y_array, int num, float t, int *result_x, int *result_y)
{
	vec2 *coeffs = xmalloc(num * sizeof(vec2));
	for (int i = 0; i < num; i++) {
		coeffs[i][0] = (*x_array)->values[i].i;
		coeffs[i][1] = (*y_array)->values[i].i;
	}

	// De Casteljau's algorithm.
	for (; num > 1; num--) {
		for (int i = 0; i < num - 1; i++)
			glm_vec2_lerp(coeffs[i], coeffs[i + 1], t, coeffs[i]);
	}
	*result_x = coeffs[0][0];
	*result_y = coeffs[0][1];

	free(coeffs);
	return true;
}

HLL_LIBRARY(Math,
	    HLL_EXPORT(Cos, Math_Cos),
	    HLL_EXPORT(Sin, Math_Sin),
	    HLL_EXPORT(Tan, Math_Tan),
	    HLL_EXPORT(Sqrt, sqrtf),
	    HLL_EXPORT(Atan, atanf),
	    HLL_EXPORT(Atan2, atan2f),
	    HLL_EXPORT(Abs, abs),
	    HLL_EXPORT(AbsF, fabsf),
	    HLL_EXPORT(Pow, powf),
	    HLL_EXPORT(SetSeed, srand),
	    HLL_EXPORT(SetSeedByCurrentTime, Math_SetSeedByCurrentTime),
	    //HLL_EXPORT(SetRandMode, Math_SetRandMode),
	    HLL_EXPORT(Rand, rand),
	    HLL_EXPORT(RandF, Math_RandF),
	    HLL_EXPORT(RandTableInit, Math_RandTableInit),
	    HLL_EXPORT(RandTable, Math_RandTable),
	    //HLL_EXPORT(RandTable2Init, Math_RandTable2Init),
	    //HLL_EXPORT(RandTable2, Math_RandTable2),
	    HLL_EXPORT(Min, Math_Min),
	    HLL_EXPORT(MinF, Math_MinF),
	    HLL_EXPORT(Max, Math_Max),
	    HLL_EXPORT(MaxF, Math_MaxF),
	    HLL_EXPORT(Swap, Math_Swap),
	    HLL_EXPORT(SwapF, Math_SwapF),
	    HLL_EXPORT(Log, logf),
	    HLL_EXPORT(Log10, log10f),
	    HLL_EXPORT(Ceil, Math_Ceil),
	    HLL_EXPORT(BezierCurve, Math_BezierCurve));

