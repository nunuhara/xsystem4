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
#include "system4/string.h"

//static bool InitPastelChime2(void);
//static bool LoadArray(struct string file_name, int buf);
//static bool LoadStringArray(struct string file_name, struct string buf);
//static bool SaveStringArray(string file_name, struct string buf);
//static bool File_Delete(struct string file_name);


HLL_WARN_UNIMPLEMENTED(true, bool, PastelChime2, InitPastelChime2, void);
HLL_WARN_UNIMPLEMENTED(false, bool, PastelChime2, LoadArray, struct string file_name, int buf[]);
HLL_WARN_UNIMPLEMENTED(true, bool, PastelChime2, LoadStringArray, struct string file_name, struct string buf[]);
HLL_WARN_UNIMPLEMENTED(true, bool, PastelChime2, File_Delete, struct string file_name);
HLL_WARN_UNIMPLEMENTED(true, bool, PastelChime2, SaveStringArray, struct string file_name, struct string buf[]);

HLL_LIBRARY(PastelChime2,
	HLL_EXPORT(InitPastelChime2, PastelChime2_InitPastelChime2),
	HLL_EXPORT(LoadArray, PastelChime2_LoadArray),
	HLL_EXPORT(LoadStringArray, PastelChime2_LoadStringArray),
    HLL_EXPORT(File_Delete, PastelChime2_File_Delete),
    HLL_EXPORT(SaveStringArray, PastelChime2_SaveStringArray));
