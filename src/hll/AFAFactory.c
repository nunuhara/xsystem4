/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

HLL_WARN_UNIMPLEMENTED(false, bool, AFAFactory, IsExistArchive, struct string *ArchiveName);
//bool AFAFactory_LoadArchive(int Type, struct string *ArchiveName);
//int AFAFactory_GetCountOfData(int Type);
//void AFAFactory_GetTitleByIndex(int Type, int Index, struct string **Name);
//int AFAFactory_SearchTitle(int Type, struct string *Name);
//int AFAFactory_PrefixSearchTitle(int Type, struct string *Name);
//int AFAFactory_SuffixSearchTitle(int Type, struct string *Name);
//int AFAFactory_GetCountOfSearchData(int Type);
//void AFAFactory_GetSearchTitleByIndex(int Type, int Index, struct string **Name);

HLL_LIBRARY(AFAFactory,
	    HLL_EXPORT(IsExistArchive, AFAFactory_IsExistArchive),
	    HLL_TODO_EXPORT(LoadArchive, AFAFactory_LoadArchive),
	    HLL_TODO_EXPORT(GetCountOfData, AFAFactory_GetCountOfData),
	    HLL_TODO_EXPORT(GetTitleByIndex, AFAFactory_GetTitleByIndex),
	    HLL_TODO_EXPORT(SearchTitle, AFAFactory_SearchTitle),
	    HLL_TODO_EXPORT(PrefixSearchTitle, AFAFactory_PrefixSearchTitle),
	    HLL_TODO_EXPORT(SuffixSearchTitle, AFAFactory_SuffixSearchTitle),
	    HLL_TODO_EXPORT(GetCountOfSearchData, AFAFactory_GetCountOfSearchData),
	    HLL_TODO_EXPORT(GetSearchTitleByIndex, AFAFactory_GetSearchTitleByIndex)
	    );
