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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"

static bool FileOperation_ExistFile(struct string *file_name)
{
	char *path = unix_path(file_name->text);
	ustat s;
	bool result = stat_utf8(path, &s) == 0 && S_ISREG(s.st_mode);
	free(path);
	return result;
}

//bool FileOperation_DeleteFile(string FileName);

static bool FileOperation_CopyFile(struct string *dest_file_name, struct string *src_file_name)
{
	char *dest = unix_path(dest_file_name->text);
	char *src = unix_path(src_file_name->text);
	bool result = file_copy(src, dest);
	free(src);
	free(dest);
	return result;
}

//bool FileOperation_GetFileCreationTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);
//bool FileOperation_GetFileLastAccessTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);
//bool FileOperation_GetFileLastWriteTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);

static bool FileOperation_GetFileSize(struct string *file_name, int *size)
{
	char *path = unix_path(file_name->text);
	ustat s;
	if (stat_utf8(path, &s) < 0) {
		WARNING("stat(\"%s\"): %s", display_utf0(path), strerror(errno));
		free(path);
		return false;
	}
	if (!S_ISREG(s.st_mode)) {
		WARNING("stat(\"%s\"): not a regular file", display_utf0(path));
		free(path);
		return false;
	}
	*size = s.st_size;
	free(path);
	return true;
}

static bool FileOperation_ExistFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = is_directory(path);
	free(path);
	return result;
}

static bool FileOperation_CreateFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = mkdir_p(path) == 0;
	if (!result)
		WARNING("mkdir_p(%s): %s", path, strerror(errno));
	free(path);
	return result;
}

static bool rmtree(const char *path)
{
	UDIR *d = opendir_utf8(path);
	if (!d) {
		WARNING("opendir(\"%s\"): %s", display_utf0(path), strerror(errno));
		return false;
	}
	bool ok = true;
	char *d_name;
	while (ok && (d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.' && (d_name[1] == '\0' || (d_name[1] == '.' && d_name[2] == '\0'))) {
			free(d_name);
			continue;
		}

		char *utf8_path = path_join(path, d_name);
		ustat s;
		if (stat_utf8(utf8_path, &s) < 0) {
			WARNING("stat(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
			ok = false;
		} else {
			if (S_ISDIR(s.st_mode)) {
				if (!rmtree(utf8_path))
					ok = false;
			} else {
				if (remove_utf8(utf8_path) < 0) {
					WARNING("remove(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
					ok = false;
				}
			}
		}
		free(utf8_path);
		free(d_name);
	}
	closedir_utf8(d);
	if (ok) {
		if (rmdir_utf8(path) < 0) {
			WARNING("rmdir(\"%s\"): %s", display_utf0(path), strerror(errno));
			ok = false;
		}
	}
	return ok;
}

static bool FileOperation_DeleteFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = rmtree(path);
	free(path);
	return result;
}

static bool get_file_list(struct string *folder_name, struct page **out, bool folders)
{
	char *dir_name = unix_path(folder_name->text);

	UDIR *d = opendir_utf8(dir_name);
	if (!d) {
		WARNING("opendir(\"%s\"): %s", display_utf0(dir_name), strerror(errno));
		free(dir_name);
		return false;
	}

	struct string **names = NULL;
	int nr_names = 0;

	char *d_name;
	while ((d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.') {
			free(d_name);
			continue;
		}

		char *utf8_path = path_join(dir_name, d_name);
		ustat s;
		if (stat_utf8(utf8_path, &s) < 0) {
			WARNING("stat(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
			goto loop_next;
		}
		if (folders) {
			if (!S_ISDIR(s.st_mode))
				goto loop_next;
		} else {
			if (!S_ISREG(s.st_mode))
				goto loop_next;
		}

		char *sjis_name = utf2sjis(d_name, 0);
		names = xrealloc_array(names, nr_names, nr_names+1, sizeof(struct string*));
		names[nr_names++] = cstr_to_string(sjis_name);
		free(sjis_name);
	loop_next:
		free(utf8_path);
		free(d_name);
	}
	closedir_utf8(d);
	free(dir_name);

	union vm_value dim = { .i = nr_names };
	struct page *page = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < nr_names; i++) {
		page->values[i].i = heap_alloc_string(names[i]);
	}
	free(names);

	if (*out) {
		delete_page_vars(*out);
		free_page(*out);
	}
	*out = page;
	return true;
}

static bool FileOperation_GetFileList(struct string *folder_name, struct page **out)
{
	return get_file_list(folder_name, out, false);
}

static bool FileOperation_GetFolderList(struct string *folder_name, struct page **out)
{
	return get_file_list(folder_name, out, true);
}

HLL_LIBRARY(FileOperation,
	    HLL_EXPORT(ExistFile, FileOperation_ExistFile),
	    HLL_TODO_EXPORT(DeleteFile, FileOperation_DeleteFile),
	    HLL_EXPORT(CopyFile, FileOperation_CopyFile),
	    HLL_TODO_EXPORT(GetFileCreationTime, FileOperation_GetFileCreationTime),
	    HLL_TODO_EXPORT(GetFileLastAccessTime, FileOperation_GetFileLastAccessTime),
	    HLL_TODO_EXPORT(GetFileLastWriteTime, FileOperation_GetFileLastWriteTime),
	    HLL_EXPORT(GetFileSize, FileOperation_GetFileSize),
	    HLL_EXPORT(ExistFolder, FileOperation_ExistFolder),
	    HLL_EXPORT(CreateFolder, FileOperation_CreateFolder),
	    HLL_EXPORT(DeleteFolder, FileOperation_DeleteFolder),
	    HLL_EXPORT(GetFileList, FileOperation_GetFileList),
	    HLL_EXPORT(GetFolderList, FileOperation_GetFolderList));
