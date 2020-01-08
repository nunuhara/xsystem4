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

HLL_WARN_UNIMPLEMENTED( , void, Confirm2, Init, void *imainsystem);
HLL_WARN_UNIMPLEMENTED(1, int,  Confirm2, ExistKeyFile, struct string *filename, int keynum);
HLL_UNIMPLEMENTED(int, Confirm2, CheckProtectFile, struct string *filename, int keynum);
HLL_UNIMPLEMENTED(int, Confirm2, CreateKeyFile, struct string *filename, int keynum);

HLL_LIBRARY(Confirm2,
	    HLL_EXPORT(Init, Confirm2_Init),
	    HLL_EXPORT(ExistKeyFile, Confirm2_ExistKeyFile),
	    HLL_EXPORT(CheckProtectFile, Confirm2_CheckProtectFile),
	    HLL_EXPORT(CreateKeyFile, Confirm2_CreateKeyFile));
