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

#include "hll.h"

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
	    HLL_TODO_EXPORT(IsRun, HTTPDownloader_IsRun),
	    HLL_TODO_EXPORT(Stop, HTTPDownloader_Stop),
	    HLL_TODO_EXPORT(GetReadSize, HTTPDownloader_GetReadSize),
	    HLL_TODO_EXPORT(ReadString, HTTPDownloader_ReadString),
	    HLL_TODO_EXPORT(ReadStringUTF8ToSJIS, HTTPDownloader_ReadStringUTF8ToSJIS)
	    );
