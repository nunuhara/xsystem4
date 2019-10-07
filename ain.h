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

#ifndef SYSTEM4_AIN_H
#define SYSTEM4_AIN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vm_string.h"

struct ain;

enum ain_error {
	AIN_SUCCESS,
	AIN_FILE_ERROR,
	AIN_UNRECOGNIZED_FORMAT,
	AIN_INVALID,
	AIN_MAX_ERROR
};

enum ain_data_type {
	AIN_VOID = 0,
	AIN_INT = 10,
	AIN_FLOAT = 11,
	AIN_STRING = 12,
	AIN_STRUCT = 13,
	AIN_ARRAY_INT = 14,
	AIN_ARRAY_FLOAT = 15,
	AIN_ARRAY_STRING = 16,
	AIN_ARRAY_STRUCT = 17,
	AIN_REF_INT = 18,
	AIN_REF_FLOAT = 19,
	AIN_REF_STRING = 20,
	AIN_REF_STRUCT = 21,
	AIN_REF_ARRAY_INT = 22,
	AIN_REF_ARRAY_FLOAT = 23,
	AIN_REF_ARRAY_STRING = 24,
	AIN_REF_ARRAY_STRUCT = 25,
	AIN_IMAIN_SYSTEM = 26,
	AIN_FUNC_TYPE = 27,
	AIN_ARRAY_FUNC_TYPE = 30,
	AIN_REF_FUNC_TYPE = 31,
	AIN_REF_ARRAY_FUNC_TYPE = 32,
	AIN_BOOL = 47,
	AIN_ARRAY_BOOL = 50,
	AIN_REF_BOOL = 51,
	AIN_REF_ARRAY_BOOL = 52,
	AIN_LONG_INT = 55,
	AIN_ARRAY_LONG_INT = 58,
	AIN_REF_LONG_INT = 59,
	AIN_REF_ARRAY_LONG_INT = 60,
	AIN_DELEGATE = 63,
	AIN_ARRAY_DELEGATE = 66,
	AIN_REF_ARRAY_DELEGATE = 69
};

struct ain_variable {
	char *name;
	int32_t data_type;
	int32_t struct_type;
	int32_t array_dimensions;
};

struct ain_function {
	int32_t address;
	char *name;
	bool is_label;
	int32_t data_type;
	int32_t struct_type;
	int32_t nr_args;
	int32_t nr_vars;
	int32_t crc;
	struct ain_variable *vars;
};

struct ain_global {
	char *name;
	int32_t data_type;
	int32_t struct_type;
	int32_t array_dimensions;
	int32_t group_index;
};

struct ain_initval {
	int32_t global_index;
	int32_t data_type;
	union {
		char *string_value;
		int32_t int_value;
	};
};

struct ain_struct {
	char *name;
	int32_t constructor;
	int32_t destructor;
	int32_t nr_members;
	struct ain_variable *members;
};

struct ain_hll_argument {
	char *name;
	int32_t data_type;
};

struct ain_hll_function {
	char *name;
	int32_t data_type;
	int32_t nr_arguments;
	struct ain_hll_argument *arguments;
};

struct ain_library {
	char *name;
	int32_t nr_functions;
	struct ain_hll_function *functions;
};

struct ain_switch_case {
	int32_t value;
	int32_t address;
};

struct ain_switch {
	int32_t case_type;
	int32_t default_address;
	int32_t nr_cases;
	struct ain_switch_case *cases;
};

struct ain_function_type {
	char *name;
	int32_t data_type;
	int32_t struct_type;
	int32_t nr_arguments;
	int32_t nr_variables;
	struct ain_variable *variables;
};

struct ain {
	char *ain_path;
	int32_t version;
	int32_t keycode;
	uint8_t *code;
	size_t code_size;
	struct ain_function *functions;
	struct ain_global *globals;
	struct ain_initval *global_initvals;
	struct ain_struct *structures;
	struct string **messages;
	int32_t main;
	int32_t msgf;
	struct ain_library *libraries;
	struct ain_switch *switches;
	int32_t game_version;
	struct string **strings;
	char **filenames;
	int32_t ojmp;
	struct ain_function_type *function_types;
	//delegates
	char **global_group_names;
};

const char *ain_strerror(int error);
struct ain *ain_open(const char *path, int *error);
void ain_free(struct ain *ain);
void vm_execute(struct ain *program);

#endif /* SYSTEM4_AIN_H */
