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

#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "savedata.h"
#include "xsystem4.h"

static bool vs_read = false;
static bool vs_from_gamedir = false;
static FILE *vs_file = NULL;

enum vsf_valuetype {
	VSF_BYTE = 0,
	VSF_INT = 1,
	VSF_FLOAT = 2,
	VSF_STRING = 3,
};

static bool vsf_write(const void *ptr, size_t size)
{
	if (fwrite(ptr, size, 1, vs_file) != 1) {
		WARNING("fwrite failed: %s", strerror(errno));
		return false;
	}
	return true;
}

static bool vsf_read(uint8_t *dst, size_t size)
{
	if (fread(dst, size, 1, vs_file) != 1) {
		WARNING("fread failed: %s", strerror(errno));
		return false;
	}
	return true;
}

static bool vsf_read_type(enum vsf_valuetype type)
{
	if (!vs_file || !vs_read) {
		WARNING("VSFile is not opened for reading");
		return false;
	}

	int c = fgetc(vs_file);
	if (c == EOF)
		return false;
	if ((enum vsf_valuetype)c != type) {
		ungetc(c, vs_file);
		return false;
	}
	return true;
}

static bool vsf_write_type(enum vsf_valuetype type)
{
	if (!vs_file || vs_read) {
		WARNING("VSFile is not opened for writing");
		return false;
	}
	uint8_t b = type;
	return vsf_write(&b, 1);
}

static bool vsfile_open(struct string *filename, bool read)
{
	if (!filename)
		return false;
	char *u = savedir_path(filename->text);
	if (vs_file) {
		WARNING("VSFile is already opened");
		fclose(vs_file);
		vs_file = NULL;
	}
	vs_read = read;
	vs_from_gamedir = false;
	if (!read) {
		char *dir = path_dirname(u);
		if (dir && *dir)
			mkdir_p(dir);
	}
	vs_file = file_open_utf8(u, read ? "rb" : "wb");
	if (!vs_file && read) {
		char *gu = gamedir_path_icase(filename->text);
		if (gu) {
			vs_file = file_open_utf8(gu, "rb");
			if (vs_file)
				vs_from_gamedir = true;
			free(gu);
		}
	}
	if (!vs_file) {
		WARNING("Failed to open '%s': %s", display_utf0(u), strerror(errno));
	}
	free(u);
	return !!vs_file;
}

static void VSFile_ModuleFini(void)
{
	if (vs_file) {
		fclose(vs_file);
		vs_file = NULL;
	}
	vs_from_gamedir = false;
}

static bool VSFile_OpenForWrite(struct string *filename)
{
	if (!vsfile_open(filename, false))
		return false;
	if (!vsf_write("VSF\0\0\0\0", 8)) {
		fclose(vs_file);
		vs_file = NULL;
		vs_from_gamedir = false;
		return false;
	}
	return true;
}

static bool VSFile_OpenForRead(struct string *filename)
{
	if (!vsfile_open(filename, true))
		return false;
	uint8_t hdr[8];
	if (!vsf_read(hdr, 8) || memcmp(hdr, "VSF\0\0\0\0", 8)) {
		fclose(vs_file);
		vs_file = NULL;
		vs_from_gamedir = false;
		return false;
	}
	return true;
}

static bool VSFile_Close(void)
{
	if (vs_file) {
		fclose(vs_file);
		vs_file = NULL;
	} else {
		WARNING("VSFILE is already closed");
	}
	vs_from_gamedir = false;
	return true;
}

static bool VSFile_WriteByte(int data)
{
	if (!vsf_write_type(VSF_BYTE))
		return false;

	uint8_t b = data;
	return vsf_write(&b, 1);
}

static bool VSFile_WriteInt(int data)
{
	if (!vsf_write_type(VSF_INT))
		return false;

	uint8_t b[4];
	LittleEndian_putDW(b, 0, data);
	return vsf_write(b, 4);
}

static bool VSFile_WriteFloat(float data)
{
	if (!vsf_write_type(VSF_FLOAT))
		return false;

	uint8_t b[4];
	union {
		uint32_t i;
		float f;
	} cast;
	cast.f = data;
	LittleEndian_putDW(b, 0, cast.i);
	return vsf_write(b, 4);
}

static bool VSFile_WriteString(struct string *str)
{
	if (!vsf_write_type(VSF_STRING))
		return false;

	const char *text = str ? str->text : "";
	size_t len = str ? (size_t)str->size + 1 : 1;
	return vsf_write(text, len);
}

static bool VSFile_ReadByte(int *data)
{
	if (!data || !vsf_read_type(VSF_BYTE))
		return false;

	uint8_t b;
	if (!vsf_read(&b, 1))
		return false;
	*data = b;
	return true;
}

static bool VSFile_ReadInt(int *data)
{
	if (!data || !vsf_read_type(VSF_INT))
		return false;

	uint8_t b[4];
	if (!vsf_read(b, 4))
		return false;
	*data = LittleEndian_getDW(b, 0);
	return true;
}

static bool VSFile_ReadFloat(float *data)
{
	if (!data || !vsf_read_type(VSF_FLOAT))
		return false;

	uint8_t b[4];
	union {
		uint32_t i;
		float f;
	} cast;
	if (!vsf_read(b, 4))
		return false;
	cast.i = LittleEndian_getDW(b, 0);
	*data = cast.f;
	return true;
}

static bool VSFile_ReadString(struct string **str)
{
	if (!str || !vsf_read_type(VSF_STRING))
		return false;

	char b;
	struct string *s = make_string("", 0);
	while (true) {
		if (!vsf_read((uint8_t*)&b, 1)) {
			free_string(s);
			return false;
		}
		if (!b)
			break;
		string_append_cstr(&s, &b, 1);
	}

	// Only cross-encoding-convert strings read from SaveData (e.g. JP save imported into CN build).
	// Read-only game data files opened from game_dir (e.g. Data/Parameter/*.dat) store raw engine lookup keys
	// that must match VM strings verbatim without heuristic SJIS<->GBK rewriting.
	if (!vs_from_gamedir && s->size > 0) {
		int bias = save_text_gbk_bias(s->text);
		if ((sys4_text_is_gbk && bias <= -12) || (!sys4_text_is_gbk && bias >= 12)) {
			struct string *conv = save_decode_vm_string(s->text, s->size, !sys4_text_is_gbk);
			free_string(s);
			s = conv;
		}
	}

	if (*str)
		free_string(*str);
	*str = s;
	return true;
}

HLL_LIBRARY(VSFile,
	    HLL_EXPORT(_ModuleFini, VSFile_ModuleFini),
	    HLL_EXPORT(OpenForWrite, VSFile_OpenForWrite),
	    HLL_EXPORT(OpenForRead, VSFile_OpenForRead),
	    HLL_EXPORT(Close, VSFile_Close),
	    HLL_EXPORT(WriteByte, VSFile_WriteByte),
	    HLL_EXPORT(WriteInt, VSFile_WriteInt),
	    HLL_EXPORT(WriteFloat, VSFile_WriteFloat),
	    HLL_EXPORT(WriteString, VSFile_WriteString),
	    HLL_EXPORT(ReadByte, VSFile_ReadByte),
	    HLL_EXPORT(ReadInt, VSFile_ReadInt),
	    HLL_EXPORT(ReadFloat, VSFile_ReadFloat),
	    HLL_EXPORT(ReadString, VSFile_ReadString)
	);
