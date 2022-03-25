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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"

#include "hll.h"
#include "savedata.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static FILE *current_file = NULL;
static char *file_contents = NULL;
static size_t file_cursor = 0;

static int File_Open(struct string *filename, int type)
{
	const char *mode;
	if (type == 1) {
		mode = "r";
	} else if (type == 2) {
		mode = "w";
	} else {
		WARNING("Unknown mode in File.Open: %d", type);
		return 0;
	}
	if (current_file) {
		WARNING("Previously opened file wasn't closed");
		fclose(current_file);
	}

	char *path = unix_path(filename->text);
	current_file = fopen(path, mode);
	if (!current_file) {
		WARNING("Failed to open file '%s': %s", path, strerror(errno));
	}

	free(path);
	return !!current_file;
}

static int File_Close(void)
{
	if (!current_file) {
		VM_ERROR("File.Close called, but no open file");
	}
	int r = !fclose(current_file);
	current_file = NULL;
	if (file_contents)
		free(file_contents);
	file_contents = NULL;
	file_cursor = 0;
	return r;
}

static int File_Read(struct page **_page)
{
	struct page *page = *_page;
	if (page->type != STRUCT_PAGE) {
		VM_ERROR("File.Read of non-struct");
	}

	if (!current_file) {
		VM_ERROR("File.Read called, but no open file");
	}

	// read file inton memory if needed
	if (!file_contents) {
		fseek(current_file, 0, SEEK_END);
		size_t file_size = ftell(current_file);
		fseek(current_file, 0, SEEK_SET);

		file_contents = xmalloc(file_size + 1);
		if (fread(file_contents, file_size, 1, current_file) != 1) {
			WARNING("File.Read failed (fread): %s", strerror(errno));
			free(file_contents);
			return 0;
		}
		file_contents[file_size] = '\0';
	}

	const char *end;
	cJSON *json = cJSON_ParseWithOpts(file_contents+file_cursor, &end, false);
	if (!json) {
		WARNING("File.Read failed to parse JSON");
		return 0;
	}
	if (!cJSON_IsArray(json)) {
		WARNING("File.Read incorrect type in parsed JSON");
		return 0;
	}

	file_cursor = end - file_contents;

	json_load_page(page, json, true);
	cJSON_Delete(json);
	return 1;
}

static int File_Write(struct page *page)
{
	if (!current_file) {
		VM_ERROR("File.Write called, but no open file");
		return 0;
	}

	cJSON *json = page_to_json(page);
	char *str = cJSON_Print(json);
	cJSON_Delete(json);

	if (fwrite(str, strlen(str), 1, current_file) != 1) {
		WARNING("File.Write failed (fwrite): %s", strerror(errno));
		free(str);
		return 0;
	}
	fflush(current_file);

	free(str);
	return 1;
}

/*

struct tagSaveDate {
    int m_nYear;
    int m_nMonth;
    int m_nDay;
    int m_nHour;
    int m_nMinute;
    int m_nSecond;
    int m_nDayOfWeek;
};

 */

static int File_GetTime(struct string *filename, struct page **page)
{
	struct stat s;
	char *path = unix_path(filename->text);
	union vm_value *date = (*page)->values;

	if (stat(path, &s) < 0) {
		WARNING("stat failed: %s", strerror(errno));
		free(path);
		return 0;
	}
	free(path);

	struct tm *tm = localtime(&s.st_mtime);
	if (!tm) {
		WARNING("localtime failed: %s", strerror(errno));
		return 0;
	}

	date[0].i = tm->tm_year + 1900;
	date[1].i = tm->tm_mon;
	date[2].i = tm->tm_mday;
	date[3].i = tm->tm_hour;
	date[4].i = tm->tm_min;
	date[5].i = tm->tm_sec;
	date[6].i = tm->tm_wday;

	return 1;
}

static int File_Delete(struct string *name)
{
	char *path = unix_path(name->text);
	if (remove(path)) {
		WARNING("remove: %s", strerror(errno));
		return 0;
	}
	free(path);
	return 1;
}

static int File_MakeDirectory(struct string *name)
{
	char *path = unix_path(name->text);
	if (mkdir_p(path)) {
		WARNING("mkdir_p: %s", strerror(errno));
	}
	free(path);
	return 1;
}

//int File_Copy(struct string *src, struct string *dst);
//int File_SetFind(struct string *name);
//int File_GetFind(struct string **name);

HLL_LIBRARY(File,
	    HLL_EXPORT(Open, File_Open),
	    HLL_EXPORT(Close, File_Close),
	    HLL_EXPORT(Read, File_Read),
	    HLL_EXPORT(Write, File_Write),
	    //HLL_EXPORT(Copy, File_Copy),
	    HLL_EXPORT(Delete, File_Delete),
	    HLL_EXPORT(GetTime, File_GetTime),
	    //HLL_EXPORT(SetFind, File_SetFind),
	    //HLL_EXPORT(GetFind, File_GetFind),
	    HLL_EXPORT(MakeDirectory, File_MakeDirectory));

