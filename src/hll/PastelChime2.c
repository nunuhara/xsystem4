/* Copyright (C) 
 * 2021 kichikuou <KichikuouChrome@gmail.com>
 * 2022 nao1215 <n.chika156@gmail.com>
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

#include <stdio.h>
#include <string.h>

#include "system4/file.h"
#include "system4/string.h"

#include "hll.h"
#include "savedata.h"
#include "vm/page.h"
#include "xsystem4.h"

HLL_WARN_UNIMPLEMENTED(true, bool, PastelChime2, InitPastelChime2, void);
HLL_WARN_UNIMPLEMENTED(false, bool, PastelChime2, DungeonDataToSaveData, int nSaveDataIndex);
HLL_WARN_UNIMPLEMENTED(false, bool, PastelChime2, DungeonDataFromSaveData, int nSaveDataIndex);

static void PastelChime2_str_erase_found(struct string **s, struct string **key)
{
	char *src = (*s)->text;
	char *found = strstr(src, (*key)->text);
	if (!found)
		return;
	// TODO: Use string_alloc/string_realloc
	char *buf = xmalloc((*s)->size);
	char *dst = buf;
	do {
		strncpy(dst, src, found - src);
		dst += found - src;
		src = found + (*key)->size;
		found = strstr(src, (*key)->text);
	} while (found);
	strcpy(dst, src);

	free_string(*s);
	*s = make_string(buf, strlen(buf));
	free(buf);
}

static bool PastelChime2_SaveArray(struct string *file_name, struct page **page)
{
	cJSON *json = page_to_json(*page);
	char *str = cJSON_Print(json);
	cJSON_Delete(json);

	char *path = unix_path(file_name->text);
	bool ok = file_write(path, (uint8_t *)str, strlen(str));
	free(path);
	free(str);
	return ok;
}

static bool load_array(struct string *file_name, struct page **_page, enum ain_data_type data_type)
{
	char *path = unix_path(file_name->text);
	char *file_content = file_read(path, NULL);
	free(path);
	if (!file_content)
		return false;

	cJSON *json = cJSON_Parse(file_content);
	free(file_content);
	if (!cJSON_IsArray(json)) {
		cJSON_Delete(json);
		return false;
	}

	union vm_value dim = { .i = cJSON_GetArraySize(json) };
	if (!*_page)
		*_page = alloc_array(1, &dim, data_type, 0, false);
	struct page *page = *_page;

	json_load_page(page, json, false);
	cJSON_Delete(json);
	return true;
}

static bool PastelChime2_LoadArray(struct string *file_name, struct page **page)
{
	return load_array(file_name, page, AIN_ARRAY_INT);
}

static bool PastelChime2_LoadStringArray(struct string *file_name, struct page **page)
{
	return load_array(file_name, page, AIN_ARRAY_STRING);
}

static bool PastelChime2_File_Delete(struct string *file_name)
{
	char *path = unix_path(file_name->text);
	int r = remove(path);
	free(path);
	return r == 0;
}

HLL_LIBRARY(PastelChime2,
	HLL_EXPORT(InitPastelChime2, PastelChime2_InitPastelChime2),
	HLL_EXPORT(SaveArray, PastelChime2_SaveArray),
    HLL_EXPORT(SaveStringArray, PastelChime2_SaveArray),
	HLL_EXPORT(LoadArray, PastelChime2_LoadArray),
	HLL_EXPORT(LoadStringArray, PastelChime2_LoadStringArray),
    HLL_EXPORT(File_Delete, PastelChime2_File_Delete),
    HLL_EXPORT(str_erase_found, PastelChime2_str_erase_found),
	HLL_EXPORT(DungeonDataToSaveData, PastelChime2_DungeonDataToSaveData),
	HLL_EXPORT(DungeonDataFromSaveData, PastelChime2_DungeonDataFromSaveData));