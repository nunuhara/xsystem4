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

#include <stdlib.h>
#include <string.h>

#include "hll.h"
#include "vm/page.h"

struct bitarray {
	uint8_t *bits;
	int size;
};

static struct bitarray *arrays;
static int nr_arrays;

static bool is_valid_bitarray(int id)
{
	if (id < 0 || id >= nr_arrays || !arrays[id].bits) {
		WARNING("Invalid bitarray handle %d", id);
		return false;
	}
	return true;
}

static int Bitarray_Create(int length)
{
	int i;
	for (i = 0; i < nr_arrays && arrays[i].bits; i++)
		;
	if (i == nr_arrays) {
		int new_size = nr_arrays + 16;
		arrays = xrealloc_array(arrays, nr_arrays, new_size, sizeof(struct bitarray));
		nr_arrays = new_size;
	}
	arrays[i].size = length;
	arrays[i].bits = xcalloc((length + 31) / 32, 4);  // 32-bit alignment
	return i;
}

// void Resize(int nBitSet, int nLength);

static int Bitarray_Numof(int bitSet)
{
	if (!is_valid_bitarray(bitSet))
		return 0;
	return arrays[bitSet].size;
}

static int Bitarray_GetBit(int bitSet, int posBit)
{
	if (!is_valid_bitarray(bitSet))
		return 0;
	if (posBit < 0 || posBit >= arrays[bitSet].size) {
		WARNING("Bitarray index out of bounds %d", posBit);
		return 0;
	}
	return !!(arrays[bitSet].bits[posBit >> 3] & 1 << (posBit & 7));
}

static void Bitarray_SetBit(int bitSet, int posBit, int bit)
{
	if (!is_valid_bitarray(bitSet))
		return;
	if (posBit < 0 || posBit >= arrays[bitSet].size) {
		WARNING("Bitarray index out of bounds %d", posBit);
		return;
	}
	if (bit)
		arrays[bitSet].bits[posBit >> 3] |= 1 << (posBit & 7);
	else
		arrays[bitSet].bits[posBit >> 3] &= ~(1 << (posBit & 7));
}

// int GetByte(int nBitSet, int nPosByte);
// void SetByte(int nBitSet, int nPosByte, int nByte);
// int GetWord(int nBitSet, int nPosWord);
// void SetWord(int nBitSet, int nPosWord, int nWord);
// int GetInt(int nBitSet, int nPosInt);
// void SetInt(int nBitSet, int nPosInt, int nInt);

static void Bitarray_GetArray(int bitSet, struct page **page)
{
	if (!is_valid_bitarray(bitSet))
		return;

	struct page *array = *page;
	if (!array || array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		ERROR("Not an flat integer array");

	int n = (arrays[bitSet].size + 31) / 32;
	if (n > array->nr_vars)
		n = array->nr_vars;

	uint32_t *p = (uint32_t *)arrays[bitSet].bits;
	for (int i = 0; i < n; i++)
		p[i] = array->values[i].i;
}

static void Bitarray_SetArray(int bitSet, struct page **page)
{
	if (!is_valid_bitarray(bitSet))
		return;

	struct page *array = *page;
	if (!array || array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		ERROR("Not an flat integer array");

	int n = (arrays[bitSet].size + 31) / 32;
	if (n > array->nr_vars)
		n = array->nr_vars;

	uint32_t *p = (uint32_t *)arrays[bitSet].bits;
	for (int i = 0; i < n; i++)
		variable_set(array, i, AIN_INT, vm_int(p[i]));
}

static void Bitarray_Free(int bitSet)
{
	if (!is_valid_bitarray(bitSet))
		return;
	free(arrays[bitSet].bits);
	arrays[bitSet].bits = NULL;
	arrays[bitSet].size = 0;
}

static void Bitarray_FreeAll(void)
{
	for (int i = 0; i < nr_arrays; i++) {
		if (arrays[i].bits)
			Bitarray_Free(i);
	}
}

HLL_LIBRARY(Bitarray,
			HLL_EXPORT(Create, Bitarray_Create),
			// HLL_EXPORT(Resize, Bitarray_Resize),
			HLL_EXPORT(Numof, Bitarray_Numof),
			HLL_EXPORT(GetBit, Bitarray_GetBit),
			HLL_EXPORT(SetBit, Bitarray_SetBit),
			// HLL_EXPORT(GetByte, Bitarray_GetByte),
			// HLL_EXPORT(SetByte, Bitarray_SetByte),
			// HLL_EXPORT(GetWord, Bitarray_GetWord),
			// HLL_EXPORT(SetWord, Bitarray_SetWord),
			// HLL_EXPORT(GetInt, Bitarray_GetInt),
			// HLL_EXPORT(SetInt, Bitarray_SetInt),
			HLL_EXPORT(GetArray, Bitarray_GetArray),
			HLL_EXPORT(SetArray, Bitarray_SetArray),
			HLL_EXPORT(Free, Bitarray_Free),
			HLL_EXPORT(FreeAll, Bitarray_FreeAll));
