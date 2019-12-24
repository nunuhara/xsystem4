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
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "hll.h"
#include "../file.h"
#include "../heap.h"
#include "../savedata.h"
#include "../system4.h"

static FILE *current_file = NULL;
static char *file_contents = NULL;
static size_t file_size = 0;
static size_t file_cursor = 0;

//int Open(string szName, int nType)
hll_defun(Open, args)
{
	const char *mode;
	if (args[1].i == 1) {
		mode = "r";
	} else if (args[1].i == 2) {
		mode = "w";
	} else {
		WARNING("Unknown mode in File.Open: %d", args[1].i);
		hll_return(0);
	}
	if (current_file) {
		WARNING("Previously opened file wasn't closed");
		fclose(current_file);
	}

	char *path = unix_path(hll_string_ref(args[0].i)->text);
	current_file = fopen(path, mode);
	if (!current_file) {
		WARNING("Failed to open file '%s': %s", path, strerror(errno));
	}

	free(path);
	hll_return(!!current_file);
}

//int Close()
hll_defun(Close, args)
{
	if (!current_file) {
		VM_ERROR("File.Close called, but no open file");
	}
	int r = !fclose(current_file);
	current_file = NULL;
	file_contents = NULL;
	file_size = 0;
	file_cursor = 0;
	hll_return(r);
}

//int Read(ref struct pIVMStruct)
hll_defun(Read, args)
{
	struct page *page = heap_get_page(args[0].i);
	if (page->type != STRUCT_PAGE) {
		VM_ERROR("File.Read of non-struct");
	}

	if (!current_file) {
		VM_ERROR("File.Read called, but no open file");
	}

	// read file inton memory if needed
	if (!file_contents) {
		fseek(current_file, 0, SEEK_END);
		file_size = ftell(current_file);
		fseek(current_file, 0, SEEK_SET);

		file_contents = xmalloc(file_size + 1);
		if (fread(file_contents, file_size, 1, current_file) != 1) {
			WARNING("File.Read failed (fread): %s", strerror(errno));
			free(file_contents);
			hll_return(0);
		}
		file_contents[file_size] = '\0';
	}

	const char *end;
	cJSON *json = cJSON_ParseWithOpts(file_contents+file_cursor, &end, false);
	if (!json) {
		WARNING("File.Read failed to parse JSON");
		hll_return(0);
	}
	if (!cJSON_IsArray(json)) {
		WARNING("File.Read incorrect type in parsed JSON");
		hll_return(0);
	}

	file_cursor = end - file_contents;

	json_load_page(page, json);
	cJSON_Delete(json);
	hll_return(0);
}

//int Write(struct pIVMStruct)
hll_defun(Write, args)
{
	if (!current_file) {
		VM_ERROR("File.Write called, but no open file");
		hll_return(0);
	}

	cJSON *json = vm_value_to_json(AIN_STRUCT, args[0]);
	char *str = cJSON_Print(json);
	cJSON_Delete(json);

	if (fwrite(str, strlen(str), 1, current_file) != 1) {
		WARNING("File.Write failed (fwrite): %s", strerror(errno));
		free(str);
		hll_return(0);
	}
	fflush(current_file);

	free(str);
	hll_return(1);
}

//int Copy(string szSrcName, string szDstName)
hll_unimplemented(File, Copy);

//int Delete(string szName)
hll_unimplemented(File, Delete);

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

//int GetTime(string szName, ref struct pIVMStruct)
hll_defun(GetTime, args)
{
	struct stat s;
	char *path = unix_path(hll_string_ref(args[0].i)->text);
	union vm_value *date = hll_struct_ref(args[1].i);

	if (stat(path, &s) < 0) {
		WARNING("stat failed: %s", strerror(errno));
		hll_return(0);
	}

	struct tm *tm = localtime(&s.st_mtime);
	if (!tm) {
		WARNING("localtime failed: %s", strerror(errno));
		hll_return(0);
	}

	date[0].i = tm->tm_year;
	date[1].i = tm->tm_mon;
	date[2].i = tm->tm_mday;
	date[3].i = tm->tm_hour;
	date[4].i = tm->tm_min;
	date[5].i = tm->tm_sec;
	date[6].i = tm->tm_wday;

	hll_return(1);
}

//int SetFind(string szName)
hll_unimplemented(File, SetFind);
//int GetFind(ref string szName)
hll_unimplemented(File, GetFind);
//int MakeDirectory(string szName)
hll_unimplemented(File, MakeDirectory);

hll_deflib(File) {
	hll_export(Open),
	hll_export(Close),
	hll_export(Read),
	hll_export(Write),
	hll_export(Copy),
	hll_export(Delete),
	hll_export(GetTime),
	hll_export(SetFind),
	hll_export(GetFind),
	hll_export(MakeDirectory),
	NULL
};
