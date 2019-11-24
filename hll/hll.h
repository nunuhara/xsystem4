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

#ifndef SYSTEM4_HLL_H
#define SYSTEM4_HLL_H

/*
 * DSL for implementing libraries.
 */

#include "../vm.h"
#include "../page.h"
#include "../system4.h"

#define hll_defun(fname, args)						\
	static union vm_value _hllfun_ ## fname(possibly_unused union vm_value *args); \
	static struct hll_function _hllstruct_ ## fname = {		\
		.name = #fname,						\
		.fun = _hllfun_ ## fname				\
	};								\
	static union vm_value _hllfun_ ## fname(possibly_unused union vm_value *args)

#define hll_defun_inline(fname, expr)			\
	hll_defun(fname, a) { hll_return(expr); }

#define hll_unimplemented(libname, fname)				\
	hll_defun(fname, args) {					\
		VM_ERROR("Unimplemented HLL function: " #libname "." #fname); \
	}

#define hll_unimplemented_warning(libname, fname)			\
	WARNING("Unimplemented HLL function: " #libname "." #fname)

#define hll_warn_unimplemented(libname, fname, rval)			\
	hll_defun(fname, args) {					\
		hll_unimplemented_warning(libname, fname);		\
		hll_return(rval);					\
	}

#define hll_ignore_unimplemented(fname, rval) hll_defun_inline(fname, rval)

#define hll_export(fname) &_hllstruct_ ## fname

#define hll_deflib(lname, ...)				\
	static struct hll_function *_lib_ ## lname[];	\
	struct library lib_ ## lname = {		\
		.name = #lname,				\
		.functions = _lib_ ## lname		\
	};						\
	static struct hll_function *_lib_ ## lname[] = 

#define hll_return(v) return vm_value_cast(v)

#endif /* SYSTEM4_HLL_H */
