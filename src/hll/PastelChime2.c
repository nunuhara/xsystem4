/* Copyright (C) 2022 nao1215 <n.chika156@gmail.com>
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

//static void PastelChime2_InitPastelChime2(void);
//static void PastelChime2_LoadArray(void);
//static void PastelChime2_LoadStringArray(void);
//static void PastelChime2_File_Delete(void);
//static void PastelChime2_SaveStringArray(void);

HLL_WARN_UNIMPLEMENTED(, void, PastelChime2, InitPastelChime2, void);
HLL_WARN_UNIMPLEMENTED(, void, PastelChime2, LoadArray, void);
HLL_WARN_UNIMPLEMENTED(, void, PastelChime2, LoadStringArray, void);
HLL_WARN_UNIMPLEMENTED(, void, PastelChime2, File_Delete, void);
HLL_WARN_UNIMPLEMENTED(, void, PastelChime2, SaveStringArray, void);

HLL_LIBRARY(PastelChime2,
	HLL_EXPORT(InitPastelChime2, PastelChime2_InitPastelChime2),
	HLL_EXPORT(LoadArray, PastelChime2_LoadArray),
	HLL_EXPORT(LoadStringArray, PastelChime2_LoadStringArray),
    HLL_EXPORT(File_Delete, PastelChime2_File_Delete),
    HLL_EXPORT(SaveStringArray, PastelChime2_SaveStringArray));
