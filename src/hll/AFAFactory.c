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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "xsystem4.h"
#include "hll.h"

struct title_index {
	bool ready;
	char **titles;
	int count, capacity;
	int *matches, match_count;
};
static struct title_index indices[ASSET_TYPE_MAX];

static void clear_index(struct title_index *index)
{
	for (int i = 0; i < index->count; i++) free(index->titles[i]);
	free(index->titles);
	free(index->matches);
	memset(index, 0, sizeof(*index));
}

static void AFAFactory_ModuleFini(void)
{
	for (int i = 0; i < ASSET_TYPE_MAX; i++) clear_index(&indices[i]);
}

static int get_asset_type(int type)
{
	switch (type) {
	case 2: return ASSET_CG;
	case 3: return ASSET_FLAT;
	case 5: return ASSET_SOUND;
	default: return -1;
	}
}

static bool AFAFactory_IsExistArchive(struct string *name)
{
	if (!name || !name->size || !name->text[0])
		return false;
	char *path = gamedir_path_icase(name->text);
	bool exists = path && file_exists(path);
	free(path);
	if (exists) return true;
	struct string *filename = string_dup(name);
	string_append_cstr(&filename, ".afa", 4);
	path = gamedir_path_icase(filename->text);
	exists = path && file_exists(path);
	free(path);
	free_string(filename);
	return exists;
}

bool AFAFactory_LoadArchive(int type, struct string *archive_name)
{
	int t = get_asset_type(type);
	if (t < 0 || t >= ASSET_TYPE_MAX) {
		WARNING("Unknown asset type: %d", type);
		return false;
	}
	if (!archive_name || !archive_name->size || !archive_name->text[0])
		return false;
	bool loaded = asset_manager_load_archive(t, archive_name->text);
	if (loaded) clear_index(&indices[t]);
	return loaded;
}

static char *normalize_title(const char *name)
{
	char *title = archive_basename(name);
	for (unsigned char *p = (unsigned char *)title; *p; p++) {
		if (SJIS_2BYTE(*p) && p[1]) p++;
		else if (*p < 128) *p = tolower(*p);
	}
	return title;
}

static void collect_title(struct archive_data *data, void *user)
{
	struct title_index *index = user;
	if (index->count == index->capacity) {
		index->capacity = index->capacity ? index->capacity * 2 : 128;
		index->titles = xrealloc(index->titles, index->capacity * sizeof(*index->titles));
	}
	index->titles[index->count++] = normalize_title(data->name);
}

static int compare_title(const void *a, const void *b)
{
	const signed char *pa = *(const signed char *const *)a;
	const signed char *pb = *(const signed char *const *)b;
	while (*pa && *pa == *pb) {
		pa++;
		pb++;
	}
	return (int)*pb - (int)*pa;
}

static struct title_index *title_index(int type)
{
	int t = get_asset_type(type);
	if (t < 0 || t >= ASSET_TYPE_MAX) return NULL;
	struct title_index *index = &indices[t];
	if (index->ready) return index;
	asset_foreach_file(t, collect_title, index);
	if (index->count > 1) qsort(index->titles, index->count, sizeof(*index->titles), compare_title);
	int count = 0;
	for (int i = 0; i < index->count; i++) {
		if (count && !strcmp(index->titles[count - 1], index->titles[i])) free(index->titles[i]);
		else index->titles[count++] = index->titles[i];
	}
	index->count = count;
	index->ready = true;
	return index;
}

static int AFAFactory_GetCountOfData(int type)
{
	struct title_index *index = title_index(type);
	return index ? index->count : 0;
}

static struct string *AFAFactory_GetTitleByIndex_v12(int type, int at)
{
	struct title_index *index = title_index(type);
	return index && at >= 0 && at < index->count
		? cstr_to_string(index->titles[at]) : string_ref(&EMPTY_STRING);
}

static int AFAFactory_SearchTitle(int type, struct string *name)
{
	struct title_index *index = title_index(type);
	if (!index) return 0;
	index->match_count = 0;
	if (!name) return 0;
	char *query = normalize_title(name->text);
	index->matches = xrealloc(index->matches, max(1, index->count) * sizeof(*index->matches));
	// v12 uses substring matching for all three search entry points,
	// including PrefixSearchTitle and SuffixSearchTitle (reference verified).
	for (int i = 0; i < index->count; i++)
		if (strstr(index->titles[i], query)) index->matches[index->match_count++] = i;
	free(query);
	return index->match_count;
}

static int AFAFactory_GetCountOfSearchData(int type)
{
	struct title_index *index = title_index(type);
	return index ? index->match_count : 0;
}

static struct string *AFAFactory_GetSearchTitleByIndex_v12(int type, int at)
{
	struct title_index *index = title_index(type);
	return index && at >= 0 && at < index->match_count
		? cstr_to_string(index->titles[index->matches[at]]) : string_ref(&EMPTY_STRING);
}

static void AFAFactory_GetTitleByIndex(int type, int at, struct string **out)
{
	if (!out)
		return;
	if (*out)
		free_string(*out);
	*out = AFAFactory_GetTitleByIndex_v12(type, at);
}

static void AFAFactory_GetSearchTitleByIndex(int type, int at, struct string **out)
{
	if (!out)
		return;
	if (*out)
		free_string(*out);
	*out = AFAFactory_GetSearchTitleByIndex_v12(type, at);
}

static void AFAFactory_GetSearchTitleList(int type, struct string *name, struct page **out)
{
	if (!out)
		return;
	int count = AFAFactory_SearchTitle(type, name);
	struct ain_type str_type = { .data = AIN_STRING, .struc = -1 };
	struct page *page = v12_array_new(&str_type, count, false);
	for (int i = 0; i < count; i++) {
		int slot = page->values[i].i;
		free_string(heap[slot].s);
		heap[slot].s = AFAFactory_GetSearchTitleByIndex_v12(type, i);
	}
	if (*out) {
		delete_page_vars(*out);
		free_page(*out);
	}
	*out = page;
}

static void AFAFactory_PreLink(void);

HLL_LIBRARY(AFAFactory,
	    HLL_EXPORT(_PreLink, AFAFactory_PreLink),
	    HLL_EXPORT(_ModuleFini, AFAFactory_ModuleFini),
	    HLL_EXPORT(IsExistArchive, AFAFactory_IsExistArchive),
	    HLL_EXPORT(LoadArchive, AFAFactory_LoadArchive),
	    HLL_EXPORT(GetCountOfData, AFAFactory_GetCountOfData),
	    HLL_EXPORT(GetTitleByIndex, AFAFactory_GetTitleByIndex),
	    HLL_EXPORT(SearchTitle, AFAFactory_SearchTitle),
	    HLL_EXPORT(PrefixSearchTitle, AFAFactory_SearchTitle),
	    HLL_EXPORT(SuffixSearchTitle, AFAFactory_SearchTitle),
	    HLL_EXPORT(GetCountOfSearchData, AFAFactory_GetCountOfSearchData),
	    HLL_EXPORT(GetSearchTitleByIndex, AFAFactory_GetSearchTitleByIndex),
	    HLL_EXPORT(GetSearchTitleList, AFAFactory_GetSearchTitleList),
	    HLL_EXPORT(GetPrefixSearchTitleList, AFAFactory_GetSearchTitleList),
	    HLL_EXPORT(GetSuffixSearchTitleList, AFAFactory_GetSearchTitleList)
	    );

static bool afa_fun_returns_string(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 && ain->libraries[libno].functions[fno].return_type.data == AIN_STRING;
}

static void AFAFactory_PreLink(void)
{
	int libno = ain_get_library(ain, "AFAFactory");
	if (libno < 0)
		return;
	if (ain->version >= 12 || afa_fun_returns_string(libno, "GetTitleByIndex"))
		static_library_replace(&lib_AFAFactory, "GetTitleByIndex", AFAFactory_GetTitleByIndex_v12);
	if (ain->version >= 12 || afa_fun_returns_string(libno, "GetSearchTitleByIndex"))
		static_library_replace(&lib_AFAFactory, "GetSearchTitleByIndex", AFAFactory_GetSearchTitleByIndex_v12);
}
