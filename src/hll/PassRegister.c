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
#include "cJSON.h"

#include "system4.h"
#include "system4/buffer.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/file.h"
#include "system4/zlib.h"

#include "hll.h"
#include "savedata.h"
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

static void free_register_entries(struct passregister *reg)
{
	free(reg->integers);
	for (size_t i = 0; i < reg->nr_strings; i++) {
		free(reg->strings[i]);
	}
	free(reg->strings);
	reg->integers = NULL;
	reg->strings = NULL;
	reg->nr_integers = 0;
	reg->nr_strings = 0;
}

static void write_register(unsigned handle)
{
	struct passregister *reg = registers + handle;

	char *tmp_path = NULL;
	FILE *f = atomic_save_open(reg->filename, "wb", &tmp_path);
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
	size_t compressed_size = zlib_compress_bound(raw_size);
	uint8_t *compressed = xmalloc(compressed_size);
	compressed_size = zlib_compress(compressed, compressed_size, buf.buf,
			raw_size, ZLIB_BEST_COMPRESSION);
	if (!compressed_size) {
		WARNING("Failed to compress PassRegister file: %s", display_utf0(reg->filename));
		free(compressed);
		free(buf.buf);
		atomic_save_abort(f, tmp_path);
		return;
	}
	uint8_t header[PSR_HEADER_SIZE] = {'P', 'S', 'R', 0, 0, 0, 0, 0};
	LittleEndian_putDW(header, 8, raw_size);
	LittleEndian_putDW(header, 12, compressed_size);
	bool write_ok = fwrite(header, sizeof(header), 1, f) == 1
		&& fwrite(compressed, compressed_size, 1, f) == 1;
	free(compressed);
	free(buf.buf);
	if (!write_ok) {
		WARNING("Failed to write PassRegister file: %s: %s", display_utf0(reg->filename), strerror(errno));
		atomic_save_abort(f, tmp_path);
		return;
	}
	if (!atomic_save_commit(f, reg->filename, tmp_path, false)) {
		WARNING("Error writing to save file: %s: %s", display_utf0(reg->filename), strerror(errno));
	}
}

static void read_register_psr(struct passregister *reg, uint8_t *data, size_t data_len)
{
	if (data_len < PSR_HEADER_SIZE) {
		WARNING("Broken PassRegister file");
		return;
	}
	int version = LittleEndian_getDW(data, 4);
	if (version != 0) {
		WARNING("Unexpected PassRegister version %d", version);
		return;
	}
	size_t raw_size = LittleEndian_getDW(data, 8);
	size_t compressed_size = LittleEndian_getDW(data, 12);
	if (compressed_size > data_len - PSR_HEADER_SIZE || PSR_HEADER_SIZE + compressed_size != data_len) {
		WARNING("Broken PassRegister file");
		return;
	}
	uint8_t *raw = xmalloc(raw_size > 0 ? raw_size : 1);
	if (!zlib_decompress_exact(raw, raw_size, data + PSR_HEADER_SIZE, compressed_size)) {
		WARNING("PassRegister: failed to uncompress");
		free(raw);
		return;
	}
	if (raw_size < 8) {
		WARNING("Broken PassRegister payload");
		free(raw);
		return;
	}

	struct buffer buf;
	buffer_init(&buf, raw, raw_size);
	uint32_t nr_ints = (uint32_t)buffer_read_int32(&buf);
	if ((size_t)nr_ints > (raw_size - buf.index) / 4) {
		WARNING("Broken PassRegister integer count");
		free(raw);
		return;
	}
	reg->nr_integers = nr_ints;
	reg->integers = reg->nr_integers ? xcalloc(reg->nr_integers, sizeof(int)) : NULL;
	for (size_t i = 0; i < reg->nr_integers; i++) {
		reg->integers[i] = buffer_read_int32(&buf);
	}
	if (buf.index + 4 > raw_size) {
		WARNING("Broken PassRegister string section");
		free_register_entries(reg);
		free(raw);
		return;
	}
	uint32_t nr_strs = (uint32_t)buffer_read_int32(&buf);
	if ((size_t)nr_strs > raw_size - buf.index) {
		WARNING("Broken PassRegister string count");
		free_register_entries(reg);
		free(raw);
		return;
	}
	reg->nr_strings = nr_strs;
	reg->strings = reg->nr_strings ? xcalloc(reg->nr_strings, sizeof(char*)) : NULL;
	for (size_t i = 0; i < reg->nr_strings; i++) {
		if (buf.index >= raw_size || !memchr(buf.buf + buf.index, 0, raw_size - buf.index)) {
			WARNING("Broken PassRegister string entry");
			reg->nr_strings = i;
			free_register_entries(reg);
			free(raw);
			return;
		}
		reg->strings[i] = strdup(buffer_skip_string(&buf));
	}

	free(raw);
}

static void read_register_json(struct passregister *reg, char *data)
{
	// parse/validate JSON data
	cJSON *obj = cJSON_Parse(data);
	if (!cJSON_IsObject(obj)) {
		WARNING("Broken PassRegister JSON file");
		cJSON_Delete(obj);
		return;
	}
	cJSON *integers = cJSON_GetObjectItem(obj, "integers");
	cJSON *strings = cJSON_GetObjectItem(obj, "strings");
	if (!cJSON_IsArray(integers) || !cJSON_IsArray(strings)) {
		WARNING("Broken PassRegister JSON structure");
		cJSON_Delete(obj);
		return;
	}

	reg->nr_integers = cJSON_GetArraySize(integers);
	reg->integers = reg->nr_integers ? xcalloc(reg->nr_integers, sizeof(int)) : NULL;
	for (size_t i = 0; i < reg->nr_integers; i++) {
		cJSON *value = cJSON_GetArrayItem(integers, i);
		if (!cJSON_IsNumber(value)) {
			WARNING("Broken PassRegister JSON integer");
			free_register_entries(reg);
			cJSON_Delete(obj);
			return;
		}
		reg->integers[i] = value->valueint;
	}
	reg->nr_strings = cJSON_GetArraySize(strings);
	reg->strings = reg->nr_strings ? xcalloc(reg->nr_strings, sizeof(char*)) : NULL;
	for (size_t i = 0; i < reg->nr_strings; i++) {
		cJSON *value = cJSON_GetArrayItem(strings, i);
		if (!cJSON_IsString(value)) {
			WARNING("Broken PassRegister JSON string");
			reg->nr_strings = i;
			free_register_entries(reg);
			cJSON_Delete(obj);
			return;
		}
		reg->strings[i] = strdup(cJSON_GetStringValue(value));
	}

	cJSON_Delete(obj);
}

static void read_register(unsigned handle, const char *filename)
{
	struct passregister *reg = registers + handle;
	free_register_entries(reg);
	free(reg->filename);
	reg->filename = savedir_path(filename);

	size_t data_len;
	char *data = file_read(reg->filename, &data_len);
	if (!data)
		return;
	if (data_len >= 4 && !memcmp(data, "PSR", 4)) {
		read_register_psr(reg, (uint8_t *)data, data_len);
	} else {
		// JSON format created by old versions of xsystem4
		read_register_json(reg, data);
	}
	free(data);

	// Make sure the arrays are sorted.
	if (reg->nr_integers > 1)
		qsort(reg->integers, reg->nr_integers, sizeof(int), compare_int);
	if (reg->nr_strings > 1)
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
	if (!reg->nr_integers)
		return NULL;
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
	if (!reg->nr_strings)
		return NULL;
	char **exact = bsearch(&text, reg->strings, reg->nr_strings, sizeof(char*), compare_string);
	if (exact)
		return exact;
	for (size_t i = 0; i < reg->nr_strings; i++) {
		if (save_symbols_equal(reg->strings[i], text))
			return &reg->strings[i];
	}
	return NULL;
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
		free(*found);
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

	free_register_entries(reg);
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
		free_register_entries(&registers[i]);
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
