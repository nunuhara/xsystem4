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

#include <stdint.h>
#include <limits.h>

#include "hll.h"
#include "vm/page.h"

static bool is_even(int x) { return !(x & 1); }
static bool is_odd(int x) { return x & 1; }

static void check_array(struct page *array)
{
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		VM_ERROR("Not a flat integer array");
}

//int vmArray_GetAdd(struct page *array, int index);
//int vmArray_GetSub(struct page *array, int index);
//int vmArray_GetMul(struct page *array, int index);
//int vmArray_GetDiv(struct page *array, int index);
//int vmArray_GetAnd(struct page *array, int index);
//int vmArray_GetOr(struct page *array, int index);
//int vmArray_GetXor(struct page *array, int index);

static void transform(struct page *array, int index, int (*func)(int, void *), void *data)
{
	if (!array)
		return;
	check_array(array);

	if (index >= 0) {
		for (int i = index; i < array->nr_vars; i++) {
			array->values[i].i = func(array->values[i].i, data);
		}
	} else {
		for (int i = -index - 1; i >= 0; --i) {
			array->values[i].i = func(array->values[i].i, data);
		}
	}
}

static inline int32_t clamp(int64_t val)
{
	if (val > INT32_MAX)
		return INT32_MAX;
	if (val < INT32_MIN)
		return INT32_MIN;
	return val;
}

static int trans_add(int x, void *data)
{
	int64_t y = *(int*)data;
	return clamp(x + y);
}

static int trans_sub(int x, void *data)
{
	int64_t y = *(int*)data;
	return clamp(x - y);
}

static int trans_mul(int x, void *data)
{
	int64_t y = *(int*)data;
	return clamp(x * y);
}

static int trans_div(int x, void *data)
{
	int64_t y = *(int*)data;
	return clamp(x / y);
}

static int trans_and(int x, void *data)
{
	int y = *(int*)data;
	return x & y;
}

static int trans_or(int x, void *data)
{
	int y = *(int*)data;
	return x | y;
}

static int trans_xor(int x, void *data)
{
	int y = *(int*)data;
	return x ^ y;
}

static void vmArray_AddNum(struct page **array, int index, int num)
{
	transform(*array, index, trans_add, &num);
}

static void vmArray_SubNum(struct page **array, int index, int num)
{
	transform(*array, index, trans_sub, &num);
}

static void vmArray_MulNum(struct page **array, int index, int num)
{
	transform(*array, index, trans_mul, &num);
}

static void vmArray_DivNum(struct page **array, int index, int num)
{
	if (num != 0)
		transform(*array, index, trans_div, &num);
}

static void vmArray_AndNum(struct page **array, int index, int num)
{
	transform(*array, index, trans_and, &num);
}

static void vmArray_OrNum(struct page **array, int index, int num)
{
	transform(*array, index, trans_or, &num);
}

static void vmArray_XorNum(struct page **array, int index, int num)
{
	transform(*array, index, trans_xor, &num);
}

//void vmArray_MinNum(struct page **array, int index, int num);
//void vmArray_MaxNum(struct page **array, int index, int num);

struct vmarray_iter {
	struct page *array;
	int index;
};

static int trans_add_array(int x, void *data)
{
	struct vmarray_iter *iter = data;
	return clamp((int64_t)x + iter->array->values[iter->index++].i);
}

static int trans_sub_array(int x, void *data)
{
	struct vmarray_iter *iter = data;
	return clamp((int64_t)x - iter->array->values[iter->index++].i);
}

static int trans_mul_array(int x, void *data)
{
	struct vmarray_iter *iter = data;
	return clamp((int64_t)x * iter->array->values[iter->index++].i);
}

static int trans_div_array(int x, void *data)
{
	struct vmarray_iter *iter = data;
	int rhs = iter->array->values[iter->index++].i;
	return rhs ? x / rhs : x;
}

static int trans_and_array(int x, void *data)
{
	struct vmarray_iter *iter = data;
	return x & iter->array->values[iter->index++].i;
}

static int trans_or_array(int x, void *data)
{
	struct vmarray_iter *iter = data;
	return x | iter->array->values[iter->index++].i;
}

static int trans_xor_array(int x, void *data)
{
	struct vmarray_iter *iter = data;
	return x ^ iter->array->values[iter->index++].i;
}

static void vmArray_AddArray(struct page **d_array, int index, struct page *s_array)
{
	struct vmarray_iter iter = { s_array, 0 };
	transform(*d_array, index, trans_add_array, &iter);
}

static void vmArray_SubArray(struct page **d_array, int index, struct page *s_array)
{
	struct vmarray_iter iter = { s_array, 0 };
	transform(*d_array, index, trans_sub_array, &iter);
}

static void vmArray_MulArray(struct page **d_array, int index, struct page *s_array)
{
	struct vmarray_iter iter = { s_array, 0 };
	transform(*d_array, index, trans_mul_array, &iter);
}

static void vmArray_DivArray(struct page **d_array, int index, struct page *s_array)
{
	struct vmarray_iter iter = { s_array, 0 };
	transform(*d_array, index, trans_div_array, &iter);
}

static void vmArray_AndArray(struct page **d_array, int index, struct page *s_array)
{
	struct vmarray_iter iter = { s_array, 0 };
	transform(*d_array, index, trans_and_array, &iter);
}

static void vmArray_OrArray(struct page **d_array, int index, struct page *s_array)
{
	struct vmarray_iter iter = { s_array, 0 };
	transform(*d_array, index, trans_or_array, &iter);
}

static void vmArray_XorArray(struct page **d_array, int index, struct page *s_array)
{
	struct vmarray_iter iter = { s_array, 0 };
	transform(*d_array, index, trans_xor_array, &iter);
}

//void vmArray_MinArray(struct page **d_array, int index, struct page *s_array);
//void vmArray_MaxArray(struct page **d_array, int index, struct page *s_array);

static bool pred_equal(int x, void *data)
{
	int y = *(int*)data;
	return x == y;
}

static bool pred_not_equal(int x, void *data)
{
	int y = *(int*)data;
	return x != y;
}

static bool pred_low(int x, void *data)
{
	int y = *(int*)data;
	return x <= y;
}

static bool pred_high(int x, void *data)
{
	int y = *(int*)data;
	return y <= x;
}

struct range {
	int min;
	int max;
};

static bool pred_in_range(int x, void *data)
{
	struct range *r = data;
	return r->min <= x && x <= r->max;
}

static int count_if(struct page *array, int index, bool (*pred)(int, void *), void *data)
{
	if (!array)
		return 0;
	check_array(array);

	int count = 0;
	for (int i = index; i < array->nr_vars; i++) {
		if (pred(array->values[i].i, data))
			count++;
	}
	return count;
}

static int vmArray_EnumEquNum(struct page *array, int index, int num)
{
	return count_if(array, index, pred_equal, &num);
}

static int vmArray_EnumNotNum(struct page *array, int index, int num)
{
	return count_if(array, index, pred_not_equal, &num);
}

static int vmArray_EnumLowNum(struct page *array, int index, int num)
{
	return count_if(array, index, pred_low, &num);
}

static int vmArray_EnumHighNum(struct page *array, int index, int num)
{
	return count_if(array, index, pred_high, &num);
}

static int vmArray_EnumRangeNum(struct page *array, int index, int min, int max)
{
	struct range r = { .min = min, .max = max };
	return count_if(array, index, pred_in_range, &r);
}

static void replace(struct page *array, int index, int n, bool (*pred)(int, void *), void *data)
{
	if (!array)
		return;
	check_array(array);

	for (int i = index; i < array->nr_vars; i++) {
		if (pred(array->values[i].i, data))
			array->values[i].i = n;
	}
}

static void vmArray_ChangeEquNum(struct page **array, int index, int num, int exg)
{
	replace(*array, index, exg, pred_equal, &num);
}

static void vmArray_ChangeNotNum(struct page **array, int index, int num, int exg)
{
	replace(*array, index, exg, pred_not_equal, &num);
}

static void vmArray_ChangeLowNum(struct page **array, int index, int num, int exg)
{
	replace(*array, index, exg, pred_low, &num);
}

static void vmArray_ChangeHighNum(struct page **array, int index, int num, int exg)
{
	replace(*array, index, exg, pred_high, &num);
}

static void vmArray_ChangeRangeNum(struct page **array, int index, int min, int max, int exg)
{
	struct range r = { .min = min, .max = max };
	replace(*array, index, exg, pred_in_range, &r);
}

static int find(struct page *array, int index, bool (*pred)(int, void *), void *data, int *out_index)
{
	if (!array)
		return 0;
	check_array(array);

	if (index >= 0) {
		for (int i = index; i < array->nr_vars; i++) {
			if (pred(array->values[i].i, data)) {
				*out_index = i;
				return 1;
			}
		}
	} else {
		for (int i = -index - 1; i >= 0; --i) {
			if (pred(array->values[i].i, data)) {
				*out_index = i;
				return 1;
			}
		}
	}
	return 0;
}

static int vmArray_GrepEquNum(struct page *array, int index, int num, int *out_index)
{
	return find(array, index, pred_equal, &num, out_index);
}

static int vmArray_GrepNotNum(struct page *array, int index, int num, int *out_index)
{
	return find(array, index, pred_not_equal, &num, out_index);
}

static int vmArray_GrepLowNum(struct page *array, int index, int num, int *out_index)
{
	return find(array, index, pred_low, &num, out_index);
}

static int vmArray_GrepHighNum(struct page *array, int index, int num, int *out_index)
{
	return find(array, index, pred_high, &num, out_index);
}

static int vmArray_GrepRangeNum(struct page *array, int index, int min, int max, int *out_index)
{
	struct range r = { .min = min, .max = max };
	return find(array, index, pred_in_range, &r, out_index);
}

static int vmArray_GrepLowOrder(struct page *s_array, int index, struct page **d_array_, int *out_index)
{
	struct page *d_array = *d_array_;
	check_array(s_array);
	check_array(d_array);

	if (index < 0)
		VM_ERROR("not implemented");

	int min_index = -1, min_value = INT_MAX;
	for (int i = index; i < s_array->nr_vars; i++) {
		if (d_array->values[i].i)
			continue;
		int val = s_array->values[i].i;
		if (min_index < 0 || val < min_value) {
			min_index = i;
			min_value = val;
		}
	}
	if (min_index < 0)
		return 0;
	*out_index = min_index;
	d_array->values[min_index].i = 1;
	return 1;
}

static int vmArray_GrepHighOrder(struct page *s_array, int index, struct page **d_array_, int *out_index)
{
	struct page *d_array = *d_array_;
	check_array(s_array);
	check_array(d_array);

	if (index < 0)
		VM_ERROR("not implemented");

	int max_index = -1, max_value = INT_MIN;
	for (int i = index; i < s_array->nr_vars; i++) {
		if (d_array->values[i].i)
			continue;
		int val = s_array->values[i].i;
		if (max_index < 0 || val > max_value) {
			max_index = i;
			max_value = val;
		}
	}
	if (max_index < 0)
		return 0;
	*out_index = max_index;
	d_array->values[max_index].i = 1;
	return 1;
}

static void map_predicate(struct page *s_array, int index, bool (*pred)(int, void *), void *data, struct page *d_array)
{
	if (!s_array || !d_array)
		return;
	check_array(s_array);
	check_array(d_array);

	if (index >= 0) {
		for (int i = index; i < s_array->nr_vars; i++) {
			d_array->values[i].i = pred(s_array->values[i].i, data) ? 1 : 0;
		}
	} else {
		for (int i = -index - 1; i >= 0; --i) {
			d_array->values[i].i = pred(s_array->values[i].i, data) ? 1 : 0;
		}
	}
}

static void vmArray_SetEquNum(struct page *s_array, int index, int num, struct page **d_array)
{
	map_predicate(s_array, index, pred_equal, &num, *d_array);
}

static void vmArray_SetNotNum(struct page *s_array, int index, int num, struct page **d_array)
{
	map_predicate(s_array, index, pred_not_equal, &num, *d_array);
}

static void vmArray_SetLowNum(struct page *s_array, int index, int num, struct page **d_array)
{
	map_predicate(s_array, index, pred_low, &num, *d_array);
}

static void vmArray_SetHighNum(struct page *s_array, int index, int num, struct page **d_array)
{
	map_predicate(s_array, index, pred_high, &num, *d_array);
}

static void vmArray_SetRangeNum(struct page *s_array, int index, int min, int max, struct page **d_array)
{
	struct range r = { .min = min, .max = max };
	map_predicate(s_array, index, pred_in_range, &r, *d_array);
}

//void vmArray_AndEquNum(struct page *pISVMArray, int index, int num, struct page **pIDVMArray);
//void vmArray_AndNotNum(struct page *pISVMArray, int index, int num, struct page **pIDVMArray);
//void vmArray_AndLowNum(struct page *pISVMArray, int index, int num, struct page **pIDVMArray);
//void vmArray_AndHighNum(struct page *pISVMArray, int index, int num, struct page **pIDVMArray);
//void vmArray_AndRangeNum(struct page *pISVMArray, int index, int nMin, int nMax, struct page **pIDVMArray);

static void set_if(struct page *s_array, int index, bool (*pred)(int, void *), void *data, struct page *d_array)
{
	if (!s_array || !d_array)
		return;
	check_array(s_array);
	check_array(d_array);

	if (index >= 0) {
		for (int i = index; i < s_array->nr_vars; i++) {
			if (pred(s_array->values[i].i, data))
				d_array->values[i].i = 1;
		}
	} else {
		for (int i = -index - 1; i >= 0; --i) {
			if (pred(s_array->values[i].i, data))
				d_array->values[i].i = 1;
		}
	}
}

static void vmArray_OrEquNum(struct page *s_array, int index, int num, struct page **d_array)
{
	set_if(s_array, index, pred_equal, &num, *d_array);
}

static void vmArray_OrNotNum(struct page *s_array, int index, int num, struct page **d_array)
{
	set_if(s_array, index, pred_not_equal, &num, *d_array);
}

static void vmArray_OrLowNum(struct page *s_array, int index, int num, struct page **d_array)
{
	set_if(s_array, index, pred_low, &num, *d_array);
}

static void vmArray_OrHighNum(struct page *s_array, int index, int num, struct page **d_array)
{
	set_if(s_array, index, pred_high, &num, *d_array);
}

static void vmArray_OrRangeNum(struct page *s_array, int index, int min, int max, struct page **d_array)
{
	struct range r = { .min = min, .max = max };
	set_if(s_array, index, pred_in_range, &r, *d_array);
}

// Find num by visiting elements spirally around (x, y).
static int vmArray_AroundRect(struct page *array, int width, int height, int x, int y, int length, int num, int *out_x, int *out_y)
{
	for (int dist = 1;; dist++) {
		// walk one step north
		if (--length < 0)
			return 0;
		y--;
		if (0 <= x && x < width && 0 <= y && y < height && array->values[y * width + x].i == num)
			goto found;

		// walk southeast
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			x++;
			y++;
			if (0 <= x && x < width && 0 <= y && y < height && array->values[y * width + x].i == num)
				goto found;
		}
		// walk southwest
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			x--;
			y++;
			if (0 <= x && x < width && 0 <= y && y < height && array->values[y * width + x].i == num)
				goto found;
		}
		// walk northwest
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			x--;
			y--;
			if (0 <= x && x < width && 0 <= y && y < height && array->values[y * width + x].i == num)
				goto found;
		}
		// walk northeast
		for (int i = 0; i < dist - 1; i++) {
			if (--length < 0)
				return 0;
			x++;
			y--;
			if (0 <= x && x < width && 0 <= y && y < height && array->values[y * width + x].i == num)
				goto found;
		}
		x++;
		y--;
	}
 found:
	*out_x = x;
	*out_y = y;
	return 1;
}

/* Hex boards are indexed like this (width = 5, height = 3, for example):
 *
 *    +--+  +--+  +--+
 *    | 0|--| 2|--| 4|
 *    |--| 1|--| 3|--|
 *    | 5|--| 7|--| 9|
 *    |--| 6|--| 8|--|
 *    |10|--|12|--|14|
 *    +--+  +--+  +--+
 *
 * Note that odd-numbered columns are one height lower. Because of this, indices
 * 11 and 13 are unused.
*/
static bool is_valid_hex(int x, int y, int w, int h) {
	return 0 <= x && x < w && 0 <= y && y < h && !(y == h - 1 && is_odd(x));
}

// Find num by visiting elements spirally around (x, y).
static int vmArray_AroundHexa(struct page *array, int width, int height, int x, int y, int length, int num, int *out_x, int *out_y)
{
	for (int dist = 1;; dist++) {
		// walk one step north
		if (--length < 0)
			return 0;
		y--;
		if (is_valid_hex(x, y, width, height) && array->values[y * width + x].i == num)
			goto found;
		// walk southeast
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			if (is_even(++x))
				y++;
			if (is_valid_hex(x, y, width, height) && array->values[y * width + x].i == num)
				goto found;
		}
		// walk south
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			y++;
			if (is_valid_hex(x, y, width, height) && array->values[y * width + x].i == num)
				goto found;
		}
		// walk southwest
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			if (is_even(--x))
				y++;
			if (is_valid_hex(x, y, width, height) && array->values[y * width + x].i == num)
				goto found;
		}
		// walk northwest
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			if (is_odd(--x))
				y--;
			if (is_valid_hex(x, y, width, height) && array->values[y * width + x].i == num)
				goto found;
		}
		// walk north
		for (int i = 0; i < dist; i++) {
			if (--length < 0)
				return 0;
			y--;
			if (is_valid_hex(x, y, width, height) && array->values[y * width + x].i == num)
				goto found;
		}
		// walk northeast
		for (int i = 0; i < dist - 1; i++) {
			if (--length < 0)
				return 0;
			if (is_odd(++x))
				y--;
			if (is_valid_hex(x, y, width, height) && array->values[y * width + x].i == num)
				goto found;
		}
		if (is_odd(++x))
			y--;
	}
 found:
	*out_x = x;
	*out_y = y;
	return 1;
}

static int vmArray_PaintRect(struct page **array_, int width, int height, int x, int y, int length)
{
	struct page *array = *array_;
	check_array(array);
	if (array->nr_vars < width * height || x >= width || y >= height || length < 0)
		return 0;

	int size = width * height;
	for (int i = 0; i < size; i++) {
		array->values[i].i = array->values[i].i < 0 ? -2 : -1;
	}

	int count = 0;
	array->values[y * width + x].i = 0;
	for (int dist = 0; dist < length; dist++) {
		for (int i = 0; i < size; i++) {
			if (array->values[i].i != dist)
				continue;
			if (i / width > 0 && array->values[i - width].i == -1) {
				array->values[i - width].i = dist + 1;
				count++;
			}
			if (i / width < height - 1 && array->values[i + width].i == -1) {
				array->values[i + width].i = dist + 1;
				count++;
			}
			if (i % width > 0 && array->values[i - 1].i == -1) {
				array->values[i - 1].i = dist + 1;
				count++;
			}
			if (i % width < width - 1 && array->values[i + 1].i == -1) {
				array->values[i + 1].i = dist + 1;
				count++;
			}
		}
	}
	return count;
}

static int vmArray_PaintHexa(struct page **array_, int width, int height, int cx, int cy, int length)
{
	struct page *array = *array_;
	check_array(array);

	if (width <= 0 || height <= 0 || cx >= width || cy >= height || length < 0)
		return 0;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int val = array->values[y * width + x].i < 0 ? -2 : -1;
			if (y == height - 1 && is_odd(x))
				val = -2;
			array->values[y * width + x].i = val;
		}
	}

	int count = 0;
	array->values[width * cy + cx].i = 0;
	for (int dist = 0; dist < length; dist++) {
		for (int i = 0; i < width * height; i++) {
			if (array->values[i].i != dist)
				continue;
			int y = i / width;
			if (y > 0 && array->values[i - width].i == -1) {
				array->values[i - width].i = dist + 1;
				count++;
			}
			if (y < height - 1 && array->values[i + width].i == -1) {
				array->values[i + width].i = dist + 1;
				count++;
			}
			int x = i % width;
			if (x > 0 && array->values[i - 1].i == -1) {
				array->values[i - 1].i = dist + 1;
				count++;
			}
			if (x < width - 1 && array->values[i + 1].i == -1) {
				array->values[i + 1].i = dist + 1;
				count++;
			}
			if (is_even(x)) {
				if (y > 0 && x > 0 && array->values[i - width - 1].i == -1) {
					array->values[i - width - 1].i = dist + 1;
					count++;
				}
				if (y > 0 && x < width - 1 && array->values[i - width + 1].i == -1) {
					array->values[i - width + 1].i = dist + 1;
					count++;
				}
			} else {
				if (y < height - 1 && x > 0 && array->values[i + width - 1].i == -1) {
					array->values[i + width - 1].i = dist + 1;
					count++;
				}
				if (y < height - 1 && x < width - 1 && array->values[i + width + 1].i == -1) {
					array->values[i + width + 1].i = dist + 1;
					count++;
				}
			}
		}
	}
	return count;
}


static int vmArray_CopyRectToRect(struct page **d_array_, int dw, int dh, int dx, int dy, struct page *s_array, int sw, int sh, int sx, int sy, int w, int h)
{
	struct page *d_array = *d_array_;
	check_array(s_array);
	check_array(d_array);
	if (s_array->nr_vars < sw * sh || d_array->nr_vars < dw * dh)
		return 0;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			if (dx + x < 0 || dx + x >= dw || dy + y < 0 || dy + y >= dh)
				continue;
			if (sx + x < 0 || sx + x >= sw || sy + y < 0 || sy + y >= sh)
				continue;
			d_array->values[(dy + y) * dw + dx + x] = s_array->values[(sy + y) * sw + sx + x];
		}
	}
	return 1;
}

static int vmArray_CopyHexaToHexa(struct page **d_array_, int dw, int dh, int dx, int dy, struct page *s_array, int sw, int sh, int sx, int sy, int cw, int ch)
{
	struct page *d_array = *d_array_;
	check_array(s_array);
	check_array(d_array);

	if (is_even(sx) == is_even(dx)) {
		for (int y = 0; y < ch; y++) {
			for (int x = 0; x < cw; x++) {
				if (is_valid_hex(sx + x, sy + y, sw, sh) && is_valid_hex(dx + x, dy + y, dw, dh)) {
					d_array->values[(dy + y) * dw + dx + x] = s_array->values[(sy + y) * sw + sx + x];
				}
			}
		}
	} else if (is_even(dx)) {  // odd sx, even dx
		for (int y = 0; y < ch; y++) {
			for (int x = 0; x < cw; x++) {
				int syy = is_even(sx + x) ? sy + y + 1 : sy + y;
				if (is_valid_hex(sx + x, syy, sw, sh) && is_valid_hex(dx + x, dy + y, dw, dh)) {
					d_array->values[(dy + y) * dw + dx + x] = s_array->values[syy * sw + sx + x];
				}
			}
		}
	} else {  // even sx, odd dx
		for (int y = 0; y < ch; y++) {
			for (int x = 0; x < cw; x++) {
				int dyy = is_odd(sx + x) ? dy + y + 1 : dy + y;
				if (is_valid_hex(sx + x, sy + y, sw, sh) && is_valid_hex(dx + x, dyy, dw, dh)) {
					d_array->values[dyy * dw + dx + x] = s_array->values[(sy + y) * sw + sx + x];
				}
			}
		}
	}
	return 1;
}

// Transpose s_array (as a 2D array of s_width * s_height) into d_array.
static int vmArray_ConvertRectSide(struct page **d_array_, struct page *s_array, int s_width, int s_height, int side)
{
	struct page *d_array = *d_array_;
	check_array(s_array);
	check_array(d_array);
	int w = side ? s_width : s_height;
	int h = side ? s_height : s_width;

	if (s_array->nr_vars < w * h || d_array->nr_vars < w * h)
		return 0;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			d_array->values[x * h + y] = s_array->values[y * w + x];
		}
	}
	return 1;
}

HLL_LIBRARY(vmArray,
	    HLL_TODO_EXPORT(GetAdd, vmArray_GetAdd),
	    HLL_TODO_EXPORT(GetSub, vmArray_GetSub),
	    HLL_TODO_EXPORT(GetMul, vmArray_GetMul),
	    HLL_TODO_EXPORT(GetDiv, vmArray_GetDiv),
	    HLL_TODO_EXPORT(GetAnd, vmArray_GetAnd),
	    HLL_TODO_EXPORT(GetOr, vmArray_GetOr),
	    HLL_TODO_EXPORT(GetXor, vmArray_GetXor),
	    HLL_EXPORT(AddNum, vmArray_AddNum),
	    HLL_EXPORT(SubNum, vmArray_SubNum),
	    HLL_EXPORT(MulNum, vmArray_MulNum),
	    HLL_EXPORT(DivNum, vmArray_DivNum),
	    HLL_EXPORT(AndNum, vmArray_AndNum),
	    HLL_EXPORT(OrNum, vmArray_OrNum),
	    HLL_EXPORT(XorNum, vmArray_XorNum),
	    HLL_TODO_EXPORT(MinNum, vmArray_MinNum),
	    HLL_TODO_EXPORT(MaxNum, vmArray_MaxNum),
	    HLL_EXPORT(AddArray, vmArray_AddArray),
	    HLL_EXPORT(SubArray, vmArray_SubArray),
	    HLL_EXPORT(MulArray, vmArray_MulArray),
	    HLL_EXPORT(DivArray, vmArray_DivArray),
	    HLL_EXPORT(AndArray, vmArray_AndArray),
	    HLL_EXPORT(OrArray, vmArray_OrArray),
	    HLL_EXPORT(XorArray, vmArray_XorArray),
	    HLL_TODO_EXPORT(MinArray, vmArray_MinArray),
	    HLL_TODO_EXPORT(MaxArray, vmArray_MaxArray),
	    HLL_EXPORT(EnumEquNum, vmArray_EnumEquNum),
	    HLL_EXPORT(EnumNotNum, vmArray_EnumNotNum),
	    HLL_EXPORT(EnumLowNum, vmArray_EnumLowNum),
	    HLL_EXPORT(EnumHighNum, vmArray_EnumHighNum),
	    HLL_EXPORT(EnumRangeNum, vmArray_EnumRangeNum),
	    HLL_EXPORT(ChangeEquNum, vmArray_ChangeEquNum),
	    HLL_EXPORT(ChangeNotNum, vmArray_ChangeNotNum),
	    HLL_EXPORT(ChangeLowNum, vmArray_ChangeLowNum),
	    HLL_EXPORT(ChangeHighNum, vmArray_ChangeHighNum),
	    HLL_EXPORT(ChangeRangeNum, vmArray_ChangeRangeNum),
	    HLL_EXPORT(GrepEquNum, vmArray_GrepEquNum),
	    HLL_EXPORT(GrepNotNum, vmArray_GrepNotNum),
	    HLL_EXPORT(GrepLowNum, vmArray_GrepLowNum),
	    HLL_EXPORT(GrepHighNum, vmArray_GrepHighNum),
	    HLL_EXPORT(GrepRangeNum, vmArray_GrepRangeNum),
	    HLL_EXPORT(GrepLowOrder, vmArray_GrepLowOrder),
	    HLL_EXPORT(GrepHighOrder, vmArray_GrepHighOrder),
	    HLL_EXPORT(SetEquNum, vmArray_SetEquNum),
	    HLL_EXPORT(SetNotNum, vmArray_SetNotNum),
	    HLL_EXPORT(SetLowNum, vmArray_SetLowNum),
	    HLL_EXPORT(SetHighNum, vmArray_SetHighNum),
	    HLL_EXPORT(SetRangeNum, vmArray_SetRangeNum),
	    HLL_TODO_EXPORT(AndEquNum, vmArray_AndEquNum),
	    HLL_TODO_EXPORT(AndNotNum, vmArray_AndNotNum),
	    HLL_TODO_EXPORT(AndLowNum, vmArray_AndLowNum),
	    HLL_TODO_EXPORT(AndHighNum, vmArray_AndHighNum),
	    HLL_TODO_EXPORT(AndRangeNum, vmArray_AndRangeNum),
	    HLL_EXPORT(OrEquNum, vmArray_OrEquNum),
	    HLL_EXPORT(OrNotNum, vmArray_OrNotNum),
	    HLL_EXPORT(OrLowNum, vmArray_OrLowNum),
	    HLL_EXPORT(OrHighNum, vmArray_OrHighNum),
	    HLL_EXPORT(OrRangeNum, vmArray_OrRangeNum),
	    HLL_EXPORT(AroundRect, vmArray_AroundRect),
	    HLL_EXPORT(AroundHexa, vmArray_AroundHexa),
	    HLL_EXPORT(PaintRect, vmArray_PaintRect),
	    HLL_EXPORT(PaintHexa, vmArray_PaintHexa),
	    HLL_EXPORT(CopyRectToRect, vmArray_CopyRectToRect),
	    HLL_EXPORT(CopyHexaToHexa, vmArray_CopyHexaToHexa),
	    HLL_EXPORT(ConvertRectSide, vmArray_ConvertRectSide)
	    );
