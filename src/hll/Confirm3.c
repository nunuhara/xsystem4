/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

HLL_QUIET_UNIMPLEMENTED( , void, Confirm3, Init, void *iMainsystem);
HLL_QUIET_UNIMPLEMENTED(0, int, Confirm3, Initialize, void);
HLL_QUIET_UNIMPLEMENTED(0, int, Confirm3, GetHDDVolumeNumber, void);
HLL_QUIET_UNIMPLEMENTED(5, int, Confirm3, GetDrive, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(0, int, Confirm3, GetRandom, int seed);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_Open, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_Create, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_SetPos, int pos);

static bool Confirm3_File_ReadByte(int *data)
{
	*data = 0;
	return true;
}

static bool Confirm3_File_ReadInt(int *data)
{
	*data = 0;
	return true;
}

HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_WriteInt, int data);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_Close, void);

HLL_LIBRARY(Confirm3,
	    HLL_EXPORT(Init, Confirm3_Init),
	    HLL_EXPORT(GetHDDVolumeNumber, Confirm3_GetHDDVolumeNumber),
	    HLL_EXPORT(Initialize, Confirm3_Initialize),
	    HLL_EXPORT(GetDrive, Confirm3_GetDrive),
	    HLL_EXPORT(GetRandom, Confirm3_GetRandom),
	    HLL_EXPORT(File_Open, Confirm3_File_Open),
	    HLL_EXPORT(File_Create, Confirm3_File_Create),
	    HLL_EXPORT(File_SetPos, Confirm3_File_SetPos),
	    HLL_EXPORT(File_ReadByte, Confirm3_File_ReadByte),
	    HLL_EXPORT(File_ReadInt, Confirm3_File_ReadInt),
	    HLL_EXPORT(File_WriteInt, Confirm3_File_WriteInt),
	    HLL_EXPORT(File_Close, Confirm3_File_Close)
	    );
