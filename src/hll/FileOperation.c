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
#include <time.h>

#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "hll.h"

static char *fileop_resolve_path(const char *sjis_path);

static bool FileOperation_ExistFile(struct string *file_name)
{
	if (!file_name)
		return false;
	char *path = fileop_resolve_path(file_name->text);
	ustat s;
	bool result = path && stat_utf8(path, &s) == 0 && S_ISREG(s.st_mode);
	free(path);
	return result;
}

static char *fileop_resolve_path(const char *sjis_path)
{
	if (!sjis_path)
		return NULL;
	char *path = unix_path(sjis_path);
	ustat s;
	if (path && stat_utf8(path, &s) == 0)
		return path;
	if (config.game_dir) {
		char *gd_path = gamedir_path_icase(sjis_path);
		if (gd_path && stat_utf8(gd_path, &s) == 0) {
			free(path);
			return gd_path;
		}
		free(gd_path);
	}
	return path;
}

static bool FileOperation_DeleteFile(struct string *file_name)
{
	if (!file_name)
		return false;
	char *path = fileop_resolve_path(file_name->text);
	bool result = path && remove_utf8(path) == 0;
	free(path);
	return result;
}

static char *fileop_resolve_write_path(const char *sjis_path)
{
	if (!sjis_path)
		return NULL;
	if (!config.game_dir)
		return unix_path(sjis_path);

	char *existing = gamedir_path_icase(sjis_path);
	if (existing)
		return existing;

	/* Walk SJIS-safe path separators from longest parent prefix to shortest
	 * so writes into existing differently-cased folders reuse them on Linux. */
	size_t len = strlen(sjis_path);
	size_t *seps = NULL;
	size_t nr_seps = 0;
	for (size_t i = 0; i < len; ) {
		if (SJIS_2BYTE(sjis_path[i]) && sjis_path[i + 1] != '\0') {
			i += 2;
			continue;
		}
		if ((sjis_path[i] == '/' || sjis_path[i] == '\\') && i > 0) {
			seps = xrealloc(seps, (nr_seps + 1) * sizeof(*seps));
			seps[nr_seps++] = i;
		}
		i++;
	}

	for (size_t k = nr_seps; k > 0; k--) {
		size_t sep_idx = seps[k - 1];
		char *parent_sjis = xmalloc(sep_idx + 1);
		memcpy(parent_sjis, sjis_path, sep_idx);
		parent_sjis[sep_idx] = '\0';
		char *parent_dir = gamedir_path_icase(parent_sjis);
		free(parent_sjis);
		if (parent_dir && is_directory(parent_dir)) {
			free(seps);
			const char *rest = sjis_path + sep_idx + 1;
			while (*rest == '/' || *rest == '\\')
				rest++;
			if (!*rest)
				return parent_dir;
			char *tail_utf8 = unix_path(rest);
			char *joined = path_join(parent_dir, tail_utf8);
			free(tail_utf8);
			free(parent_dir);
			return joined;
		}
		free(parent_dir);
	}
	free(seps);
	return gamedir_path(sjis_path);
}

static bool FileOperation_CopyFile(struct string *dest_file_name, struct string *src_file_name)
{
	if (!dest_file_name || !src_file_name)
		return false;
	char *dest = fileop_resolve_write_path(dest_file_name->text);
	char *src = fileop_resolve_path(src_file_name->text);
	if (dest) {
		char *dest_copy = xstrdup(dest);
		char *dir = path_dirname(dest_copy);
		if (dir && dir[0])
			mkdir_p(dir);
		free(dest_copy);
	}
	bool result = src && dest && file_copy(src, dest);
	free(src);
	free(dest);
	return result;
}

//bool FileOperation_GetFileCreationTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);
//bool FileOperation_GetFileLastAccessTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);
static bool fileop_time_to_fields(time_t t, int *year, int *month, int *day,
		int *week, int *hour, int *min, int *second)
{
	struct tm *tm = localtime(&t);
	if (!tm) {
		*year = *month = *day = *week = *hour = *min = *second = 0;
		return false;
	}
	*year = tm->tm_year + 1900;
	*month = tm->tm_mon + 1;
	*day = tm->tm_mday;
	*week = tm->tm_wday;
	*hour = tm->tm_hour;
	*min = tm->tm_min;
	*second = tm->tm_sec;
	return true;
}

static bool FileOperation_GetFileCreationTime(struct string *file_name,
		int *year, int *month, int *day, int *week, int *hour, int *min, int *second)
{
	char *path = file_name ? fileop_resolve_path(file_name->text) : NULL;
	ustat s;
	if (!path || stat_utf8(path, &s) < 0 || !S_ISREG(s.st_mode)) {
		free(path);
		*year = *month = *day = *week = *hour = *min = *second = 0;
		return false;
	}
	free(path);
	return fileop_time_to_fields(s.st_ctime, year, month, day, week, hour, min, second);
}

static bool FileOperation_GetFileLastAccessTime(struct string *file_name,
		int *year, int *month, int *day, int *week, int *hour, int *min, int *second)
{
	char *path = file_name ? fileop_resolve_path(file_name->text) : NULL;
	ustat s;
	if (!path || stat_utf8(path, &s) < 0 || !S_ISREG(s.st_mode)) {
		free(path);
		*year = *month = *day = *week = *hour = *min = *second = 0;
		return false;
	}
	free(path);
	return fileop_time_to_fields(s.st_atime, year, month, day, week, hour, min, second);
}

static bool FileOperation_GetFileLastWriteTime(struct string *file_name,
		int *year, int *month, int *day, int *week, int *hour, int *min, int *second)
{
	char *path = file_name ? fileop_resolve_path(file_name->text) : NULL;
	ustat s;
	if (!path || stat_utf8(path, &s) < 0 || !S_ISREG(s.st_mode)) {
		free(path);
		*year = *month = *day = *week = *hour = *min = *second = 0;
		return false;
	}
	free(path);
	return fileop_time_to_fields(s.st_mtime, year, month, day, week, hour, min, second);
}

static bool FileOperation_GetFileSize(struct string *file_name, int *size)
{
	if (!file_name || !size)
		return false;
	char *path = fileop_resolve_path(file_name->text);
	ustat s;
	if (!path || stat_utf8(path, &s) < 0) {
		WARNING("stat(\"%s\"): %s", display_utf0(path ? path : ""), strerror(errno));
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
	if (!folder_name)
		return false;
	char *path = fileop_resolve_path(folder_name->text);
	bool result = path && is_directory(path);
	free(path);
	return result;
}

static bool FileOperation_CreateFolder(struct string *folder_name)
{
	if (!folder_name)
		return false;
	char *path = fileop_resolve_write_path(folder_name->text);
	bool result = path && mkdir_p(path) == 0;
	if (!result && path)
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
	if (!folder_name)
		return false;
	char *path = fileop_resolve_path(folder_name->text);
	bool result = path && rmtree(path);
	free(path);
	return result;
}

static bool copytree(const char *src_dir, const char *dst_dir)
{
	if (mkdir_p(dst_dir) != 0)
		return false;
	UDIR *d = opendir_utf8(src_dir);
	if (!d)
		return false;
	bool ok = true;
	char *d_name;
	while (ok && (d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.' && (d_name[1] == '\0' || (d_name[1] == '.' && d_name[2] == '\0'))) {
			free(d_name);
			continue;
		}
		char *src_path = path_join(src_dir, d_name);
		char *dst_path = path_join(dst_dir, d_name);
		ustat s;
		if (stat_utf8(src_path, &s) < 0) {
			ok = false;
		} else if (S_ISDIR(s.st_mode)) {
			ok = copytree(src_path, dst_path);
		} else if (S_ISREG(s.st_mode)) {
			ok = file_copy(src_path, dst_path);
		}
		free(src_path);
		free(dst_path);
		free(d_name);
	}
	closedir_utf8(d);
	return ok;
}

static bool FileOperation_CopyFolder(struct string *dest_folder_name, struct string *src_folder_name)
{
	if (!dest_folder_name || !src_folder_name)
		return false;
	char *src = fileop_resolve_path(src_folder_name->text);
	char *dst = fileop_resolve_write_path(dest_folder_name->text);
	bool ok = src && dst && is_directory(src) && copytree(src, dst);
	free(src);
	free(dst);
	return ok;
}

static void collect_files_recursive(const char *dir_utf8, const char *rel_prefix, int depth,
		struct string ***names, int *nr_names)
{
	if (depth > 32)
		return;
	UDIR *d = opendir_utf8(dir_utf8);
	if (!d)
		return;
	char *d_name;
	while ((d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.') {
			free(d_name);
			continue;
		}
		char *utf8_path = path_join(dir_utf8, d_name);
		ustat s;
		if (stat_utf8(utf8_path, &s) < 0) {
			WARNING("stat(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
			free(utf8_path);
			free(d_name);
			continue;
		}
		char *sjis_comp = utf2filename_cjk(d_name, 0);
		char *rel_sjis;
		if (rel_prefix && rel_prefix[0]) {
			size_t plen = strlen(rel_prefix);
			size_t clen = strlen(sjis_comp);
			rel_sjis = xmalloc(plen + 1 + clen + 1);
			memcpy(rel_sjis, rel_prefix, plen);
			rel_sjis[plen] = '\\';
			memcpy(rel_sjis + plen + 1, sjis_comp, clen + 1);
		} else {
			rel_sjis = xstrdup(sjis_comp);
		}
		free(sjis_comp);
		if (S_ISDIR(s.st_mode)) {
			collect_files_recursive(utf8_path, rel_sjis, depth + 1, names, nr_names);
		} else if (S_ISREG(s.st_mode)) {
			*names = xrealloc_array(*names, *nr_names, *nr_names + 1, sizeof(struct string *));
			(*names)[(*nr_names)++] = cstr_to_string(rel_sjis);
		}
		free(rel_sjis);
		free(utf8_path);
		free(d_name);
	}
	closedir_utf8(d);
}

static bool get_file_list(struct string *folder_name, struct page **out, bool folders, bool recursive)
{
	if (!folder_name || !out)
		return false;
	char *dir_name = fileop_resolve_path(folder_name->text);
	if (!dir_name) return false;

	struct string **names = NULL;
	int nr_names = 0;

	if (recursive && !folders) {
		if (!is_directory(dir_name)) {
			free(dir_name);
			return false;
		}
		collect_files_recursive(dir_name, "", 0, &names, &nr_names);
		free(dir_name);
	} else {
		UDIR *d = opendir_utf8(dir_name);
		if (!d) {
			if (errno != ENOENT)
				WARNING("opendir(\"%s\"): %s", display_utf0(dir_name), strerror(errno));
			free(dir_name);
			return false;
		}

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

			char *sjis_name = utf2filename_cjk(d_name, 0);
			names = xrealloc_array(names, nr_names, nr_names+1, sizeof(struct string*));
			names[nr_names++] = cstr_to_string(sjis_name);
			free(sjis_name);
		loop_next:
			free(utf8_path);
			free(d_name);
		}
		closedir_utf8(d);
		free(dir_name);
	}

	union vm_value dim = { .i = nr_names };
	struct page *page = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < nr_names; i++) {
		int slot = page->values[i].i;
		free_string(heap[slot].s);
		heap[slot].s = names[i];
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
	return get_file_list(folder_name, out, false, false);
}

static bool FileOperation_GetFileListWithSubFolder(struct string *folder_name, struct page **out)
{
	return get_file_list(folder_name, out, false, true);
}

static bool FileOperation_GetFolderList(struct string *folder_name, struct page **out)
{
	return get_file_list(folder_name, out, true, false);
}

static struct page *FileOperation_GetFileList_v12(struct string *folder_name)
{
	struct page *out = NULL;
	if (!get_file_list(folder_name, &out, false, false))
		out = v12_array_new(&(struct ain_type){ .data = AIN_STRING }, 0, false);
	return out;
}

static struct page *FileOperation_GetFileListWithSubFolder_v12(struct string *folder_name)
{
	struct page *out = NULL;
	if (!get_file_list(folder_name, &out, false, true))
		out = v12_array_new(&(struct ain_type){ .data = AIN_STRING }, 0, false);
	return out;
}

static struct page *FileOperation_GetFolderList_v12(struct string *folder_name)
{
	struct page *out = NULL;
	if (!get_file_list(folder_name, &out, true, false))
		out = v12_array_new(&(struct ain_type){ .data = AIN_STRING }, 0, false);
	return out;
}

static void *FileOperation_ResolveList(const struct ain_hll_function *function)
{
	bool folders = !strcmp(function->name, "GetFolderList");
	bool recursive = !strcmp(function->name, "GetFileListWithSubFolder");
	if (function->nr_arguments == 1 && function->return_type.data == AIN_ARRAY) {
		if (folders)
			return FileOperation_GetFolderList_v12;
		return recursive ? FileOperation_GetFileListWithSubFolder_v12 : FileOperation_GetFileList_v12;
	}
	if (function->nr_arguments == 2) {
		if (folders)
			return FileOperation_GetFolderList;
		return recursive ? FileOperation_GetFileListWithSubFolder : FileOperation_GetFileList;
	}
	return NULL;
}

static bool FileOperation_OpenFolder(struct string *folder_name)
{
	return FileOperation_ExistFolder(folder_name);
}

HLL_LIBRARY(FileOperation,
	    HLL_EXPORT(ExistFile, FileOperation_ExistFile),
	    HLL_EXPORT(DeleteFile, FileOperation_DeleteFile),
	    HLL_EXPORT(CopyFile, FileOperation_CopyFile),
	    HLL_EXPORT(CopyFolder, FileOperation_CopyFolder),
	    HLL_EXPORT(GetFileCreationTime, FileOperation_GetFileCreationTime),
	    HLL_EXPORT(GetFileLastAccessTime, FileOperation_GetFileLastAccessTime),
	    HLL_EXPORT(GetFileLastWriteTime, FileOperation_GetFileLastWriteTime),
	    HLL_EXPORT(GetFileSize, FileOperation_GetFileSize),
	    HLL_EXPORT(ExistFolder, FileOperation_ExistFolder),
	    HLL_EXPORT(CreateFolder, FileOperation_CreateFolder),
	    HLL_EXPORT(DeleteFolder, FileOperation_DeleteFolder),
	    HLL_EXPORT(OpenFolder, FileOperation_OpenFolder),
	    HLL_OVERLOAD(GetFileList, FileOperation_ResolveList),
	    HLL_OVERLOAD(GetFileListWithSubFolder, FileOperation_ResolveList),
	    HLL_OVERLOAD(GetFolderList, FileOperation_ResolveList));

static char *clipboard_buf = NULL;

static void Clipboard_ModuleFini(void)
{
	free(clipboard_buf);
	clipboard_buf = NULL;
}

static void Clipboard_SetText(struct string *text)
{
	free(clipboard_buf);
	clipboard_buf = xstrdup(text ? text->text : "");
}

static struct string *Clipboard_GetText(void)
{
	return cstr_to_string(clipboard_buf ? clipboard_buf : "");
}

static bool Clipboard_GetText_ref(struct string **text)
{
	if (!text)
		return false;
	if (*text)
		free_string(*text);
	*text = cstr_to_string(clipboard_buf ? clipboard_buf : "");
	return true;
}

static void *Clipboard_ResolveGetText(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 1 && f->arguments[0].type.data == AIN_REF_STRING
			&& (f->return_type.data == AIN_VOID || f->return_type.data == AIN_BOOL))
		return Clipboard_GetText_ref;
	if (f->nr_arguments == 0 && f->return_type.data == AIN_STRING)
		return Clipboard_GetText;
	return NULL;
}

HLL_LIBRARY(Clipboard,
	    HLL_EXPORT(_ModuleFini, Clipboard_ModuleFini),
	    HLL_EXPORT(SetText, Clipboard_SetText),
	    HLL_OVERLOAD(GetText, Clipboard_ResolveGetText));

static FILE *text_file_fp = NULL;

struct text_file_entry {
	bool used;
	bool is_writer;
	FILE *fp;
	char *buf;
	size_t len;
	size_t pos;
};

static struct text_file_entry *tf_entries = NULL;
static int nr_tf_entries = 0;

static int alloc_tf_handle(void)
{
	for (int i = 1; i < nr_tf_entries; i++) {
		if (!tf_entries[i].used) {
			memset(&tf_entries[i], 0, sizeof(tf_entries[i]));
			tf_entries[i].used = true;
			return i;
		}
	}
	int next = nr_tf_entries ? nr_tf_entries * 2 : 16;
	tf_entries = xrealloc(tf_entries, next * sizeof(*tf_entries));
	memset(tf_entries + nr_tf_entries, 0, (next - nr_tf_entries) * sizeof(*tf_entries));
	int idx = nr_tf_entries ? nr_tf_entries : 1;
	nr_tf_entries = next;
	tf_entries[idx].used = true;
	return idx;
}

static bool is_valid_utf8_multibyte(const uint8_t *s, size_t len)
{
	bool has_multibyte = false;
	size_t i = 0;
	while (i < len) {
		uint8_t c = s[i];
		if (c < 0x80) {
			i++;
		} else if (c >= 0xC2 && c <= 0xDF) {
			if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80)
				return false;
			has_multibyte = true;
			i += 2;
		} else if (c >= 0xE0 && c <= 0xEF) {
			if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80)
				return false;
			if (c == 0xE0 && s[i + 1] < 0xA0)
				return false;
			if (c == 0xED && s[i + 1] >= 0xA0)
				return false;
			has_multibyte = true;
			i += 3;
		} else if (c >= 0xF0 && c <= 0xF4) {
			if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80)
				return false;
			if (c == 0xF0 && s[i + 1] < 0x90)
				return false;
			if (c == 0xF4 && s[i + 1] >= 0x90)
				return false;
			has_multibyte = true;
			i += 4;
		} else {
			return false;
		}
	}
	return has_multibyte;
}

static char *read_text_file_bytes(const char *path, size_t *out_len)
{
	size_t len = 0;
	char *buf = file_read(path, &len);
	if (!buf)
		return NULL;
	if (len >= 3 && (uint8_t)buf[0] == 0xEF && (uint8_t)buf[1] == 0xBB && (uint8_t)buf[2] == 0xBF) {
		char *sjis = utf2sjis(buf + 3, len - 3);
		free(buf);
		*out_len = strlen(sjis);
		return sjis;
	}
	if (is_valid_utf8_multibyte((const uint8_t *)buf, len)) {
		char *sjis = utf2sjis(buf, len);
		free(buf);
		*out_len = strlen(sjis);
		return sjis;
	}
	*out_len = len;
	return buf;
}

static bool TextFile_Close(void)
{
	if (!text_file_fp)
		return false;
	fclose(text_file_fp);
	text_file_fp = NULL;
	return true;
}

static void TextFile_ModuleFini(void)
{
	TextFile_Close();
	for (int i = 1; i < nr_tf_entries; i++) {
		if (tf_entries[i].used) {
			if (tf_entries[i].fp)
				fclose(tf_entries[i].fp);
			free(tf_entries[i].buf);
		}
	}
	free(tf_entries);
	tf_entries = NULL;
	nr_tf_entries = 0;
}

static void TextFileWriter_ModuleFini(void)
{
	TextFile_Close();
}

static bool TextFile_Close_v14(int handle)
{
	if (handle <= 0 || handle >= nr_tf_entries || !tf_entries[handle].used)
		return false;
	if (tf_entries[handle].fp)
		fclose(tf_entries[handle].fp);
	free(tf_entries[handle].buf);
	memset(&tf_entries[handle], 0, sizeof(tf_entries[handle]));
	return true;
}

static void *TextFile_ResolveClose(const struct ain_hll_function *f)
{
	return f->nr_arguments == 1 ? (void *)TextFile_Close_v14 : (void *)TextFile_Close;
}

static FILE *open_text_writer_path(struct string *file_name)
{
	if (!file_name)
		return NULL;
	char *path = fileop_resolve_write_path(file_name->text);
	if (!path)
		return NULL;
	char *path_copy = xstrdup(path);
	char *dir = path_dirname(path_copy);
	if (dir && dir[0])
		mkdir_p(dir);
	free(path_copy);
	FILE *fp = file_open_utf8(path, "wb");
	free(path);
	return fp;
}

static bool TextFile_Create(struct string *file_name)
{
	TextFile_Close();
	text_file_fp = open_text_writer_path(file_name);
	return text_file_fp != NULL;
}

static int TextFile_CreateWriter(struct string *file_name)
{
	FILE *fp = open_text_writer_path(file_name);
	if (!fp)
		return -1;
	int h = alloc_tf_handle();
	tf_entries[h].is_writer = true;
	tf_entries[h].fp = fp;
	return h;
}

static bool TextFile_Write(struct string *text)
{
	if (!text_file_fp || !text)
		return false;
	if (text->size <= 0)
		return true;
	return fwrite(text->text, 1, text->size, text_file_fp) == (size_t)text->size;
}

static bool TextFile_Write_v14(int handle, struct string *text)
{
	if (handle <= 0 || handle >= nr_tf_entries || !tf_entries[handle].used || !tf_entries[handle].fp || !text)
		return false;
	if (text->size <= 0)
		return true;
	return fwrite(text->text, 1, text->size, tf_entries[handle].fp) == (size_t)text->size;
}

static void *TextFile_ResolveWrite(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)TextFile_Write_v14 : (void *)TextFile_Write;
}

static bool TextFile_WriteLine(int handle, struct string *text)
{
	if (!TextFile_Write_v14(handle, text))
		return false;
	return fwrite("\r\n", 1, 2, tf_entries[handle].fp) == 2;
}

static bool TextFile_WriteAll(struct string *file_name, struct string *text)
{
	FILE *fp = open_text_writer_path(file_name);
	if (!fp)
		return false;
	bool ok = true;
	if (text && text->size > 0)
		ok = fwrite(text->text, 1, text->size, fp) == (size_t)text->size;
	fclose(fp);
	return ok;
}

static bool TextFile_ReadAll(struct string *file_name, struct string **text)
{
	if (!file_name || !text)
		return false;
	char *path = fileop_resolve_path(file_name->text);
	if (!path)
		return false;
	size_t len = 0;
	char *buf = read_text_file_bytes(path, &len);
	free(path);
	if (!buf)
		return false;
	if (*text)
		free_string(*text);
	*text = make_string(buf, len);
	free(buf);
	return true;
}

static int TextFile_OpenReader(struct string *file_name)
{
	if (!file_name)
		return -1;
	char *path = fileop_resolve_path(file_name->text);
	if (!path)
		return -1;
	size_t len = 0;
	char *buf = read_text_file_bytes(path, &len);
	free(path);
	if (!buf)
		return -1;
	int h = alloc_tf_handle();
	tf_entries[h].is_writer = false;
	tf_entries[h].buf = buf;
	tf_entries[h].len = len;
	tf_entries[h].pos = 0;
	return h;
}

static bool TextFile_IsEOF(int handle)
{
	if (handle <= 0 || handle >= nr_tf_entries || !tf_entries[handle].used || tf_entries[handle].is_writer)
		return true;
	return tf_entries[handle].pos >= tf_entries[handle].len;
}

static bool TextFile_Read(int handle, struct string **text)
{
	if (!text || TextFile_IsEOF(handle))
		return false;
	struct text_file_entry *e = &tf_entries[handle];
	size_t clen = 1;
	if (SJIS_2BYTE(e->buf[e->pos]) && e->pos + 1 < e->len)
		clen = 2;
	if (*text)
		free_string(*text);
	*text = make_string(e->buf + e->pos, clen);
	e->pos += clen;
	return true;
}

static bool TextFile_ReadLine(int handle, struct string **text)
{
	if (!text || TextFile_IsEOF(handle))
		return false;
	struct text_file_entry *e = &tf_entries[handle];
	size_t start = e->pos;
	while (e->pos < e->len && e->buf[e->pos] != '\n' && e->buf[e->pos] != '\r')
		e->pos++;
	size_t line_len = e->pos - start;
	if (e->pos < e->len && e->buf[e->pos] == '\r')
		e->pos++;
	if (e->pos < e->len && e->buf[e->pos] == '\n')
		e->pos++;
	if (*text)
		free_string(*text);
	*text = make_string(e->buf + start, line_len);
	return true;
}

HLL_LIBRARY(TextFile,
	    HLL_EXPORT(_ModuleFini, TextFile_ModuleFini),
	    HLL_EXPORT(Create, TextFile_Create),
	    HLL_EXPORT(CreateWriter, TextFile_CreateWriter),
	    HLL_OVERLOAD(Close, TextFile_ResolveClose),
	    HLL_OVERLOAD(Write, TextFile_ResolveWrite),
	    HLL_EXPORT(WriteLine, TextFile_WriteLine),
	    HLL_EXPORT(WriteAll, TextFile_WriteAll),
	    HLL_EXPORT(ReadAll, TextFile_ReadAll),
	    HLL_EXPORT(OpenReader, TextFile_OpenReader),
	    HLL_EXPORT(Read, TextFile_Read),
	    HLL_EXPORT(ReadLine, TextFile_ReadLine),
	    HLL_EXPORT(IsEOF, TextFile_IsEOF));

HLL_LIBRARY(TextFileWriter,
	    HLL_EXPORT(_ModuleFini, TextFileWriter_ModuleFini),
	    HLL_EXPORT(Create, TextFile_Create),
	    HLL_EXPORT(Close, TextFile_Close),
	    HLL_EXPORT(Write, TextFile_Write));

static void EXWriter_Create(void) {}
static void EXWriter_Close(void) {}
static int EXWriter_CreateHandle(void) { return 0; }
static void EXWriter_ReleaseHandle(possibly_unused int id) {}
static bool EXWriter_Load(possibly_unused int id, possibly_unused struct string *file_name) { return false; }
static bool EXWriter_Load_v8(possibly_unused struct string *file_name) { return false; }
static void *EXWriter_ResolveLoad(const struct ain_hll_function *f)
{
	return f->nr_arguments == 1 ? (void *)EXWriter_Load_v8 : (void *)EXWriter_Load;
}
static bool EXWriter_Save(possibly_unused int id, possibly_unused struct string *file_name) { return false; }
static bool EXWriter_Save_v8(possibly_unused struct string *file_name) { return false; }
static void *EXWriter_ResolveSave(const struct ain_hll_function *f)
{
	return f->nr_arguments == 1 ? (void *)EXWriter_Save_v8 : (void *)EXWriter_Save;
}
static bool EXWriter_LoadFromString(possibly_unused int id, possibly_unused struct string *text) { return false; }
static bool EXWriter_LoadFromString_v8(possibly_unused struct string *text) { return false; }
static void *EXWriter_ResolveLoadFromString(const struct ain_hll_function *f)
{
	return f->nr_arguments == 1 ? (void *)EXWriter_LoadFromString_v8 : (void *)EXWriter_LoadFromString;
}
static bool EXWriter_SaveToString(possibly_unused int id, struct string **text)
{
	if (text) {
		if (*text)
			free_string(*text);
		*text = make_string("", 0);
	}
	return false;
}
static bool EXWriter_SaveToString_v8(struct string **text)
{
	return EXWriter_SaveToString(0, text);
}
static void *EXWriter_ResolveSaveToString(const struct ain_hll_function *f)
{
	return f->nr_arguments == 1 ? (void *)EXWriter_SaveToString_v8 : (void *)EXWriter_SaveToString;
}
static bool EXWriter_EraseDefine(possibly_unused int id, possibly_unused struct string *name) { return false; }
static bool EXWriter_EraseDefine_v8(possibly_unused struct string *name) { return false; }
static void *EXWriter_ResolveEraseDefine(const struct ain_hll_function *f)
{
	return f->nr_arguments == 1 ? (void *)EXWriter_EraseDefine_v8 : (void *)EXWriter_EraseDefine;
}
static bool EXWriter_Rename(possibly_unused int id, possibly_unused struct string *old_name, possibly_unused struct string *new_name) { return false; }
static bool EXWriter_Rename_v8(possibly_unused struct string *old_name, possibly_unused struct string *new_name) { return false; }
static void *EXWriter_ResolveRename(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_Rename_v8 : (void *)EXWriter_Rename;
}
static bool EXWriter_SetPartial(possibly_unused int id, possibly_unused struct string *name, possibly_unused bool partial) { return false; }
static bool EXWriter_SetPartial_v8(possibly_unused struct string *name, possibly_unused bool partial) { return false; }
static void *EXWriter_ResolveSetPartial(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_SetPartial_v8 : (void *)EXWriter_SetPartial;
}
static bool EXWriter_SetInt(possibly_unused int id, possibly_unused struct string *name, possibly_unused int data) { return false; }
static bool EXWriter_SetInt_v8(possibly_unused struct string *name, possibly_unused int data) { return false; }
static void *EXWriter_ResolveSetInt(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_SetInt_v8 : (void *)EXWriter_SetInt;
}
static bool EXWriter_SetFloat(possibly_unused int id, possibly_unused struct string *name, possibly_unused float data) { return false; }
static bool EXWriter_SetFloat_v8(possibly_unused struct string *name, possibly_unused float data) { return false; }
static void *EXWriter_ResolveSetFloat(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_SetFloat_v8 : (void *)EXWriter_SetFloat;
}
static bool EXWriter_SetString(possibly_unused int id, possibly_unused struct string *name, possibly_unused struct string *data) { return false; }
static bool EXWriter_SetString_v8(possibly_unused struct string *name, possibly_unused struct string *data) { return false; }
static void *EXWriter_ResolveSetString(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_SetString_v8 : (void *)EXWriter_SetString;
}
static bool EXWriter_AddArrayInt(possibly_unused int id, possibly_unused struct string *name, possibly_unused int data) { return false; }
static bool EXWriter_AddArrayInt_v8(possibly_unused struct string *name, possibly_unused int data) { return false; }
static void *EXWriter_ResolveAddArrayInt(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_AddArrayInt_v8 : (void *)EXWriter_AddArrayInt;
}
static bool EXWriter_AddArrayFloat(possibly_unused int id, possibly_unused struct string *name, possibly_unused float data) { return false; }
static bool EXWriter_AddArrayFloat_v8(possibly_unused struct string *name, possibly_unused float data) { return false; }
static void *EXWriter_ResolveAddArrayFloat(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_AddArrayFloat_v8 : (void *)EXWriter_AddArrayFloat;
}
static bool EXWriter_AddArrayString(possibly_unused int id, possibly_unused struct string *name, possibly_unused struct string *data) { return false; }
static bool EXWriter_AddArrayString_v8(possibly_unused struct string *name, possibly_unused struct string *data) { return false; }
static void *EXWriter_ResolveAddArrayString(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_AddArrayString_v8 : (void *)EXWriter_AddArrayString;
}
static bool EXWriter_AddTableFormatInt(possibly_unused int id, possibly_unused struct string *name, possibly_unused struct string *format_name, possibly_unused bool is_key) { return false; }
static bool EXWriter_AddTableFormatInt_v8(possibly_unused struct string *name, possibly_unused struct string *format_name, possibly_unused bool is_key) { return false; }
static void *EXWriter_ResolveAddTableFormatInt(const struct ain_hll_function *f)
{
	return f->nr_arguments == 3 ? (void *)EXWriter_AddTableFormatInt_v8 : (void *)EXWriter_AddTableFormatInt;
}
static bool EXWriter_AddTableFormatFloat(possibly_unused int id, possibly_unused struct string *name, possibly_unused struct string *format_name) { return false; }
static bool EXWriter_AddTableFormatFloat_v8(possibly_unused struct string *name, possibly_unused struct string *format_name) { return false; }
static void *EXWriter_ResolveAddTableFormatFloat(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_AddTableFormatFloat_v8 : (void *)EXWriter_AddTableFormatFloat;
}
static bool EXWriter_AddTableFormatString(possibly_unused int id, possibly_unused struct string *name, possibly_unused struct string *format_name, possibly_unused bool is_key) { return false; }
static bool EXWriter_AddTableFormatString_v8(possibly_unused struct string *name, possibly_unused struct string *format_name, possibly_unused bool is_key) { return false; }
static void *EXWriter_ResolveAddTableFormatString(const struct ain_hll_function *f)
{
	return f->nr_arguments == 3 ? (void *)EXWriter_AddTableFormatString_v8 : (void *)EXWriter_AddTableFormatString;
}
static bool EXWriter_AddTableInt(possibly_unused int id, possibly_unused struct string *name, possibly_unused int data) { return false; }
static bool EXWriter_AddTableInt_v8(possibly_unused struct string *name, possibly_unused int data) { return false; }
static void *EXWriter_ResolveAddTableInt(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_AddTableInt_v8 : (void *)EXWriter_AddTableInt;
}
static bool EXWriter_AddTableFloat(possibly_unused int id, possibly_unused struct string *name, possibly_unused float data) { return false; }
static bool EXWriter_AddTableFloat_v8(possibly_unused struct string *name, possibly_unused float data) { return false; }
static void *EXWriter_ResolveAddTableFloat(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_AddTableFloat_v8 : (void *)EXWriter_AddTableFloat;
}
static bool EXWriter_AddTableString(possibly_unused int id, possibly_unused struct string *name, possibly_unused struct string *data) { return false; }
static bool EXWriter_AddTableString_v8(possibly_unused struct string *name, possibly_unused struct string *data) { return false; }
static void *EXWriter_ResolveAddTableString(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)EXWriter_AddTableString_v8 : (void *)EXWriter_AddTableString;
}

HLL_LIBRARY(EXWriter,
	    HLL_EXPORT(Create, EXWriter_Create),
	    HLL_EXPORT(Close, EXWriter_Close),
	    HLL_EXPORT(CreateHandle, EXWriter_CreateHandle),
	    HLL_EXPORT(ReleaseHandle, EXWriter_ReleaseHandle),
	    HLL_OVERLOAD(Load, EXWriter_ResolveLoad),
	    HLL_OVERLOAD(Save, EXWriter_ResolveSave),
	    HLL_OVERLOAD(LoadFromString, EXWriter_ResolveLoadFromString),
	    HLL_OVERLOAD(SaveToString, EXWriter_ResolveSaveToString),
	    HLL_OVERLOAD(EraseDefine, EXWriter_ResolveEraseDefine),
	    HLL_OVERLOAD(Rename, EXWriter_ResolveRename),
	    HLL_OVERLOAD(SetPartial, EXWriter_ResolveSetPartial),
	    HLL_OVERLOAD(SetInt, EXWriter_ResolveSetInt),
	    HLL_OVERLOAD(SetFloat, EXWriter_ResolveSetFloat),
	    HLL_OVERLOAD(SetString, EXWriter_ResolveSetString),
	    HLL_OVERLOAD(AddArrayInt, EXWriter_ResolveAddArrayInt),
	    HLL_OVERLOAD(AddArrayFloat, EXWriter_ResolveAddArrayFloat),
	    HLL_OVERLOAD(AddArrayString, EXWriter_ResolveAddArrayString),
	    HLL_OVERLOAD(AddTableFormatInt, EXWriter_ResolveAddTableFormatInt),
	    HLL_OVERLOAD(AddTableFormatFloat, EXWriter_ResolveAddTableFormatFloat),
	    HLL_OVERLOAD(AddTableFormatString, EXWriter_ResolveAddTableFormatString),
	    HLL_OVERLOAD(AddTableInt, EXWriter_ResolveAddTableInt),
	    HLL_OVERLOAD(AddTableFloat, EXWriter_ResolveAddTableFloat),
	    HLL_OVERLOAD(AddTableString, EXWriter_ResolveAddTableString));


static bool FileDialog_OpenLoadDialog(struct string **load_file_name,
		possibly_unused struct string *title_name,
		possibly_unused struct string *folder_name,
		possibly_unused struct page *filter_title_list,
		possibly_unused struct page *filter_extend_list)
{
	(void)load_file_name;
	return false;
}

static bool FileDialog_OpenSaveDialog(struct string **save_file_name,
		possibly_unused struct string *title_name,
		possibly_unused struct string *folder_name,
		possibly_unused struct page *filter_title_list,
		possibly_unused struct page *filter_extend_list)
{
	(void)save_file_name;
	return false;
}

static bool FileDialog_OpenSelectFolderDialog(possibly_unused struct string *display_text,
		possibly_unused struct string *title_text,
		struct string **folder_name)
{
	(void)folder_name;
	return false;
}

HLL_LIBRARY(FileDialog,
	    HLL_EXPORT(OpenLoadDialog, FileDialog_OpenLoadDialog),
	    HLL_EXPORT(OpenSaveDialog, FileDialog_OpenSaveDialog),
	    HLL_EXPORT(OpenSelectFolderDialog, FileDialog_OpenSelectFolderDialog));

static int ADVEngine_NumofFunctionName(void) { return 0; }
static void ADVEngine_GetFunctionName(struct string **out, possibly_unused int idx)
{
	if (out) {
		if (*out)
			free_string(*out);
		*out = string_ref(&EMPTY_STRING);
	}
}
static bool ADVEngine_AnalyseFunction(possibly_unused struct string *func_name) { return false; }
static bool ADVEngine_AnalyseFunction_v6(possibly_unused struct string *func_name, possibly_unused struct string *if_data) { return false; }
static void *ADVEngine_ResolveAnalyseFunction(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)ADVEngine_AnalyseFunction_v6 : (void *)ADVEngine_AnalyseFunction;
}
static bool ADVEngine_AnalyseIf(possibly_unused struct string *func_name) { return false; }
static int ADVEngine_NumofIf(possibly_unused struct string *func_name) { return 0; }
static void ADVEngine_GetIf(struct string **out, possibly_unused struct string *func_name, possibly_unused int idx)
{
	if (out) {
		if (*out)
			free_string(*out);
		*out = string_ref(&EMPTY_STRING);
	}
}
static void ADVEngine_GetFunctionList(possibly_unused struct string *file_path,
		possibly_unused struct page **p_array)
{
}
static bool ADVEngine_Load(possibly_unused struct string *function_name) { return false; }
static bool ADVEngine_LoadByFile(possibly_unused struct string *file_path,
		possibly_unused struct string *function_name) { return false; }
static bool ADVEngine_LoadByText(possibly_unused struct string *text) { return false; }
static void ADVEngine_Release(void) {}
static bool ADVEngine_IsExistIf(void) { return false; }
static bool ADVEngine_IsExistLowerIf(void) { return false; }
static int ADVEngine_NumofStep(void) { return 0; }
static int ADVEngine_NumofStep_fn(possibly_unused struct string *fn) { return 0; }
static void *ADVEngine_ResolveNumofStep(const struct ain_hll_function *f)
{
	return f->nr_arguments == 1 ? (void *)ADVEngine_NumofStep_fn : (void *)ADVEngine_NumofStep;
}
static struct string *ADVEngine_GetStepText(possibly_unused int step) { return string_ref(&EMPTY_STRING); }
static void ADVEngine_GetEmptyOut_i(struct string **out, possibly_unused int step)
{
	if (out) {
		if (*out) free_string(*out);
		*out = string_ref(&EMPTY_STRING);
	}
}
static void ADVEngine_GetEmptyOut_s(struct string **out, possibly_unused struct string *key)
{
	if (out) {
		if (*out) free_string(*out);
		*out = string_ref(&EMPTY_STRING);
	}
}
static void ADVEngine_GetStepText_v6(struct string **out, possibly_unused struct string *fn, possibly_unused int step)
{
	if (out) {
		if (*out) free_string(*out);
		*out = string_ref(&EMPTY_STRING);
	}
}
static void *ADVEngine_ResolveGetStepText(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 3)
		return (void *)ADVEngine_GetStepText_v6;
	if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID)
		return (void *)ADVEngine_GetEmptyOut_i;
	return (void *)ADVEngine_GetStepText;
}
static void ADVEngine_OpenFunctionFile(possibly_unused int step) {}
static void ADVEngine_OpenFunctionFile_v6(possibly_unused struct string *fn, possibly_unused int step) {}
static void *ADVEngine_ResolveOpenFunctionFile(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)ADVEngine_OpenFunctionFile_v6 : (void *)ADVEngine_OpenFunctionFile;
}
static int ADVEngine_GetCodeType(possibly_unused int step) { return 0; }
static int ADVEngine_GetCodeType_v6(possibly_unused struct string *fn, possibly_unused int step) { return 0; }
static void *ADVEngine_ResolveGetCodeType(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)ADVEngine_GetCodeType_v6 : (void *)ADVEngine_GetCodeType;
}
static struct string *ADVEngine_GetMessage(possibly_unused int step) { return string_ref(&EMPTY_STRING); }
static void *ADVEngine_ResolveGetMessage(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 3)
		return (void *)ADVEngine_GetStepText_v6;
	if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID)
		return (void *)ADVEngine_GetEmptyOut_i;
	return (void *)ADVEngine_GetMessage;
}
static bool ADVEngine_BindFunction(possibly_unused int step) { return false; }
static bool ADVEngine_BindFunction_v6(possibly_unused struct string *fn, possibly_unused int step) { return false; }
static void *ADVEngine_ResolveBindFunction(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)ADVEngine_BindFunction_v6 : (void *)ADVEngine_BindFunction;
}
static struct string *ADVEngine_GetFuncName(possibly_unused int step) { return string_ref(&EMPTY_STRING); }
static void *ADVEngine_ResolveGetFuncName(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 3)
		return (void *)ADVEngine_GetStepText_v6;
	if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID)
		return (void *)ADVEngine_GetEmptyOut_i;
	return (void *)ADVEngine_GetFuncName;
}
static int ADVEngine_CreateDelegateObjectID(possibly_unused int step) { return -1; }
static int ADVEngine_CreateDelegateObjectID_v6(possibly_unused struct string *fn, possibly_unused int step) { return -1; }
static void *ADVEngine_ResolveCreateDelegateObjectID(const struct ain_hll_function *f)
{
	return f->nr_arguments == 2 ? (void *)ADVEngine_CreateDelegateObjectID_v6 : (void *)ADVEngine_CreateDelegateObjectID;
}
static bool ADVEngine_GetArgumentValue_int(int *value, possibly_unused int step, possibly_unused int index)
{
	if (value) *value = 0;
	return false;
}
static bool ADVEngine_GetArgumentValue_int_v6(int *value, possibly_unused struct string *fn, possibly_unused int step, possibly_unused int index)
{
	if (value) *value = 0;
	return false;
}
static void *ADVEngine_ResolveGetArgInt(const struct ain_hll_function *f)
{
	return f->nr_arguments == 4 ? (void *)ADVEngine_GetArgumentValue_int_v6 : (void *)ADVEngine_GetArgumentValue_int;
}
static bool ADVEngine_GetArgumentValue_float(float *value, possibly_unused int step, possibly_unused int index)
{
	if (value) *value = 0.0f;
	return false;
}
static bool ADVEngine_GetArgumentValue_float_v6(float *value, possibly_unused struct string *fn, possibly_unused int step, possibly_unused int index)
{
	if (value) *value = 0.0f;
	return false;
}
static void *ADVEngine_ResolveGetArgFloat(const struct ain_hll_function *f)
{
	return f->nr_arguments == 4 ? (void *)ADVEngine_GetArgumentValue_float_v6 : (void *)ADVEngine_GetArgumentValue_float;
}
static bool ADVEngine_GetArgumentValue_bool(int *value, possibly_unused int step, possibly_unused int index)
{
	if (value) *value = 0;
	return false;
}
static void *ADVEngine_ResolveGetArgBool(const struct ain_hll_function *f)
{
	return f->nr_arguments == 4 ? (void *)ADVEngine_GetArgumentValue_int_v6 : (void *)ADVEngine_GetArgumentValue_bool;
}
static bool ADVEngine_GetArgumentValue_string(struct string **value, possibly_unused int step, possibly_unused int index)
{
	if (value) {
		if (*value)
			free_string(*value);
		*value = string_ref(&EMPTY_STRING);
	}
	return false;
}
static bool ADVEngine_GetArgumentValue_string_v6(struct string **value, possibly_unused struct string *fn, possibly_unused int step, possibly_unused int index)
{
	if (value) {
		if (*value)
			free_string(*value);
		*value = string_ref(&EMPTY_STRING);
	}
	return false;
}
static void *ADVEngine_ResolveGetArgString(const struct ain_hll_function *f)
{
	return f->nr_arguments == 4 ? (void *)ADVEngine_GetArgumentValue_string_v6 : (void *)ADVEngine_GetArgumentValue_string;
}
static struct string *ADVEngine_GetIfFuncName(possibly_unused int step) { return string_ref(&EMPTY_STRING); }
static void *ADVEngine_ResolveGetIfFuncName(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 3)
		return (void *)ADVEngine_GetStepText_v6;
	if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID)
		return (void *)ADVEngine_GetEmptyOut_i;
	return (void *)ADVEngine_GetIfFuncName;
}
static struct string *ADVEngine_GetIfFuncArg(possibly_unused int step) { return string_ref(&EMPTY_STRING); }
static void *ADVEngine_ResolveGetIfFuncArg(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 3)
		return (void *)ADVEngine_GetStepText_v6;
	if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID)
		return (void *)ADVEngine_GetEmptyOut_i;
	return (void *)ADVEngine_GetIfFuncArg;
}
static int ADVEngine_GetIfValue(possibly_unused int step) { return 0; }
static bool ADVEngine_IsLowerIf(possibly_unused int step) { return false; }
static struct string *ADVEngine_GetAttribute(possibly_unused struct string *key) { return string_ref(&EMPTY_STRING); }
static void *ADVEngine_ResolveGetAttribute(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID)
		return (void *)ADVEngine_GetEmptyOut_s;
	return (void *)ADVEngine_GetAttribute;
}

HLL_LIBRARY(ADVEngine,
	    HLL_EXPORT(NumofFunctionName, ADVEngine_NumofFunctionName),
	    HLL_EXPORT(GetFunctionName, ADVEngine_GetFunctionName),
	    HLL_EXPORT(AnalyseIf, ADVEngine_AnalyseIf),
	    HLL_EXPORT(NumofIf, ADVEngine_NumofIf),
	    HLL_EXPORT(GetIf, ADVEngine_GetIf),
	    HLL_OVERLOAD(AnalyseFunction, ADVEngine_ResolveAnalyseFunction),
	    HLL_EXPORT(GetFunctionList, ADVEngine_GetFunctionList),
	    HLL_EXPORT(Load, ADVEngine_Load),
	    HLL_EXPORT(LoadByFile, ADVEngine_LoadByFile),
	    HLL_EXPORT(LoadByText, ADVEngine_LoadByText),
	    HLL_EXPORT(Release, ADVEngine_Release),
	    HLL_EXPORT(IsExistIf, ADVEngine_IsExistIf),
	    HLL_EXPORT(IsExistLowerIf, ADVEngine_IsExistLowerIf),
	    HLL_OVERLOAD(NumofStep, ADVEngine_ResolveNumofStep),
	    HLL_OVERLOAD(GetStepText, ADVEngine_ResolveGetStepText),
	    HLL_OVERLOAD(OpenFunctionFile, ADVEngine_ResolveOpenFunctionFile),
	    HLL_OVERLOAD(GetCodeType, ADVEngine_ResolveGetCodeType),
	    HLL_OVERLOAD(GetMessage, ADVEngine_ResolveGetMessage),
	    HLL_OVERLOAD(BindFunction, ADVEngine_ResolveBindFunction),
	    HLL_OVERLOAD(GetFuncName, ADVEngine_ResolveGetFuncName),
	    HLL_OVERLOAD(CreateDelegateObjectID, ADVEngine_ResolveCreateDelegateObjectID),
	    HLL_OVERLOAD(GetArgumentValue_int, ADVEngine_ResolveGetArgInt),
	    HLL_OVERLOAD(GetArgumentValue_float, ADVEngine_ResolveGetArgFloat),
	    HLL_OVERLOAD(GetArgumentValue_bool, ADVEngine_ResolveGetArgBool),
	    HLL_OVERLOAD(GetArgumentValue_string, ADVEngine_ResolveGetArgString),
	    HLL_OVERLOAD(GetIfFuncName, ADVEngine_ResolveGetIfFuncName),
	    HLL_OVERLOAD(GetIfFuncArg, ADVEngine_ResolveGetIfFuncArg),
	    HLL_EXPORT(GetIfValue, ADVEngine_GetIfValue),
	    HLL_EXPORT(IsLowerIf, ADVEngine_IsLowerIf),
	    HLL_OVERLOAD(GetAttribute, ADVEngine_ResolveGetAttribute));
