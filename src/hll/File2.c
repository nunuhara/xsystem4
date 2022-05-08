/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"

#include "little_endian.h"
#include "hll.h"
#include "xsystem4.h"

static FILE *current_file = NULL;

static bool file2_open(struct string *filename, const char *mode)
{
	if (current_file) {
		WARNING("Previously opened file wasn't closed");
		fclose(current_file);
	}

	char *path = unix_path(filename->text);
	current_file = file_open_utf8(path, mode);
	if (!current_file) {
		WARNING("Failed to open file '%s': %s", display_utf0(path), strerror(errno));
	}

	free(path);
	return !!current_file;
}

static bool file2_write(const void *ptr, size_t size)
{
	if (!current_file) {
		WARNING("File2.Write* called, but no open file");
		return false;
	}
	if (fwrite(ptr, size, 1, current_file) != 1) {
		WARNING("fwrite failed: %s", strerror(errno));
		return false;
	}
	return true;
}

static bool file2_read(void *dst, size_t size)
{
	if (!current_file) {
		WARNING("File2.Read* called, but no open file");
		return false;
	}
	if (fread(dst, size, 1, current_file) != 1) {
		WARNING("fread failed: %s", strerror(errno));
		return false;
	}
	return true;
}

static bool File2_OpenForWrite(struct string *filename)
{
	return file2_open(filename, "wb");
}

static bool File2_OpenForRead(struct string *filename)
{
	return file2_open(filename, "rb");
}

static bool File2_Close(void)
{
	if (!current_file) {
		VM_ERROR("File2.Close called, but no open file");
	}
	int r = !fclose(current_file);
	current_file = NULL;
	return r;
}

static bool File2_WriteByte(int data)
{
	uint8_t b = data;
	return file2_write(&b, 1);
}

static bool File2_WriteInt(int data)
{
	uint8_t b[4];
	LittleEndian_putDW(b, 0, data);
	return file2_write(b, 4);
}

static bool File2_WriteFloat(float data)
{
	uint8_t b[4];
	union {
		uint32_t i;
		float f;
	} cast;
	cast.f = data;
	LittleEndian_putDW(b, 0, cast.i);
	return file2_write(b, 4);
}

static bool File2_WriteString(struct string *str)
{
	return file2_write(str->text, str->size+1);
}

static bool File2_ReadByte(int *data)
{
	uint8_t b;
	if (!file2_read(&b, 1))
		return false;
	*data = b;
	return true;
}

static bool File2_ReadInt(int *data)
{
	uint8_t b[4];
	if (!file2_read(b, 4))
		return false;
	*data = LittleEndian_getDW(b, 0);
	return true;
}

static bool File2_ReadFloat(float *data)
{
	uint8_t b[4];
	union {
		uint32_t i;
		float f;
	} cast;
	if (!file2_read(b, 4))
		return false;
	cast.i = LittleEndian_getDW(b, 0);
	*data = cast.f;
	return true;
}

static bool File2_ReadString(struct string **str)
{
	char b;
	struct string *s = make_string("", 0);
	while (true) {
		if (!file2_read((uint8_t*)&b, 1)) {
			free_string(s);
			return false;
		}
		if (!b)
			break;
		string_append_cstr(&s, &b, 1);
	}

	if (*str)
		free_string(*str);
	*str = s;
	return true;
}

HLL_LIBRARY(File2,
	    HLL_EXPORT(OpenForWrite, File2_OpenForWrite),
	    HLL_EXPORT(OpenForRead, File2_OpenForRead),
	    HLL_EXPORT(Close, File2_Close),
	    HLL_EXPORT(WriteByte, File2_WriteByte),
	    HLL_EXPORT(WriteInt, File2_WriteInt),
	    HLL_EXPORT(WriteFloat, File2_WriteFloat),
	    HLL_EXPORT(WriteString, File2_WriteString),
	    HLL_EXPORT(ReadByte, File2_ReadByte),
	    HLL_EXPORT(ReadInt, File2_ReadInt),
	    HLL_EXPORT(ReadFloat, File2_ReadFloat),
	    HLL_EXPORT(ReadString, File2_ReadString)
	    );
