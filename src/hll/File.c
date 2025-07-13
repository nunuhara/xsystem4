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
#include <zlib.h>

#include "system4.h"
#include "system4/buffer.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"

#include "hll.h"
#include "savedata.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

enum file_mode {
	FILE_READ = 1,
	FILE_WRITE = 2
};

static FILE *current_file = NULL;
static enum file_mode current_mode;
// Buffer for reading from / writing to binary format
static struct buffer contents;
// Buffer for reading from JSON format
static char *json_contents = NULL;
static size_t json_cursor = 0;

static void read_page(struct page *page);
static void write_page(struct page *page);

static void read_value(union vm_value *v, enum ain_data_type type)
{
	switch (type) {
	case AIN_INT:
	case AIN_BOOL:
	case AIN_LONG_INT:
		v->i = buffer_read_int32(&contents);
		break;
	case AIN_FLOAT:
		v->f = buffer_read_float(&contents);
		break;
	case AIN_STRING:
		variable_fini(*v, type, true);
		v->i = heap_alloc_string(buffer_read_string(&contents));
		break;
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
		read_page(heap_get_page(v->i));
		break;
	default:
		VM_ERROR("Unsupported value type %d", type);
	}
}

static void read_page(struct page *page)
{
	switch (page->type) {
	case STRUCT_PAGE:
		{
			struct ain_struct *s = &ain->structures[page->index];
			for (int i = 0; i < s->nr_members; i++) {
				read_value(&page->values[i], s->members[i].type.data);
			}
		}
		break;
	case ARRAY_PAGE:
		{
			enum ain_data_type type = page->array.rank > 1 ? page->a_type : array_type(page->a_type);
			for (int i = 0; i < page->nr_vars; i++) {
				read_value(&page->values[i], type);
			}
		}
		break;
	default:
		VM_ERROR("Unsupported page type %d", page->type);
	}
}

static void write_value(union vm_value v, enum ain_data_type type)
{
	switch (type) {
	case AIN_INT:
	case AIN_BOOL:
	case AIN_LONG_INT:
		buffer_write_int32(&contents, v.i);
		break;
	case AIN_FLOAT:
		buffer_write_float(&contents, v.f);
		break;
	case AIN_STRING:
		buffer_write_cstringz(&contents, heap_get_string(v.i)->text);
		break;
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
		write_page(heap_get_page(v.i));
		break;
	default:
		VM_ERROR("Unsupported value type %d", type);
	}
}

static void write_page(struct page *page)
{
	switch (page->type) {
	case STRUCT_PAGE:
		{
			struct ain_struct *s = &ain->structures[page->index];
			for (int i = 0; i < s->nr_members; i++) {
				write_value(page->values[i], s->members[i].type.data);
			}
		}
		break;
	case ARRAY_PAGE:
		{
			enum ain_data_type type = page->array.rank > 1 ? page->a_type : array_type(page->a_type);
			for (int i = 0; i < page->nr_vars; i++) {
				write_value(page->values[i], type);
			}
		}
		break;
	default:
		VM_ERROR("Unsupported page type %d", page->type);
	}
}

static int File_Open(struct string *filename, int type)
{
	const char *mode;
	if (type == FILE_READ) {
		mode = "rb";
	} else if (type == FILE_WRITE) {
		mode = "wb";
	} else {
		WARNING("Unknown mode in File.Open: %d", type);
		return 0;
	}
	if (current_file) {
		WARNING("Previously opened file wasn't closed");
		fclose(current_file);
	}

	char *path = unix_path(filename->text);
	current_file = file_open_utf8(path, mode);
	current_mode = type;
	if (!current_file) {
		WARNING("Failed to open file '%s': %s", display_utf0(path), strerror(errno));
	}

	free(path);
	return !!current_file;
}

static int File_Close(void)
{
	if (!current_file) {
		VM_ERROR("File.Close called, but no open file");
	}
	int r = 1;
	if (current_mode == FILE_WRITE) {
		size_t raw_size = contents.index;
		unsigned long compressed_size = compressBound(raw_size);
		uint8_t *buf = xmalloc(4 + compressed_size);
		LittleEndian_putDW(buf, 0, raw_size);
		int r = compress2(buf + 4, &compressed_size, contents.buf, raw_size, Z_DEFAULT_COMPRESSION);
		if (r != Z_OK) {
			WARNING("File.Close: compress2 failed");
			r = 0;
		} else {
			if (fwrite(buf, 4 + compressed_size, 1, current_file) != 1) {
				WARNING("File.Close: fwrite failed: %s", strerror(errno));
				r = 0;
			}
		}
		free(buf);
	}
	if (fclose(current_file))
		r = 0;
	current_file = NULL;
	if (json_contents)
		free(json_contents);
	json_contents = NULL;
	json_cursor = 0;
	if (contents.buf)
		free(contents.buf);
	buffer_init(&contents, NULL, 0);
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
	if (current_mode != FILE_READ) {
		VM_ERROR("File.Read called, but file wasn't opened for reading");
	}

	// read file into memory if needed
	if (!json_contents && !contents.buf) {
		fseek(current_file, 0, SEEK_END);
		size_t file_size = ftell(current_file);
		fseek(current_file, 0, SEEK_SET);

		uint8_t *buf = xmalloc(file_size + 1);
		if (fread(buf, file_size, 1, current_file) != 1) {
			WARNING("File.Read failed (fread): %s", strerror(errno));
			free(buf);
			return 0;
		}
		buf[file_size] = '\0';

		// Check if it's ZLIB compressed
		if (file_size > 6 && buf[4] == 0x78 && buf[5] == 0x9c) {
			unsigned long raw_size = LittleEndian_getDW(buf, 0);
			uint8_t *raw_buf = xmalloc(raw_size);
			int r = uncompress(raw_buf, &raw_size, buf + 4, file_size);
			free(buf);
			if (r != Z_OK) {
				WARNING("File.Read failed to uncompress");
				free(raw_buf);
				return 0;
			}
			buffer_init(&contents, raw_buf, raw_size);
		} else {
			// JSON format created by old versions of xsystem4
			json_contents = (char*)buf;
		}
	}

	if (contents.buf) {
		read_page(page);
	} else {
		const char *end;
		cJSON *json = cJSON_ParseWithOpts(json_contents+json_cursor, &end, false);
		if (!json) {
			WARNING("File.Read failed to parse JSON");
			return 0;
		}
		if (!cJSON_IsArray(json)) {
			WARNING("File.Read incorrect type in parsed JSON");
			return 0;
		}

		json_cursor = end - json_contents;

		json_load_page(page, json, true);
		cJSON_Delete(json);
	}
	return 1;
}

static int File_Write(struct page *page)
{
	if (!current_file) {
		VM_ERROR("File.Write called, but no open file");
	}
	if (current_mode != FILE_WRITE) {
		VM_ERROR("File.Write called, but file wasn't opened for writing");
	}
	write_page(page);
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
	ustat s;
	char *path = unix_path(filename->text);
	union vm_value *date = (*page)->values;

	if (stat_utf8(path, &s) < 0) {
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
	date[1].i = tm->tm_mon + 1;
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
	if (remove_utf8(path)) {
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

