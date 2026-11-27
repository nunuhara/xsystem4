/* Copyright (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4/string.h"
#include "hll.h"

static bool HTTPDownloader_IsRun(void) { return false; }
static void HTTPDownloader_Stop(void) { }
static int HTTPDownloader_GetReadSize(void) { return 0; }
static bool HTTPDownloader_ReadString(struct string **s) { (void)s; return false; }
static bool HTTPDownloader_ReadByte(int *v) { if (v) *v = 0; return false; }
static bool HTTPDownloader_ReadInt(int *v) { if (v) *v = 0; return false; }
static bool HTTPDownloader_ReadFloat(float *v) { if (v) *v = 0.0f; return false; }
static bool HTTPDownloader_ConvertString(struct string **s)
{
	(void)s;
	return true;
}

HLL_WARN_UNIMPLEMENTED(false, bool, HTTPDownloader, Get, struct string *pIURL);
HLL_WARN_UNIMPLEMENTED(false, bool, HTTPDownloader, Post, struct string *pIURL, struct string *pIParam);
//bool HTTPDownloader_IsRun(void);
//void HTTPDownloader_Stop(void);
//int HTTPDownloader_GetReadSize(void);
//bool HTTPDownloader_ReadString(struct string **pIString);
//bool HTTPDownloader_ReadStringUTF8ToSJIS(struct string **pIString);

HLL_LIBRARY(HTTPDownloader,
	    HLL_EXPORT(Get, HTTPDownloader_Get),
	    HLL_EXPORT(Post, HTTPDownloader_Post),
	    HLL_EXPORT(IsRun, HTTPDownloader_IsRun),
	    HLL_EXPORT(Stop, HTTPDownloader_Stop),
	    HLL_EXPORT(GetReadSize, HTTPDownloader_GetReadSize),
	    HLL_EXPORT(ReadAllString, HTTPDownloader_ReadString),
	    HLL_EXPORT(ReadByte, HTTPDownloader_ReadByte),
	    HLL_EXPORT(ReadInt, HTTPDownloader_ReadInt),
	    HLL_EXPORT(ReadFloat, HTTPDownloader_ReadFloat),
	    HLL_EXPORT(ReadString, HTTPDownloader_ReadString),
	    HLL_EXPORT(ReadStringUTF8ToSJIS, HTTPDownloader_ReadString),
	    HLL_EXPORT(UTF8ToSJIS, HTTPDownloader_ConvertString),
	    HLL_EXPORT(SJISToUTF8, HTTPDownloader_ConvertString),
	    HLL_EXPORT(EncodeString, HTTPDownloader_ConvertString),
	    HLL_EXPORT(DecodeString, HTTPDownloader_ConvertString),
	    HLL_TODO_EXPORT(IsRun, HTTPDownloader_IsRun),
	    HLL_TODO_EXPORT(Stop, HTTPDownloader_Stop),
	    HLL_TODO_EXPORT(GetReadSize, HTTPDownloader_GetReadSize),
	    HLL_TODO_EXPORT(ReadString, HTTPDownloader_ReadString),
	    HLL_TODO_EXPORT(ReadStringUTF8ToSJIS, HTTPDownloader_ReadStringUTF8ToSJIS)
	    );
