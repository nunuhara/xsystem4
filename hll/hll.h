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

#ifndef SYSTEM4_HLL_H
#define SYSTEM4_HLL_H

/*
 * DSL for implementing for implementing libraries.
 */

#include "../vm.h"
#include "../page.h"

#define hll_defun(fname)						\
	static void _hllfun_ ## fname(void);				\
	static struct hll_function _hllstruct_ ## fname = {		\
		.name = #fname,						\
		.fun = _hllfun_ ## fname				\
	};								\
	static void _hllfun_ ## fname(void)

#define hll_unimplemented(fname)				\
	hll_defun(fname) {					\
		ERROR("Unimplemented HLL function: " #fname);	\
	}

#define hll_export(fname) &_hllstruct_ ## fname

#define hll_deflib(lname, ...)				\
	static struct hll_function *_lib_ ## lname[];	\
	struct library lib_ ## lname = {		\
		.name = #lname,				\
		.functions = _lib_ ## lname		\
	};						\
	static struct hll_function *_lib_ ## lname[] = 

#define hll_return(v) stack_push(vm_value_cast(v))

static inline union vm_value hll_arg(int n)
{
	return stack[stack_ptr + n];
}

#endif /* SYSTEM4_HLL_H */
