/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * Credit to SLC for reverse engineering AIN formats.
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "little_endian.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static const char *errtab[AIN_MAX_ERROR] = {
	[AIN_SUCCESS]             = "Success",
	[AIN_FILE_ERROR]          = "Error opening AIN file",
	[AIN_UNRECOGNIZED_FORMAT] = "Unrecognized or invalid AIN format",
	[AIN_INVALID]             = "Invalid AIN file"
};

const char *ain_strerror(int error)
{
	if (error < AIN_MAX_ERROR)
		return errtab[error];
	return "Invalid error code";
}

const char *ain_strtype(struct ain *ain, enum ain_data_type type, int struct_type)
{
	static char buf[1024] = { [1023] = '\0' };
	switch (type) {
	case AIN_VOID:                return "void";
	case AIN_INT:                 return "int";
	case AIN_FLOAT:               return "float";
	case AIN_STRING:              return "string";
	case AIN_STRUCT:
		if (struct_type == -1 || !ain)
			return "struct";
		return ain->structures[struct_type].name;
	case AIN_ARRAY_INT:           return "array@int";
	case AIN_ARRAY_FLOAT:         return "array@float";
	case AIN_ARRAY_STRING:        return "array@string";
	case AIN_ARRAY_STRUCT:
		if (struct_type == -1 || !ain)
			return "array@struct";
		snprintf(buf, 1023, "array@%s", ain->structures[struct_type].name);
		return buf;
	case AIN_REF_INT:             return "ref int";
	case AIN_REF_FLOAT:           return "ref float";
	case AIN_REF_STRING:          return "ref string";
	case AIN_REF_STRUCT:
		if (struct_type == -1 || !ain)
			return "ref struct";
		snprintf(buf, 1023, "ref %s", ain->structures[struct_type].name);
		return buf;
	case AIN_REF_ARRAY_INT:       return "ref array@int";
	case AIN_REF_ARRAY_FLOAT:     return "ref array@float";
	case AIN_REF_ARRAY_STRING:    return "ref array@string";
	case AIN_REF_ARRAY_STRUCT:
		if (struct_type == -1 || !ain)
			return "ref array@struct";
		snprintf(buf, 1023, "ref array@%s", ain->structures[struct_type].name);
		return buf;
	case AIN_IMAIN_SYSTEM:        return "imain_system";
	case AIN_FUNC_TYPE:           return "functype";
	case AIN_ARRAY_FUNC_TYPE:     return "array@functype";
	case AIN_REF_FUNC_TYPE:       return "ref functype";
	case AIN_REF_ARRAY_FUNC_TYPE: return "ref array@functype";
	case AIN_BOOL:                return "bool";
	case AIN_ARRAY_BOOL:          return "array@bool";
	case AIN_REF_BOOL:            return "ref bool";
	case AIN_REF_ARRAY_BOOL:      return "ref array@bool";
	case AIN_LONG_INT:            return "lint";
	case AIN_ARRAY_LONG_INT:      return "array@lint";
	case AIN_REF_LONG_INT:        return "ref lint";
	case AIN_REF_ARRAY_LONG_INT:  return "ref array@lint";
	case AIN_DELEGATE:            return "delegate";
	case AIN_ARRAY_DELEGATE:      return "array@delegate";
	case AIN_REF_ARRAY_DELEGATE:  return "ref array@delegate";
	case AIN_UNKNOWN_TYPE_67:     return "type_67";
	case AIN_UNKNOWN_TYPE_74:     return "type_74";
	case AIN_UNKNOWN_TYPE_75:     return "type_75";
	case AIN_ARRAY:               return "array<?>";
	case AIN_REF_ARRAY:           return "ref array<?>";
	case AIN_UNKNOWN_TYPE_82:     return "type_82";
	case AIN_UNKNOWN_TYPE_86:     return "type_86";
	case AIN_UNKNOWN_TYPE_89:     return "type_89";
	case AIN_UNKNOWN_TYPE_92:     return "type_92";
	case AIN_UNKNOWN_TYPE_93:     return "type_93";
	case AIN_UNKNOWN_TYPE_95:     return "type_95";
	default:
		WARNING("Unknown type: %d", type);
		snprintf(buf, 1023, "unknown_type_%d", type);
		return buf;
	}
}

const char *ain_variable_to_string(struct ain *ain, struct ain_variable *v)
{
	static char buf[2048] = { [2047] = '\0' };
	const char *type = ain_strtype(ain, v->data_type, v->struct_type);

	int i = snprintf(buf, 2047, "%s %s", type, v->name);
	if (v->has_initval) {
		switch (v->data_type) {
		case AIN_STRING: {
			// FIXME: string needs escaping
			snprintf(buf+i, 2047-i, " = \"%s\"", v->initval.s);
			break;
		}
		case AIN_DELEGATE:
		case AIN_REF_TYPE:
			break;
		case AIN_FLOAT:
			snprintf(buf+i, 2047-i, " = %f", v->initval.f);
			break;
		default:
			snprintf(buf+i, 2047-i, " = %d", v->initval.i);
			break;
		}
	}
	return buf;
}

struct ain_reader {
	uint8_t *buf;
	size_t size;
	size_t index;
};

static int32_t read_int32(struct ain_reader *r)
{
	int32_t v = LittleEndian_getDW(r->buf, r->index);
	r->index += 4;
	return v;
}

static uint8_t *read_bytes(struct ain_reader *r, size_t len)
{
	uint8_t *bytes = xmalloc(len);
	memcpy(bytes, r->buf + r->index, len);
	r->index += len;
	return bytes;
}

static char *read_string(struct ain_reader *r)
{
	char *str = strdup((char*)r->buf + r->index);
	r->index += strlen(str) + 1;
	return str;
}

// TODO: should just memcpy the whole section and create pointers into it
//       e.g. char *_messages; // raw AIN section
//            char **messages; // array of pointers into _messages
static char **read_strings(struct ain_reader *r, int count)
{
	char **strings = calloc(count, sizeof(char*));
	for (int i = 0; i < count; i++) {
		strings[i] = read_string(r);
	}
	return strings;
}

static struct string *read_vm_string(struct ain_reader *r)
{
	size_t len = strlen((char*)r->buf + r->index);
	struct string *s = make_string((char*)r->buf + r->index, len);
	s->cow = true;
	r->index += len + 1;
	return s;
}

static struct string **read_vm_strings(struct ain_reader *r, int count)
{
	struct string **strings = calloc(count, sizeof(struct string*));
	for (int i = 0; i < count; i++) {
		strings[i] = read_vm_string(r);
	}
	return strings;
}

static struct string *read_msg1_string(struct ain_reader *r)
{
	int32_t len = read_int32(r);
	struct string *s = make_string((char*)r->buf + r->index, len);
	s->cow = true;
	r->index += len;

	// why...
	for (int i = 0; i < len; i++) {
		s->text[i] -= (uint8_t)i;
		s->text[i] -= 0x60;
	}

	return s;
}

static struct string **read_msg1_strings(struct ain_reader *r, int count)
{
	struct string **strings = calloc(count, sizeof(struct string*));
	for (int i = 0; i < count; i++) {
		strings[i] = read_msg1_string(r);
	}
	return strings;
}

static void read_ainv8_variable_data(struct ain_reader *r, struct ain_variable *v)
{
	// read array type
	if (v->data_type == 79 || v->data_type == 80 || v->data_type == 82) {
		v->array_data_type = read_int32(r);
		v->array_struct_type = read_int32(r);
		v->array_array_rank = read_int32(r);
		if (v->array_data_type == 79) {
			v->array2_data_type = read_int32(r);
			v->array2_struct_type = read_int32(r);
			v->array2_array_rank = read_int32(r);
		}
		// FIXME: Probably this is an array of array_array_rank + 1 elements.
		//        An array<array<array<...>>> would likely break this.
	}

	// read initval
	if ((v->has_initval = read_int32(r))) {
		if (v->has_initval != 1)
			ERROR("variable->has_initval is not boolean? %d (at %p)", v->has_initval, r->index - 4);
		switch (v->data_type) {
		case AIN_STRING:
			v->initval.s = read_string(r);
			break;
		case AIN_DELEGATE:
		case AIN_REF_TYPE:
			break;
		default:
			v->initval.i = read_int32(r);
		}
	}
}

static struct ain_variable *read_variables(struct ain_reader *r, int count, struct ain *ain, enum ain_variable_type var_type)
{
	struct ain_variable *variables = calloc(count, sizeof(struct ain_variable));
	for (int i = 0; i < count; i++) {
		variables[i].var_type = var_type;
		variables[i].name = read_string(r);
		if (ain->version >= 12)
			variables[i].name2 = read_string(r); // duplicate name?
		variables[i].data_type = read_int32(r);
		variables[i].struct_type = read_int32(r);
		variables[i].array_dimensions = read_int32(r);
		if (ain->version >= 8)
			read_ainv8_variable_data(r, &variables[i]);
	}
	return variables;
}

static struct ain_function *read_functions(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_function *funs = calloc(count, sizeof(struct ain_function));
	for (int i = 0; i < count; i++) {
		funs[i].address = read_int32(r);
		funs[i].name = read_string(r);
		if (!strcmp(funs[i].name, "0"))
			ain->alloc = i;
		if (ain->version > 0 && ain->version < 7)
			funs[i].is_label = read_int32(r);
		funs[i].data_type = read_int32(r);
		funs[i].struct_type = read_int32(r);

		if (ain->version >= 11) {
			funs[i].uk_data_exists = read_int32(r); // known values: 0, 1
			if (funs[i].uk_data_exists == 1) {
				// NOTE: only present for return type in (79,80,86)
				funs[i].uk_data_type   = read_int32(r);
				funs[i].uk_struct_type = read_int32(r);
				funs[i].uk_array_rank  = read_int32(r);
			} else if (funs[i].uk_data_exists) {
				ERROR("function->uk_data_exists is not a boolean? %d (at %p)", funs[i].uk_data_exists, r->index - 4);
			}
		}

		funs[i].nr_args = read_int32(r);
		funs[i].nr_vars = read_int32(r);

		if (ain->version >= 11) {
			funs[i].is_lambda = read_int32(r); // known values: 0, 1
			if (funs[i].is_lambda && funs[i].is_lambda != 1) {
				ERROR("function->is_lambda is not a boolean? %d (at %p)", funs[i].is_lambda, r->index - 4);
			}
		}

		if (ain->version > 0) {
			funs[i].crc = read_int32(r);
		}
		if (funs[i].nr_vars > 0) {
			funs[i].vars = read_variables(r, funs[i].nr_vars, ain, AIN_VAR_LOCAL);
		}
	}
	return funs;
}

static struct ain_global *read_globals(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_global *globals = calloc(count, sizeof(struct ain_global));
	for (int i = 0; i < count; i++) {
		globals[i].name = read_string(r);
		if (ain->version >= 12)
			globals[i].name2 = read_string(r);
		globals[i].data_type = read_int32(r);
		globals[i].struct_type = read_int32(r);
		globals[i].array_dimensions = read_int32(r);
		if (globals[i].data_type == AIN_ARRAY) {
			globals[i].array_data_type = read_int32(r); // array data type?
			globals[i].array_struct_type = read_int32(r); // always matches struct_type?
			globals[i].array_array_rank = read_int32(r); // always 0?
		}
		if (ain->version >= 5) {
			globals[i].group_index = read_int32(r);
		}
	}
	return globals;
}

static struct ain_initval *read_initvals(struct ain_reader *r, int count)
{
	struct ain_initval *values = calloc(count, sizeof(struct ain_initval));
	for (int i = 0; i < count; i++) {
		values[i].global_index = read_int32(r);
		values[i].data_type = read_int32(r);
		if (values[i].data_type == AIN_STRING) {
			values[i].string_value = read_string(r);
		} else {
			values[i].int_value = read_int32(r);
		}
	}
	return values;
}

static struct ain_struct *read_structures(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_struct *structures = calloc(count, sizeof(struct ain_struct));
	for (int i = 0; i < count; i++) {
		structures[i].name = read_string(r);
		if (ain->version >= 11) {
			structures[i].nr_interfaces = read_int32(r);
			structures[i].interfaces = xcalloc(structures[i].nr_interfaces, sizeof(struct ain_interface));
			for (int j = 0; j < structures[i].nr_interfaces; j++) {
				structures[i].interfaces[j].struct_type = read_int32(r);
				structures[i].interfaces[j].uk = read_int32(r);
			}
		}
		structures[i].constructor = read_int32(r);
		structures[i].destructor = read_int32(r);
		structures[i].nr_members = read_int32(r);
		structures[i].members = read_variables(r, structures[i].nr_members, ain, AIN_VAR_MEMBER);
	}
	return structures;
}

static struct ain_hll_argument *read_hll_arguments(struct ain_reader *r, int count)
{
	struct ain_hll_argument *arguments = calloc(count, sizeof(struct ain_hll_argument));
	for (int i = 0; i < count; i++) {
		arguments[i].name = read_string(r);
		arguments[i].data_type = read_int32(r);
	}
	return arguments;
}

static struct ain_hll_function *read_hll_functions(struct ain_reader *r, int count)
{
	struct ain_hll_function *functions = calloc(count, sizeof(struct ain_hll_function));
	for (int i = 0; i < count; i++) {
		functions[i].name = read_string(r);
		functions[i].data_type = read_int32(r);
		functions[i].nr_arguments = read_int32(r);
		functions[i].arguments = read_hll_arguments(r, functions[i].nr_arguments);
	}
	return functions;
}

static struct ain_library *read_libraries(struct ain_reader *r, int count)
{
	struct ain_library *libraries = calloc(count, sizeof(struct ain_library));
	for (int i = 0; i < count; i++) {
		libraries[i].name = read_string(r);
		libraries[i].nr_functions = read_int32(r);
		libraries[i].functions = read_hll_functions(r, libraries[i].nr_functions);
	}
	return libraries;
}

static struct ain_switch_case *read_switch_cases(struct ain_reader *r, int count)
{
	struct ain_switch_case *cases = calloc(count, sizeof(struct ain_switch));
	for (int i = 0; i < count; i++) {
		cases[i].value = read_int32(r);
		cases[i].address = read_int32(r);
	}
	return cases;
}

static struct ain_switch *read_switches(struct ain_reader *r, int count)
{
	struct ain_switch *switches = calloc(count, sizeof(struct ain_switch));
	for (int i = 0; i < count; i++) {
		switches[i].case_type = read_int32(r);
		switches[i].default_address = read_int32(r);
		switches[i].nr_cases = read_int32(r);
		switches[i].cases = read_switch_cases(r, switches[i].nr_cases);
	}
	return switches;
}

static struct ain_function_type *read_function_types(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_function_type *types = calloc(count, sizeof(struct ain_function_type));
	for (int i = 0; i < count; i++) {
		types[i].name = read_string(r);
		types[i].data_type = read_int32(r);
		types[i].struct_type = read_int32(r);
		if (ain->version >= 11) {
			types[i].uk_data_exists = read_int32(r);
			if (types[i].uk_data_exists == 1) {
				types[i].uk_data_type   = read_int32(r);
				types[i].uk_struct_type = read_int32(r);
				types[i].uk_array_rank  = read_int32(r);
			} else if (types[i].uk_data_exists) {
				ERROR("functype->uk_data_exists is not a boolean? %d (at %p)", types[i].uk_data_exists, r->index - 4);
			}
		}
		types[i].nr_arguments = read_int32(r);
		types[i].nr_variables = read_int32(r);
		types[i].variables = read_variables(r, types[i].nr_variables, ain, AIN_VAR_LOCAL);
	}
	return types;
}

static bool read_tag(struct ain_reader *r, struct ain *ain)
{
	if (r->index + 4 >= r->size)
		return false;

	uint8_t *tag_loc = r->buf + r->index;
	r->index += 4;

#define TAG_EQ(tag) !strncmp((char*)tag_loc, tag, 4)
	// FIXME: need to check len or could segfault on currupt AIN file
	if (TAG_EQ("VERS")) {
		ain->version = read_int32(r);
		if (ain->version > 8) {
			instructions[CALLHLL].nr_args = 3;
			instructions[S_MOD].nr_args = 1;
			instructions[OBJSWAP].nr_args = 1;
			instructions[DG_STR_TO_METHOD].nr_args = 1;
		}
	} else if (TAG_EQ("KEYC")) {
		ain->keycode = read_int32(r);
	} else if (TAG_EQ("CODE")) {
		ain->code_size = read_int32(r);
		ain->code = read_bytes(r, ain->code_size);
	} else if (TAG_EQ("FUNC")) {
		ain->nr_functions = read_int32(r);
		ain->functions = read_functions(r, ain->nr_functions, ain);
	} else if (TAG_EQ("GLOB")) {
		ain->nr_globals = read_int32(r);
		// FIXME: why off by one?
		if (ain->version >= 12)
			ain->nr_globals++;
		ain->globals = read_globals(r, ain->nr_globals, ain);
	} else if (TAG_EQ("GSET")) {
		ain->nr_initvals = read_int32(r);
		ain->global_initvals = read_initvals(r, ain->nr_initvals);
	} else if (TAG_EQ("STRT")) {
		ain->nr_structures = read_int32(r);
		ain->structures = read_structures(r, ain->nr_structures, ain);
	} else if (TAG_EQ("MSG0")) {
		ain->nr_messages = read_int32(r);
		ain->messages = read_vm_strings(r, ain->nr_messages);
	} else if (TAG_EQ("MSG1")) {
		ain->nr_messages = read_int32(r);
		read_int32(r); // ???
		ain->messages = read_msg1_strings(r, ain->nr_messages);
	} else if (TAG_EQ("MAIN")) {
		ain->main = read_int32(r);
	} else if (TAG_EQ("MSGF")) {
		ain->msgf = read_int32(r);
	} else if (TAG_EQ("HLL0")) {
		ain->nr_libraries = read_int32(r);
		ain->libraries = read_libraries(r, ain->nr_libraries);
	} else if (TAG_EQ("SWI0")) {
		ain->nr_switches = read_int32(r);
		ain->switches = read_switches(r, ain->nr_switches);
	} else if (TAG_EQ("GVER")) {
		ain->game_version = read_int32(r);
	} else if (TAG_EQ("STR0")) {
		ain->nr_strings = read_int32(r);
		ain->strings = read_vm_strings(r, ain->nr_strings);
	} else if (TAG_EQ("FNAM")) {
		ain->nr_filenames = read_int32(r);
		ain->filenames = read_strings(r, ain->nr_filenames);
	} else if (TAG_EQ("OJMP")) {
		ain->ojmp = read_int32(r);
	} else if (TAG_EQ("FNCT")) {
		read_int32(r); // ???
		ain->nr_function_types = read_int32(r);
		ain->function_types = read_function_types(r, ain->nr_function_types, ain);
	} else if (TAG_EQ("DELG")) {
		read_int32(r); // ???
		ain->nr_delegates = read_int32(r);
		ain->delegates = read_function_types(r, ain->nr_delegates, ain);
	} else if (TAG_EQ("OBJG")) {
		ain->nr_global_groups = read_int32(r);
		ain->global_group_names = read_strings(r, ain->nr_global_groups);
	} else if (TAG_EQ("ENUM")) {
		ain->nr_enums = read_int32(r);
		ain->enums = read_strings(r, ain->nr_enums);
	} else {
		return false;
	}
#undef TAG_EQ

	return true;
}

static uint8_t *decompress_ain(uint8_t *in, long *len)
{
	uint8_t *out;
	long out_len = LittleEndian_getDW(in, 8);
	long in_len  = LittleEndian_getDW(in, 12);

	if (out_len < 0 || in_len < 0)
		return NULL;

	out = xmalloc(out_len);
	if (Z_OK != uncompress(out, (unsigned long*)&out_len, in+16, in_len)) {
		free(out);
		return NULL;
	}

	*len = out_len;
	return out;
}

static uint32_t temper(uint32_t in)
{
	in ^= (in >> 11);
	in ^= (in << 7)  & 0x9D2C5680;
	in ^= (in << 15) & 0xEFC60000;
	in ^= (in >> 18);
	return in;
}

static void update(uint32_t *state)
{
	int ahead = 0x18D;
	int write = 0;
	int read = 2;
	uint32_t current = state[1];
	uint32_t last = state[0];
	uint32_t tmp;

	for (int i = 0; i < 0x270; i++) {
		tmp = (((current ^ last) & 0x7FFFFFFE) ^ last) >> 1;
		tmp = (current & 1) ? (tmp ^ 0x9908B0DF) : tmp;
		state[write] = tmp ^ state[ahead];
		last = current;
		current = state[read];
		write++;
		read++;
		ahead++;
		while (read >= 0x270)
			read -= 0x270;
		while (ahead >= 0x270)
			ahead -= 0x270;
	}
}

static void decrypt_ain(uint8_t *buf, size_t len)
{
	uint32_t state[0x270];
	uint32_t key = 0x5D3E3;
	int off = 0;

	for (int i = 0; i < 0x270; i++) {
		state[i] = key;
		key *= 0x10DCD;
	}

	update(state);
	for (size_t i = 0; i < len; i++) {
		if (off >= 0x270) {
			update(state);
			off = 0;
		}
		buf[i] ^= (uint8_t)(temper(state[off]) & 0xFF);
		off++;
	}
}

static bool ain_is_encrypted(uint8_t *buf)
{
	uint8_t magic[8];

	memcpy(magic, buf, 8);
	decrypt_ain(magic, 8);

	return !strncmp((char*)magic, "VERS", 4) && !magic[5] && !magic[6] && !magic[7];
}

uint8_t *ain_read(const char *path, long *len, int *error)
{
	FILE *fp;
	uint8_t *buf = NULL;

	if (!(fp = fopen(path, "rb"))) {
		*error = AIN_FILE_ERROR;
		goto err;
	}

	// get size of AIN file
	fseek(fp, 0, SEEK_END);
	*len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// read AIN file into memory
	buf = xmalloc(*len + 4); // why +4?
	fread(buf, 1, *len, fp);
	fclose(fp);

	// check magic
	if (!strncmp((char*)buf, "AI2\0\0\0\0", 8)) {
		uint8_t *uc = decompress_ain(buf, len);
		if (!uc) {
			*error = AIN_INVALID;
			goto err;
		}
		free(buf);
		buf = uc;
	} else if (ain_is_encrypted(buf)) {
		decrypt_ain(buf, *len);
	} else {
		printf("%.4s\n", buf);
		*error = AIN_UNRECOGNIZED_FORMAT;
		goto err;
	}

	return buf;
err:
	free(buf);
	return NULL;
}

struct ain *ain_open(const char *path, int *error)
{
	long len;
	struct ain *ain = NULL;
	uint8_t *buf = ain_read(path, &len, error);

	// read data into ain struct
	struct ain_reader r = {
		.buf = buf,
		.index = 0,
		.size = len
	};
	ain = calloc(1, sizeof(struct ain));
	while (read_tag(&r, ain));
	if (!ain->version) {
		*error = AIN_INVALID;
		goto err;
	}

	free(buf);
	*error = AIN_SUCCESS;
	return ain;
err:
	free(buf);
	free(ain);
	return NULL;
}

static void ain_free_variables(struct ain_variable *vars, int nr_vars)
{
	for (int i = 0; i < nr_vars; i++) {
		free(vars[i].name);
		free(vars[i].name2);
		if (vars[i].has_initval && vars[i].data_type == AIN_STRING)
			free(vars[i].initval.s);
	}
	free(vars);
}

static void ain_free_function_types(struct ain_function_type *funs, int n)
{
	for (int i = 0; i < n; i++) {
		free(funs[i].name);
		ain_free_variables(funs[i].variables, funs[i].nr_variables);
	}
	free(funs);
}

static void ain_free_strings(struct string **strings, int n)
{
	for (int i = 0; i < n; i++) {
		free_string(strings[i]);
	}
	free(strings);
}

static void ain_free_cstrings(char **strings, int n)
{
	for (int i = 0; i < n; i++) {
		free(strings[i]);
	}
	free(strings);
}

void ain_free(struct ain *ain)
{
	free(ain->code);

	for (int f = 0; f < ain->nr_functions; f++) {
		free(ain->functions[f].name);
		ain_free_variables(ain->functions[f].vars, ain->functions[f].nr_vars);
	}
	free(ain->functions);

	for (int i = 0; i < ain->nr_globals; i++) {
		free(ain->globals[i].name);
		free(ain->globals[i].name2);
	}
	free(ain->globals);

	for (int i = 0; i < ain->nr_initvals; i++) {
		if (ain->global_initvals[i].data_type != AIN_STRING)
			continue;
		free(ain->global_initvals[i].string_value);
	}
	free(ain->global_initvals);

	for (int s = 0; s < ain->nr_structures; s++) {
		free(ain->structures[s].name);
		free(ain->structures[s].interfaces);
		ain_free_variables(ain->structures[s].members, ain->structures[s].nr_members);
	}
	free(ain->structures);

	for (int lib = 0; lib < ain->nr_libraries; lib++) {
		free(ain->libraries[lib].name);
		for (int f = 0; f < ain->libraries[lib].nr_functions; f++) {
			free(ain->libraries[lib].functions[f].name);
			for (int a = 0; a < ain->libraries[lib].functions[f].nr_arguments; a++) {
				free(ain->libraries[lib].functions[f].arguments[a].name);
			}
			free(ain->libraries[lib].functions[f].arguments);
		}
		free(ain->libraries[lib].functions);
	}
	free(ain->libraries);

	for (int i = 0; i < ain->nr_switches; i++) {
		free(ain->switches[i].cases);
	}
	free(ain->switches);

	ain_free_function_types(ain->function_types, ain->nr_function_types);
	ain_free_function_types(ain->delegates, ain->nr_delegates);

	ain_free_strings(ain->strings, ain->nr_strings);
	ain_free_strings(ain->messages, ain->nr_messages);

	ain_free_cstrings(ain->filenames, ain->nr_filenames);
	ain_free_cstrings(ain->global_group_names, ain->nr_global_groups);
	ain_free_cstrings(ain->enums, ain->nr_enums);

	free(ain);
}
