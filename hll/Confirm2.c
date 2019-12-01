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

// void Init(imain_system pIMainSystem)
hll_warn_unimplemented(Confirm2, Init, 0);
// int ExistKeyFile(string pIFileName, int nKeyNum)
hll_warn_unimplemented(Confirm2, ExistKeyFile, 1);
// int CheckProtectFile(string pIFileName, int nKeyNum)
hll_unimplemented(Confirm2, CheckProtectFile);
// int CreateKeyFile(string pIFileName, int nKeyNum)
hll_unimplemented(Confirm2, CreateKeyFile);

hll_deflib(Confirm2) {
	hll_export(Init),
	hll_export(ExistKeyFile),
	hll_export(CheckProtectFile),
	hll_export(CreateKeyFile),
	NULL
};
