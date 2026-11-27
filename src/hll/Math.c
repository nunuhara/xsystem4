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
#include <string.h>
#include <time.h>
#include <cglm/cglm.h>

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#include "system4/mt19937int.h"
#include "system4/ain.h"

#include "hll.h"
#include "vm/page.h"

static struct mt19937 mt;
static bool mt_initialized;

static uint32_t math_genrand(void)
{
	if (!mt_initialized) {
		mt19937_init(&mt, 4357);
		mt_initialized = true;
	}
	return mt19937_genrand(&mt);
}

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

static void Math_SetSeed(int seed)
{
	mt19937_init(&mt, (uint32_t)seed | 1);
	mt19937_genrand(&mt);  // the original engine discards the first value
	mt_initialized = true;
}

static void Math_SetSeedByCurrentTime(void)
{
	Math_SetSeed(time(NULL));
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

#define MATH_MINMAX_MORE(name, type) \
	static type name##3(type a, type b, type c) { return name(name(a, b), c); } \
	static type name##4(type a, type b, type c, type d) { return name(name(a, b), name(c, d)); }
MATH_MINMAX_MORE(Math_Min, int)
MATH_MINMAX_MORE(Math_MinF, float)
MATH_MINMAX_MORE(Math_Max, int)
MATH_MINMAX_MORE(Math_MaxF, float)
#undef MATH_MINMAX_MORE

static int Math_Clamp(int value, int low, int high)
{
	return Math_Min(Math_Max(value, low), high);
}

static float Math_ClampF(float value, float low, float high)
{
	return Math_MinF(Math_MaxF(value, low), high);
}

static int Math_AbsF_int(float x)
{
	return (int)fabsf(x);
}

static void *Math_ResolveAbsF(const struct ain_hll_function *f)
{
	if (f->nr_arguments != 1)
		return NULL;
	if (f->return_type.data == AIN_INT) {
		if (f->arguments[0].type.data == AIN_FLOAT)
			return (void *)Math_AbsF_int;
		if (f->arguments[0].type.data == AIN_INT)
			return (void *)abs;
		return NULL;
	}
	if (f->return_type.data == AIN_FLOAT && f->arguments[0].type.data == AIN_FLOAT)
		return (void *)fabsf;
	return NULL;
}

static void *Math_Resolve(const struct ain_hll_function *f)
{
	enum ain_data_type type = f->return_type.data;
	if (type != AIN_INT && type != AIN_FLOAT) return NULL;
	for (int i = 0; i < f->nr_arguments; i++)
		if (f->arguments[i].type.data != type) return NULL;
	bool floating = type == AIN_FLOAT;
	if (!strcmp(f->name, "Abs") && f->nr_arguments == 1)
		return floating ? (void *)fabsf : (void *)abs;
	if (!strcmp(f->name, "Clamp") && f->nr_arguments == 3)
		return floating ? (void *)Math_ClampF : (void *)Math_Clamp;
	if (!strcmp(f->name, "Min")) {
		switch (f->nr_arguments) {
		case 2: return floating ? (void *)Math_MinF : (void *)Math_Min;
		case 3: return floating ? (void *)Math_MinF3 : (void *)Math_Min3;
		case 4: return floating ? (void *)Math_MinF4 : (void *)Math_Min4;
		}
	}
	if (!strcmp(f->name, "Max")) {
		switch (f->nr_arguments) {
		case 2: return floating ? (void *)Math_MaxF : (void *)Math_Max;
		case 3: return floating ? (void *)Math_MaxF3 : (void *)Math_Max3;
		case 4: return floating ? (void *)Math_MaxF4 : (void *)Math_Max4;
		}
	}
	return NULL;
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

static int Math_Rand(void)
{
	return math_genrand() & 0x7fffffff;
}

static float Math_RandF(void)
{
	return (float)(math_genrand() * (1.0 / 4294967296.0));
}

static void shuffle_array(int *a, int len)
{
	for (int i = len - 1; i > 0; i--) {
		int j = (int)(Math_RandF() * (i + 1));
		if (j > i)
			j = i;
		int tmp = a[j];
		a[j] = a[i];
		a[i] = tmp;
	}
}

static void Math_RandTableInit(int num, int size)
{
	if (size <= 0) {
		WARNING("Invalid rand table size %d for table %d", size, num);
		return;
	}
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (tbl) {
		free(tbl->elems);
	} else {
		tbl = xmalloc(sizeof(struct shuffle_table));
		tbl->id = num;
		tbl->next = shuffle_tables;
		shuffle_tables = tbl;
	}
	tbl->elems = xmalloc((size_t)size * sizeof(int));
	tbl->size = size;
	tbl->i = 0;
	for (int i = 0; i < size; i++)
		tbl->elems[i] = i;
	shuffle_array(tbl->elems, size);
}

static int Math_RandTable(int num)
{
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (!tbl || tbl->size <= 0) {
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

static void Math_ModuleFini(void)
{
	struct shuffle_table *tbl = shuffle_tables;
	while (tbl) {
		struct shuffle_table *next = tbl->next;
		free(tbl->elems);
		free(tbl);
		tbl = next;
	}
	shuffle_tables = NULL;
	mt_initialized = false;
}

//void Math_RandTable2Init(int num, struct page *array);
//int Math_RandTable2(int num);

static float Math_Asin(float x)
{
	if (isnan(x))
		return 0.0f;
	return asinf(Math_ClampF(x, -1.0f, 1.0f)) * (float)(180.0 / M_PI);
}

static float Math_Acos(float x)
{
	if (isnan(x))
		return 0.0f;
	return acosf(Math_ClampF(x, -1.0f, 1.0f)) * (float)(180.0 / M_PI);
}

static int Math_Ceil(float f)
{
	return (int)ceilf(f);
}

static int Math_Floor(float f)
{
	return (int)floorf(f);
}

static int Math_Round(float f)
{
	return (int)roundf(f);
}

static float Math_MTRandFInclude1(void)
{
	return (float)(math_genrand() * (1.0 / 4294967295.0));
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
	    HLL_EXPORT(_ModuleFini, Math_ModuleFini),
	    HLL_EXPORT(Cos, Math_Cos),
	    HLL_EXPORT(Sin, Math_Sin),
	    HLL_EXPORT(Tan, Math_Tan),
	    HLL_EXPORT(Asin, Math_Asin),
	    HLL_EXPORT(Acos, Math_Acos),
	    HLL_EXPORT(Sqrt, sqrtf),
	    HLL_EXPORT(Asin, asinf),
	    HLL_EXPORT(Acos, acosf),
	    HLL_EXPORT(Atan, atanf),
	    HLL_EXPORT(Atan2, atan2f),
	    HLL_OVERLOAD(Abs, Math_Resolve),
	    HLL_OVERLOAD(AbsF, Math_ResolveAbsF),
	    HLL_EXPORT(Pow, powf),
	    HLL_EXPORT(SetSeed, Math_SetSeed),
	    HLL_EXPORT(SetSeedByCurrentTime, Math_SetSeedByCurrentTime),
	    //HLL_EXPORT(SetRandMode, Math_SetRandMode),
	    HLL_EXPORT(Rand, Math_Rand),
	    HLL_EXPORT(RandF, Math_RandF),
	    HLL_EXPORT(MTSetSeed, Math_SetSeed),
	    HLL_EXPORT(MTSetSeedByCurrentTime, Math_SetSeedByCurrentTime),
	    HLL_EXPORT(MTRand, Math_Rand),
	    HLL_EXPORT(MTRandF, Math_RandF),
	    HLL_EXPORT(MTRandFInclude1, Math_MTRandFInclude1),
	    HLL_EXPORT(RandTableInit, Math_RandTableInit),
	    HLL_EXPORT(RandTable, Math_RandTable),
	    //HLL_EXPORT(RandTable2Init, Math_RandTable2Init),
	    //HLL_EXPORT(RandTable2, Math_RandTable2),
	    HLL_OVERLOAD(Min, Math_Resolve),
	    HLL_EXPORT(MinF, Math_MinF),
	    HLL_OVERLOAD(Max, Math_Resolve),
	    HLL_OVERLOAD(Clamp, Math_Resolve),
	    HLL_EXPORT(MaxF, Math_MaxF),
	    HLL_EXPORT(Swap, Math_Swap),
	    HLL_EXPORT(SwapF, Math_SwapF),
	    HLL_EXPORT(Log, logf),
	    HLL_EXPORT(Log10, log10f),
	    HLL_EXPORT(Ceil, Math_Ceil),
	    HLL_EXPORT(Floor, Math_Floor),
	    HLL_EXPORT(Round, Math_Round),
	    HLL_EXPORT(BezierCurve, Math_BezierCurve));

