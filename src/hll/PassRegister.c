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
#include <string.h>
#include <errno.h>
#include "cJSON.h"

#include "system4.h"
#include "system4/string.h"
#include "system4/file.h"

#include "hll.h"
#include "xsystem4.h"

/*
 * Special SaveData API for savings sets of integers and strings
 * (e.g. unlocked CGs).
 */

struct passregister {
	char *filename;
	size_t nr_integers;
	int *integers;
	size_t nr_strings;
	char **strings;
};

static size_t nr_registers = 0;
static struct passregister *registers = NULL;

static cJSON *register_to_json(unsigned handle)
{
	struct passregister *reg = registers + handle;
	cJSON *obj = cJSON_CreateObject();
	cJSON *integers = cJSON_CreateArray();
	cJSON *strings = cJSON_CreateArray();

	for (size_t i = 0; i < reg->nr_integers; i++) {
		cJSON_AddItemToArray(integers, cJSON_CreateNumber(reg->integers[i]));
	}
	for (size_t i = 0; i < reg->nr_strings; i++) {
		cJSON_AddItemToArray(strings, cJSON_CreateString(reg->strings[i]));
	}

	cJSON_AddItemToObject(obj, "integers", integers);
	cJSON_AddItemToObject(obj, "strings", strings);
	return obj;
}

static void write_register(unsigned handle)
{
	struct passregister *reg = registers + handle;

	FILE *f = file_open_utf8(reg->filename, "wb");
	if (!f) {
		WARNING("Failed to open PassRegister file: %s: %s", display_utf0(reg->filename), strerror(errno));
		return;
	}

	cJSON *json = register_to_json(handle);
	char *str = cJSON_Print(json);
	if (fwrite(str, strlen(str), 1, f) != 1) {
		WARNING("Failed to write PassRegister file: %s: %s", display_utf0(reg->filename), strerror(errno));
		goto end;
	}
	if (fclose(f)) {
		WARNING("Error writing to save file: %s: %s", display_utf0(reg->filename), strerror(errno));
		goto end;
	}
end:
	free(str);
	cJSON_Delete(json);
}

static cJSON *_type_check(const char *file, const char *func, int line, int type, cJSON *json)
{
	if (!json || json->type != type)
		_vm_error("*ERROR*(%s:%s:%d): Invalid PassRegister save file", file, func, line);
	return json;
}

#define type_check(type, json) _type_check(__FILE__, __func__, __LINE__, type, json)

static void read_register(unsigned handle, const char *filename)
{
	struct passregister *reg = registers + handle;
	free(reg->filename);
	reg->filename = savedir_path(filename);

	size_t data_len;
	char *data = file_read(reg->filename, &data_len);
	if (data) {
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
		free(data);
	}
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

static bool exist_number(int handle, int n)
{
	// NOTE: this could be improved by sorting the array
	struct passregister *reg = registers + handle;
	for (size_t i = 0; i < reg->nr_integers; i++) {
		if (reg->integers[i] == n)
			return true;
	}
	return false;
}

static bool PassRegister_RegistNumber(int handle, int n)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;
	if (exist_number(handle, n))
		return true;

	struct passregister *reg = registers + handle;
	reg->integers = xrealloc_array(reg->integers, reg->nr_integers, reg->nr_integers+1, sizeof(int));
	reg->integers[reg->nr_integers++] = n;
	write_register(handle);
	return true;
}

static bool exist_text(int handle, const char *text)
{
	// NOTE: this could be improved by sorting the array
	struct passregister *reg = registers + handle;
	for (size_t i = 0; i < reg->nr_strings; i++) {
		if (!strcmp(reg->strings[i], text))
			return true;
	}
	return false;
}

static bool PassRegister_RegistText(int handle, struct string *text)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;
	if (exist_text(handle, text->text))
		return true;

	struct passregister *reg = registers + handle;
	reg->strings = xrealloc_array(reg->strings, reg->nr_strings, reg->nr_strings+1, sizeof(char*));
	reg->strings[reg->nr_strings++] = strdup(text->text);
	write_register(handle);
	return true;
}

static bool PassRegister_UnregistNumber(int handle, int n)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;

	struct passregister *reg = registers + handle;
	for (size_t i = 0; i < reg->nr_integers; i++) {
		if (reg->integers[i] != n)
			continue;
		for (size_t j = i+1; j < reg->nr_integers; j++) {
			reg->integers[j-1] = reg->integers[j];
		}
		reg->nr_integers--;
		write_register(handle);
	}
	return true;
}

static bool PassRegister_UnregistText(int handle, struct string *text)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;

	struct passregister *reg = registers + handle;
	for (size_t i = 0; i < reg->nr_strings; i++) {
		if (strcmp(reg->strings[i], text->text))
			continue;
		for (size_t j = 0; j < reg->nr_strings; j++) {
			reg->strings[j-1] = reg->strings[j];
		}
		reg->nr_strings--;
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
	return exist_number(handle, n);
}

static bool PassRegister_ExistText(int handle, struct string *text)
{
	if (handle < 0 || (size_t)handle >= nr_registers || !registers[handle].filename)
		return false;
	return exist_text(handle, text->text);
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
