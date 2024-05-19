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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include "cJSON.h"

#include "system4.h"
#include "system4/buffer.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/file.h"

#include "hll.h"
#include "xsystem4.h"

/*
 * Special SaveData API for savings sets of integers and strings
 * (e.g. unlocked CGs).
 *
 * The data is stored in a file with the following format:
 *
 * struct psr_file {
 *     char     magic[4];               // "PSR\0"
 *     uint32_t version;                // must be 0
 *     uint32_t raw_size;               // size of the uncompressed data
 *     uint32_t compressed_size;        // size of the compressed data
 *     uint8_t  data[compressed_size];  // ZLIB-compressed data
 * };
 *
 * The uncompressed data is a structure of the following form:
 *
 * struct psr_data {
 *     uint32_t nr_integers;
 *     int32_t  integers[nr_integers];
 *     uint32_t nr_strings;
 *     char     strings[nr_strings][?];  // null-terminated strings
 * };
 */

#define PSR_HEADER_SIZE 16

struct passregister {
	char *filename;
	size_t nr_integers;
	int *integers;
	size_t nr_strings;
	char **strings;
};

static size_t nr_registers = 0;
static struct passregister *registers = NULL;

static int compare_int(const void *a, const void *b) {
    return (*(int*)a - *(int*)b);
}

static int compare_string(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void write_register(unsigned handle)
{
	struct passregister *reg = registers + handle;

	FILE *f = file_open_utf8(reg->filename, "wb");
	if (!f) {
		WARNING("Failed to open PassRegister file: %s: %s", display_utf0(reg->filename), strerror(errno));
		return;
	}

	struct buffer buf;
	buffer_init(&buf, NULL, 0);
	buffer_write_int32(&buf, reg->nr_integers);
	for (size_t i = 0; i < reg->nr_integers; i++) {
		buffer_write_int32(&buf, reg->integers[i]);
	}
	buffer_write_int32(&buf, reg->nr_strings);
	for (size_t i = 0; i < reg->nr_strings; i++) {
		buffer_write_cstringz(&buf, reg->strings[i]);
	}

	size_t raw_size = buf.index;
	unsigned long compressed_size = compressBound(raw_size);
	uint8_t *compressed = xmalloc(compressed_size);
	int r = compress2(compressed, &compressed_size, buf.buf, raw_size, Z_BEST_COMPRESSION);
	if (r != Z_OK) {
		WARNING("Failed to compress PassRegister file: %s", display_utf0(reg->filename));
		goto end;
	}
	uint8_t header[PSR_HEADER_SIZE] = {'P', 'S', 'R', 0, 0, 0, 0, 0};
	LittleEndian_putDW(header, 8, raw_size);
	LittleEndian_putDW(header, 12, compressed_size);
	if (fwrite(header, sizeof(header), 1, f) != 1 || fwrite(compressed, compressed_size, 1, f) != 1) {
		WARNING("Failed to write PassRegister file: %s: %s", display_utf0(reg->filename), strerror(errno));
		goto end;
	}
end:
	free(compressed);
	free(buf.buf);
	if (fclose(f)) {
		WARNING("Error writing to save file: %s: %s", display_utf0(reg->filename), strerror(errno));
		goto end;
	}
}

static cJSON *_type_check(const char *file, const char *func, int line, int type, cJSON *json)
{
	if (!json || json->type != type)
		_vm_error("*ERROR*(%s:%s:%d): Invalid PassRegister save file", file, func, line);
	return json;
}

#define type_check(type, json) _type_check(__FILE__, __func__, __LINE__, type, json)

static void read_register_psr(struct passregister *reg, uint8_t *data, size_t data_len)
{
	int version = LittleEndian_getDW(data, 4);
	if (version != 0) {
		WARNING("Unexpected PassRegister version %d", version);
		return;
	}
	unsigned long raw_size = LittleEndian_getDW(data, 8);
	size_t compressed_size = LittleEndian_getDW(data, 12);
	if (PSR_HEADER_SIZE + compressed_size != data_len) {
		WARNING("Broken PassRegister file");
		return;
	}
	uint8_t *raw = xmalloc(raw_size);
	int r = uncompress(raw, &raw_size, data + PSR_HEADER_SIZE, compressed_size);
	if (r != Z_OK) {
		WARNING("PassRegister: failed to uncompress");
		free(raw);
		return;
	}

	struct buffer buf;
	buffer_init(&buf, raw, raw_size);
	reg->nr_integers = buffer_read_int32(&buf);
	reg->integers = xcalloc(reg->nr_integers, sizeof(int));
	for (size_t i = 0; i < reg->nr_integers; i++) {
		reg->integers[i] = buffer_read_int32(&buf);
	}
	reg->nr_strings = buffer_read_int32(&buf);
	reg->strings = xcalloc(reg->nr_strings, sizeof(char*));
	for (size_t i = 0; i < reg->nr_strings; i++) {
		reg->strings[i] = strdup(buffer_skip_string(&buf));
	}

	free(raw);
}

static void read_register_json(struct passregister *reg, char *data)
{
	// parse/validate JSON data
	cJSON *obj = type_check(cJSON_Object, cJSON_Parse(data));
	cJSON *integers = type_check(cJSON_Array, cJSON_GetObjectItem(obj, "integers"));
	cJSON *strings = type_check(cJSON_Array, cJSON_GetObjectItem(obj, "strings"));

	reg->nr_integers = cJSON_GetArraySize(integers);
	reg->integers = xcalloc(reg->nr_integers, sizeof(int));
	for (size_t i = 0; i < reg->nr_integers; i++) {
		cJSON *value = type_check(cJSON_Number, cJSON_GetArrayItem(integers, i));
		reg->integers[i] = value->valueint;
	}
	reg->nr_strings = cJSON_GetArraySize(strings);
	reg->strings = xcalloc(reg->nr_strings, sizeof(char*));
	for (size_t i = 0; i < reg->nr_strings; i++) {
		cJSON *value = type_check(cJSON_String, cJSON_GetArrayItem(strings, i));
		reg->strings[i] = strdup(cJSON_GetStringValue(value));
	}

	cJSON_Delete(obj);
}

static void read_register(unsigned handle, const char *filename)
{
	struct passregister *reg = registers + handle;
	free(reg->filename);
	reg->filename = savedir_path(filename);

	size_t data_len;
	char *data = file_read(reg->filename, &data_len);
	if (!data)
		return;
	if (!memcmp(data, "PSR", 4)) {
		read_register_psr(reg, (uint8_t *)data, data_len);
	} else {
		// JSON format created by old versions of xsystem4
		read_register_json(reg, data);
	}
	free(data);

	// Make sure the arrays are sorted.
	qsort(reg->integers, reg->nr_integers, sizeof(int), compare_int);
	qsort(reg->strings, reg->nr_strings, sizeof(char*), compare_string);
}

static bool PassRegister_SetFileName(int handle, struct string *filename)
{
	if (handle < 0)
		return false;
	if ((size_t)handle >= nr_registers) {
		registers = xrealloc_array(registers, nr_registers, handle+1, sizeof(struct passregister));
		nr_registers = handle+1;
	}
	read_register(handle, filename->text);
	return true;
}

static int *find_number(int handle, int n)
{
	struct passregister *reg = registers + handle;
	return bsearch(&n, reg->integers, reg->nr_integers, sizeof(int), compare_int);
}

static bool PassRegister_RegistNumber(int handle, int n)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;
	if (find_number(handle, n) != NULL)
		return true;

	struct passregister *reg = registers + handle;
	reg->integers = xrealloc_array(reg->integers, reg->nr_integers, reg->nr_integers+1, sizeof(int));
	// NOTE: this could be improved by using O(n) insertion
	reg->integers[reg->nr_integers++] = n;
	qsort(reg->integers, reg->nr_integers, sizeof(int), compare_int);
	write_register(handle);
	return true;
}

static char **find_text(int handle, const char *text)
{
	struct passregister *reg = registers + handle;
	return bsearch(&text, reg->strings, reg->nr_strings, sizeof(char*), compare_string);
}

static bool PassRegister_RegistText(int handle, struct string *text)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;
	if (find_text(handle, text->text) != NULL)
		return true;

	struct passregister *reg = registers + handle;
	reg->strings = xrealloc_array(reg->strings, reg->nr_strings, reg->nr_strings+1, sizeof(char*));
	// NOTE: this could be improved by using O(n) insertion
	reg->strings[reg->nr_strings++] = strdup(text->text);
	qsort(reg->strings, reg->nr_strings, sizeof(char*), compare_string);
	write_register(handle);
	return true;
}

static bool PassRegister_UnregistNumber(int handle, int n)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;

	struct passregister *reg = registers + handle;
	int *found = find_number(handle, n);
	if (found) {
		int i = found - reg->integers;
		reg->nr_integers--;
		memmove(found, found + 1, (reg->nr_integers - i) * sizeof(int));
		write_register(handle);
	}
	return true;
}

static bool PassRegister_UnregistText(int handle, struct string *text)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;

	struct passregister *reg = registers + handle;
	char **found = find_text(handle, text->text);
	if (found) {
		int i = found - reg->strings;
		reg->nr_strings--;
		memmove(found, found + 1, (reg->nr_strings - i) * sizeof(char*));
		write_register(handle);
	}
	return true;
}

static bool PassRegister_UnregistAll(int handle)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;

	struct passregister *reg = registers + handle;
	if (!reg->nr_integers && !reg->nr_strings)
		return true;

	free(reg->integers);
	free(reg->strings);
	reg->integers = NULL;
	reg->strings = NULL;
	reg->nr_integers = 0;
	reg->nr_strings = 0;
	write_register(handle);
	return true;
}

static bool PassRegister_ExistNumber(int handle, int n)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;
	return find_number(handle, n) != NULL;
}

static bool PassRegister_ExistText(int handle, struct string *text)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;
	return find_text(handle, text->text) != NULL;
}

static void PassRegister_fini(void)
{
	for (size_t i = 0; i < nr_registers; i++) {
		free(registers[i].filename);
		free(registers[i].integers);
		for (size_t j = 0; j < registers[i].nr_strings; j++) {
			free(registers[i].strings[j]);
		}
		free(registers[i].strings);
	}
	free(registers);
	registers = NULL;
	nr_registers = 0;
}

HLL_LIBRARY(PassRegister,
	    HLL_EXPORT(SetFileName, PassRegister_SetFileName),
	    HLL_EXPORT(RegistNumber, PassRegister_RegistNumber),
	    HLL_EXPORT(RegistText, PassRegister_RegistText),
	    HLL_EXPORT(UnregistNumber, PassRegister_UnregistNumber),
	    HLL_EXPORT(UnregistText, PassRegister_UnregistText),
	    HLL_EXPORT(UnregistAll, PassRegister_UnregistAll),
	    HLL_EXPORT(ExistNumber, PassRegister_ExistNumber),
	    HLL_EXPORT(ExistText, PassRegister_ExistText),
	    HLL_EXPORT(_ModuleFini, PassRegister_fini));
