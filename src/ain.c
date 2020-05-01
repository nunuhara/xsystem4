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

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "khash.h"
#include "little_endian.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

struct func_list {
	int nr_slots;
	struct ain_function *slots[];
};
#define func_list_size(nr_slots) (sizeof(struct func_list) + sizeof(struct ain_function*)*(nr_slots))

KHASH_MAP_INIT_STR(func_ht, struct func_list*);
KHASH_MAP_INIT_STR(struct_ht, struct ain_struct*);

static void init_func_ht(struct ain *ain)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		int ret;
		khiter_t k = kh_put(func_ht, ain->_func_ht, ain->functions[i].name, &ret);
		if (!ret) {
			// entry with this name already exists: add to list
			struct func_list *list = kh_value(ain->_func_ht, k);
			list = xrealloc(list, func_list_size(list->nr_slots+1));
			list->slots[list->nr_slots] = &ain->functions[i];
			list->nr_slots++;
			kh_value(ain->_func_ht, k) = list;
		} else if (ret == 1) {
			// empty bucket: create list
			struct func_list *list = xmalloc(func_list_size(1));
			list->nr_slots = 1;
			list->slots[0] = &ain->functions[i];
			kh_value(ain->_func_ht, k) = list;
		} else {
			ERROR("Failed to insert function into hash table (%d)", ret);
		}
	}
}

static void init_struct_ht(struct ain *ain)
{
	for (int i = 0; i < ain->nr_structures; i++) {
		int ret;
		khiter_t k = kh_put(struct_ht, ain->_struct_ht, ain->structures[i].name, &ret);
		if (!ret) {
			ERROR("Duplicate structure names: '%s'", ain->structures[i].name);
		} else if (ret == 1) {
			kh_value(ain->_struct_ht, k) = &ain->structures[i];
		} else {
			ERROR("Failed to insert struct into hash table (%d)", ret);
		}
	}
}

static bool function_is_member_of(char *func_name, char *struct_name)
{
	while (*func_name && *struct_name && *func_name != '@' && *func_name == *struct_name) {
		func_name++;
		struct_name++;
	}
	return !*struct_name && *func_name == '@';
}

/*
 * Returns true if func_name contains the '@' character.
 * We can't use strchr here because '@' is valid as the second byte of a SJIS character.
 */
static bool is_member_function(char *func_name)
{
	while (*func_name) {
		if (*func_name == '@')
			return true;
		if (SJIS_2BYTE(*func_name)) {
			func_name++;
			if (*func_name)
				func_name++;
		} else {
			func_name++;
		}
	}
	return false;
}

/*
 * Infer struct member functions from function names.
 */
static void init_member_functions(struct ain *ain)
{
	for (int f = 0; f < ain->nr_functions; f++) {
		ain->functions[f].struct_type = -1;
		ain->functions[f].enum_type = -1;
		char *name = ain->functions[f].name;
		if (!is_member_function(name))
			continue;
		for (int s = 0; s < ain->nr_structures; s++) {
			if (function_is_member_of(name, ain->structures[s].name)) {
				ain->functions[f].struct_type = s;
				break;
			}
		}
		if (ain->functions[f].struct_type != -1)
			continue;
		// check enums
		for (int e = 0; e < ain->nr_enums; e++) {
			if (function_is_member_of(name, ain->enums[e].name)) {
				ain->functions[f].enum_type = e;
				break;
			}
		}
		if (ain->functions[f].enum_type == -1) {
			char *u = sjis2utf(name, 0);
			WARNING("Failed to find struct type for function \"%s\"", u);
			free(u);
		}
	}
}

static struct func_list *get_function(struct ain *ain, const char *name)
{
	int ret;
	khiter_t k = kh_put(func_ht, ain->_func_ht, name, &ret);
	if (ret)
		return NULL;
	return kh_value(ain->_func_ht, k);
}

struct ain_function *ain_get_function(struct ain *ain, char *name)
{
	size_t len;
	long n = 0;

	// handle name#index syntax
	for (len = 0; name[len]; len++) {
		if (name[len] == '#') {
			char *endptr;
			n = strtol(name+len+1, &endptr, 10);
			if (!name[len+1] || *endptr || n < 0)
				ERROR("Invalid function name: '%s'", name);
			name[len] = '\0';
			break;
		}
	}

	struct func_list *funs = get_function(ain, name);
	if (!funs || n >= funs->nr_slots)
		return NULL;
	return funs->slots[n];
}

int ain_get_function_index(struct ain *ain, struct ain_function *f)
{
	struct func_list *funs = get_function(ain, f->name);
	if (!funs)
		goto err;

	for (int i = 0; i < funs->nr_slots; i++) {
		if (funs->slots[i] == f)
			return i;
	}
err:
	ERROR("Invalid function: '%s'", f->name);
}

struct ain_struct *ain_get_struct(struct ain *ain, char *name)
{
	khiter_t k = kh_get(struct_ht, ain->_struct_ht, name);
	if (k == kh_end(ain->_struct_ht))
		return NULL;
	return kh_value(ain->_struct_ht, k);
}

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
	struct ain_type t = { .data = type, .struc = struct_type, .rank = 0 };
	char *str = ain_strtype_d(ain, &t);
	strncpy(buf, str, 1023);
	free(str);
	return buf;
}

static char *type_sprintf(const char *fmt, ...)
{
	va_list ap;
	char *buf = xmalloc(1024);
	va_start(ap, fmt);

	vsnprintf(buf, 1023, fmt, ap);
	buf[1023] = '\0';

	va_end(ap);

	return buf;
}

static char *array_type_string(const char *str, int rank)
{
	if (rank <= 1)
		return strdup(str);
	return type_sprintf("%s@%d", str, rank);
}

static char *container_type_string(struct ain *ain, struct ain_type *t)
{
	char *buf = xmalloc(1024);
	char *type = ain_strtype_d(ain, t->array_type);
	const char *container_type
		= t->data == AIN_ARRAY     ? "array"
		: t->data == AIN_REF_ARRAY ? "ref array"
		: t->data == AIN_ITERATOR  ? "iter"
		: "unknown_container";

	snprintf(buf, 1023, "%s<%s>", container_type, type);

	free(type);
	buf[1023] = '\0';
	return buf;
}

char *ain_strtype_d(struct ain *ain, struct ain_type *v)
{
	char buf[1024];
	if (!v)
		return strdup("?");
	switch (v->data) {
	case AIN_VOID:                return strdup("void");
	case AIN_INT:                 return strdup("int");
	case AIN_FLOAT:               return strdup("float");
	case AIN_STRING:              return strdup("string");
	case AIN_STRUCT:
		if (v->struc == -1 || !ain)
			return strdup("struct");
		return strdup(ain->structures[v->struc].name);
	case AIN_ARRAY_INT:           return array_type_string("array@int", v->rank);
	case AIN_ARRAY_FLOAT:         return array_type_string("array@float", v->rank);
	case AIN_ARRAY_STRING:        return array_type_string("array@string", v->rank);
	case AIN_ARRAY_STRUCT:
		if (v->struc == -1 || !ain)
			return array_type_string("array@struct", v->rank);
		snprintf(buf, 1024, "array@%s", ain->structures[v->struc].name);
		return array_type_string(buf, v->rank);
	case AIN_REF_INT:             return strdup("ref int");
	case AIN_REF_FLOAT:           return strdup("ref float");
	case AIN_REF_STRING:          return strdup("ref string");
	case AIN_REF_STRUCT:
		if (v->struc == -1 || !ain)
			return strdup("ref struct");
		return type_sprintf("ref %s", ain->structures[v->struc].name);
	case AIN_REF_ARRAY_INT:       return array_type_string("ref array@int", v->rank);
	case AIN_REF_ARRAY_FLOAT:     return array_type_string("ref array@float", v->rank);
	case AIN_REF_ARRAY_STRING:    return array_type_string("ref array@string", v->rank);
	case AIN_REF_ARRAY_STRUCT:
		if (v->struc == -1 || !ain)
			return strdup("ref array@struct");
		snprintf(buf, 1024, "ref array@%s", ain->structures[v->struc].name);
		return array_type_string(buf, v->rank);
	case AIN_IMAIN_SYSTEM:        return strdup("imain_system");
	case AIN_FUNC_TYPE:           return strdup("functype");
	case AIN_ARRAY_FUNC_TYPE:     return array_type_string("array@functype", v->rank);
	case AIN_REF_FUNC_TYPE:       return strdup("ref functype");
	case AIN_REF_ARRAY_FUNC_TYPE: return array_type_string("ref array@functype", v->rank);
	case AIN_BOOL:                return strdup("bool");
	case AIN_ARRAY_BOOL:          return array_type_string("array@bool", v->rank);
	case AIN_REF_BOOL:            return strdup("ref bool");
	case AIN_REF_ARRAY_BOOL:      return array_type_string("ref array@bool", v->rank);
	case AIN_LONG_INT:            return strdup("lint");
	case AIN_ARRAY_LONG_INT:      return array_type_string("array@lint", v->rank);
	case AIN_REF_LONG_INT:        return strdup("ref lint");
	case AIN_REF_ARRAY_LONG_INT:  return array_type_string("ref array@lint", v->rank);
	case AIN_DELEGATE:            return strdup("delegate");
	case AIN_ARRAY_DELEGATE:      return array_type_string("array@delegate", v->rank);
	case AIN_REF_ARRAY_DELEGATE:  return array_type_string("ref array@delegate", v->rank);
	case AIN_UNKNOWN_TYPE_67:     return strdup("type_67");
	case AIN_UNKNOWN_TYPE_74:     return strdup("type_74");
	case AIN_UNKNOWN_TYPE_75:     return strdup("type_75");
	case AIN_ARRAY:
	case AIN_REF_ARRAY:
	case AIN_ITERATOR:
		return container_type_string(ain, v);
	case AIN_ENUM1:
		if (!v->array_type || v->array_type->struc == -1 || !ain)
			return strdup("enum");
		//return strdup(ain->enums[v->array_type->struc]);
		return type_sprintf("%s#86", ain->enums[v->array_type->struc].name);
	case AIN_IFACE:
		if (v->struc == -1 || !ain)
			return strdup("interface");
		return strdup(ain->structures[v->struc].name);
	case AIN_ENUM2:
	case AIN_ENUM3:
		if (v->struc == -1 || !ain)
			return strdup("enum");
		//return strdup(ain->enums[v->struc]);
		return type_sprintf("%s#%d", ain->enums[v->struc].name, v->data);
	case AIN_REF_ENUM:
		if (v->struc == -1 || !ain)
			return strdup("enum");
		return type_sprintf("ref %s", ain->enums[v->struc].name);
	case AIN_UNKNOWN_TYPE_95:     return strdup("type_95");
	default:
		WARNING("Unknown type: %d", v->data);
		return type_sprintf("unknown_type_%d", v->data);
	}
}

const char *ain_variable_to_string(struct ain *ain, struct ain_variable *v)
{
	static char buf[2048] = { [2047] = '\0' };
	char *type = ain_strtype_d(ain, &v->type);
	int i = snprintf(buf, 2047, "%s %s", type, v->name);
	free(type);

	if (v->has_initval) {
		switch (v->type.data) {
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
	struct ain_section *section;
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

static struct ain_type *read_array_type(struct ain_reader *r)
{
	int32_t data_type   = read_int32(r);
	int32_t struct_type = read_int32(r);
	int32_t array_rank  = read_int32(r);

	if (array_rank < 0)
		ERROR("Invalid array rank: %d", array_rank);

	struct ain_type *type = xcalloc(array_rank+1, sizeof(struct ain_type));
	type[0].data  = data_type;
	type[0].struc = struct_type;
	type[0].rank  = array_rank;

	for (int i = 0; i < array_rank; i++) {
		type[i+1].data  = read_int32(r);
		type[i+1].struc = read_int32(r);
		type[i+1].rank  = read_int32(r);
		type[i].array_type = type + i + 1;
	}

	return type;
}

static void read_variable_initval(struct ain_reader *r, struct ain_variable *v)
{
	if ((v->has_initval = read_int32(r))) {
		if (v->has_initval != 1)
			ERROR("variable->has_initval is not boolean? %d (at %p)", v->has_initval, r->index - 4);
		switch (v->type.data) {
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

static void read_variable_type(struct ain_reader *r, struct ain_type *t)
{
	t->data  = read_int32(r);
	t->struc = read_int32(r);
	t->rank  = read_int32(r);

	if (t->data == AIN_ARRAY || t->data == AIN_REF_ARRAY || t->data == AIN_ITERATOR || t->data == AIN_ENUM1)
		t->array_type = read_array_type(r);
}

static struct ain_variable *read_variables(struct ain_reader *r, int count, struct ain *ain, enum ain_variable_type var_type)
{
	struct ain_variable *variables = calloc(count, sizeof(struct ain_variable));
	for (int i = 0; i < count; i++) {
		struct ain_variable *v = &variables[i];
		v->var_type = var_type;
		v->name = read_string(r);
		if (ain->version >= 12)
			v->name2 = read_string(r); // ???
		read_variable_type(r, &v->type);
		if (ain->version >= 8)
			read_variable_initval(r, v);
	}
	return variables;
}

static void read_return_type(struct ain_reader *r, struct ain_type *t, struct ain *ain)
{
	if (ain->version >= 11) {
		read_variable_type(r, t);
		return;
	}

	t->data  = read_int32(r);
	t->struc = read_int32(r);
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

		read_return_type(r, &funs[i].return_type, ain);

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

static struct ain_variable *read_globals(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_variable *globals = calloc(count, sizeof(struct ain_variable));
	for (int i = 0; i < count; i++) {
		globals[i].name = read_string(r);
		if (ain->version >= 12)
			globals[i].name2 = read_string(r);
		read_variable_type(r, &globals[i].type);
		if (ain->version >= 5)
			globals[i].group_index = read_int32(r);
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

static struct ain_switch_case *read_switch_cases(struct ain_reader *r, int count, struct ain_switch *parent)
{
	struct ain_switch_case *cases = calloc(count, sizeof(struct ain_switch));
	for (int i = 0; i < count; i++) {
		cases[i].value = read_int32(r);
		cases[i].address = read_int32(r);
		cases[i].parent = parent;
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
		switches[i].cases = read_switch_cases(r, switches[i].nr_cases, &switches[i]);
	}
	return switches;
}

static struct ain_function_type *read_function_types(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_function_type *types = calloc(count, sizeof(struct ain_function_type));
	for (int i = 0; i < count; i++) {
		types[i].name = read_string(r);
		read_return_type(r, &types[i].return_type, ain);
		types[i].nr_arguments = read_int32(r);
		types[i].nr_variables = read_int32(r);
		types[i].variables = read_variables(r, types[i].nr_variables, ain, AIN_VAR_LOCAL);
	}
	return types;
}

#define for_each_instruction(ain, addr, instr, start, user_code)	\
	do {								\
		const struct instruction *instr;			\
		for (size_t addr = start; addr < ain->code_size; addr += instruction_width(instr->opcode)) { \
			uint16_t _fei_opcode = LittleEndian_getW(ain->code, addr); \
			if (_fei_opcode >= NR_OPCODES)			\
				ERROR("Unknown/invalid opcode: %u", _fei_opcode); \
			instr = &instructions[_fei_opcode];		\
			if (addr + instr->nr_args * 4 >= ain->code_size) \
				ERROR("CODE section truncated?");	\
			user_code;					\
		}							\
	} while (0)

struct ain_enum *read_enums(struct ain_reader *r, int count, struct ain *ain)
{
	char **names = read_strings(r, count);
	struct ain_enum *enums = xcalloc(count, sizeof(struct ain_enum));

	for (int i = 0; i < count; i++) {
		char buf[1024] = { [1023] = '\0' };
		snprintf(buf, 1023, "%s@String", names[i]);

		struct func_list *funs = get_function(ain, buf);
		if (!funs || funs->nr_slots != 1) {
			WARNING("Failed to parse enum: %s", names[i]);
			continue;
		}

		int j = 0;
		for_each_instruction(ain, addr, instr, funs->slots[0]->address, {
			if (instr->opcode == ENDFUNC)
				break;
			if (instr->opcode != S_PUSH)
				continue;

			int32_t strno = LittleEndian_getDW(ain->code, addr + 2);
			if (strno < 0 || strno >= ain->nr_strings) {
				WARNING("Encountered invalid string number when parsing enums");
				continue;
			}
			if (!ain->strings[strno]->size)
				continue;

			enums[i].symbols = xrealloc(enums[i].symbols, sizeof(char*) * (j+1));
			enums[i].symbols[j] = ain->strings[strno]->text;
			enums[i].nr_symbols = j + 1;
			j++;
		});
		enums[i].name = names[i];
	}

	free(names);
	return enums;
}

static void start_section(struct ain_reader *r, struct ain_section *section)
{
	if (r->section)
		r->section->size = r->index - r->section->addr;
	r->section = section;
	if (section) {
		r->section->addr = r->index;
		r->section->present = true;
		r->index += 4;
	}
}

static bool read_tag(struct ain_reader *r, struct ain *ain)
{
	if (r->index + 4 >= r->size) {
		start_section(r, NULL);
		return false;
	}

	uint8_t *tag_loc = r->buf + r->index;

#define TAG_EQ(tag) !strncmp((char*)tag_loc, tag, 4)
	// FIXME: need to check len or could segfault on currupt AIN file
	if (TAG_EQ("VERS")) {
		start_section(r, &ain->VERS);
		ain->version = read_int32(r);
		if (ain->version >= 11) {
			instructions[CALLHLL].nr_args = 3;
			instructions[NEW].nr_args = 2;
			instructions[S_MOD].nr_args = 1;
			instructions[OBJSWAP].nr_args = 1;
			instructions[DG_STR_TO_METHOD].nr_args = 1;
			instructions[CALLMETHOD].args[0] = T_INT;
		}
	} else if (TAG_EQ("KEYC")) {
		start_section(r, &ain->KEYC);
		ain->keycode = read_int32(r);
	} else if (TAG_EQ("CODE")) {
		start_section(r, &ain->CODE);
		ain->code_size = read_int32(r);
		ain->code = read_bytes(r, ain->code_size);
	} else if (TAG_EQ("FUNC")) {
		start_section(r, &ain->FUNC);
		ain->nr_functions = read_int32(r);
		ain->functions = read_functions(r, ain->nr_functions, ain);
		init_func_ht(ain);
	} else if (TAG_EQ("GLOB")) {
		start_section(r, &ain->GLOB);
		ain->nr_globals = read_int32(r);
		ain->globals = read_globals(r, ain->nr_globals, ain);
	} else if (TAG_EQ("GSET")) {
		start_section(r, &ain->GSET);
		ain->nr_initvals = read_int32(r);
		ain->global_initvals = read_initvals(r, ain->nr_initvals);
	} else if (TAG_EQ("STRT")) {
		start_section(r, &ain->STRT);
		ain->nr_structures = read_int32(r);
		ain->structures = read_structures(r, ain->nr_structures, ain);
		init_struct_ht(ain);
	} else if (TAG_EQ("MSG0")) {
		start_section(r, &ain->MSG0);
		ain->nr_messages = read_int32(r);
		ain->messages = read_vm_strings(r, ain->nr_messages);
	} else if (TAG_EQ("MSG1")) {
		start_section(r, &ain->MSG1);
		ain->nr_messages = read_int32(r);
		ain->msg1_uk = read_int32(r); // ???
		ain->messages = read_msg1_strings(r, ain->nr_messages);
	} else if (TAG_EQ("MAIN")) {
		start_section(r, &ain->MAIN);
		ain->main = read_int32(r);
	} else if (TAG_EQ("MSGF")) {
		start_section(r, &ain->MSGF);
		ain->msgf = read_int32(r);
	} else if (TAG_EQ("HLL0")) {
		start_section(r, &ain->HLL0);
		ain->nr_libraries = read_int32(r);
		ain->libraries = read_libraries(r, ain->nr_libraries);
	} else if (TAG_EQ("SWI0")) {
		start_section(r, &ain->SWI0);
		ain->nr_switches = read_int32(r);
		ain->switches = read_switches(r, ain->nr_switches);
	} else if (TAG_EQ("GVER")) {
		start_section(r, &ain->GVER);
		ain->game_version = read_int32(r);
	} else if (TAG_EQ("STR0")) {
		start_section(r, &ain->STR0);
		ain->nr_strings = read_int32(r);
		ain->strings = read_vm_strings(r, ain->nr_strings);
	} else if (TAG_EQ("FNAM")) {
		start_section(r, &ain->FNAM);
		ain->nr_filenames = read_int32(r);
		ain->filenames = read_strings(r, ain->nr_filenames);
	} else if (TAG_EQ("OJMP")) {
		start_section(r, &ain->OJMP);
		ain->ojmp = read_int32(r);
	} else if (TAG_EQ("FNCT")) {
		start_section(r, &ain->FNCT);
		ain->fnct_size = read_int32(r);
		ain->nr_function_types = read_int32(r);
		ain->function_types = read_function_types(r, ain->nr_function_types, ain);
	} else if (TAG_EQ("DELG")) {
		start_section(r, &ain->DELG);
		ain->delg_size = read_int32(r);
		ain->nr_delegates = read_int32(r);
		ain->delegates = read_function_types(r, ain->nr_delegates, ain);
	} else if (TAG_EQ("OBJG")) {
		start_section(r, &ain->OBJG);
		ain->nr_global_groups = read_int32(r);
		ain->global_group_names = read_strings(r, ain->nr_global_groups);
	} else if (TAG_EQ("ENUM")) {
		start_section(r, &ain->ENUM);
		ain->ENUM.present = true;
		ain->nr_enums = read_int32(r);
		ain->enums = read_enums(r, ain->nr_enums, ain);
	} else {
		start_section(r, NULL);
		WARNING("Junk at end of AIN file?");
		return false;
	}
#undef TAG_EQ

	return true;
}

static void distribute_initvals(struct ain *ain)
{
	for (int i = 0; i < ain->nr_initvals; i++) {
		struct ain_variable *g = &ain->globals[ain->global_initvals[i].global_index];
		g->has_initval = true;
		if (ain->global_initvals[i].data_type == AIN_STRING)
			g->initval.s = ain->global_initvals[i].string_value;
		else
			g->initval.i = ain->global_initvals[i].int_value;
	}
}

static uint8_t *decompress_ain(uint8_t *in, long *len)
{
	uint8_t *out;
	long out_len = LittleEndian_getDW(in, 8);
	long in_len  = LittleEndian_getDW(in, 12);

	if (out_len < 0 || in_len < 0)
		return NULL;

	out = xmalloc(out_len);
	int r = uncompress(out, (unsigned long*)&out_len, in+16, in_len);
	if (r != Z_OK) {
		if (r == Z_BUF_ERROR)
			WARNING("uncompress failed: Z_BUF_ERROR");
		else if (r == Z_MEM_ERROR)
			WARNING("uncompress failed: Z_MEM_ERROR");
		else if (r == Z_DATA_ERROR)
			WARNING("uncompress failed: Z_DATA_ERROR");
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

void ain_decrypt(uint8_t *buf, size_t len)
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
	ain_decrypt(magic, 8);

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
		ain_decrypt(buf, *len);
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
	if (!buf)
		goto err;

	// read data into ain struct
	struct ain_reader r = {
		.buf = buf,
		.index = 0,
		.size = len
	};
	ain = calloc(1, sizeof(struct ain));
	ain->_func_ht = kh_init(func_ht);
	ain->_struct_ht = kh_init(struct_ht);
	while (read_tag(&r, ain));
	if (!ain->version) {
		*error = AIN_INVALID;
		goto err;
	}
	distribute_initvals(ain);
	init_member_functions(ain);

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
		free(vars[i].type.array_type);
		if (vars[i].has_initval && vars[i].type.data == AIN_STRING)
			free(vars[i].initval.s);
	}
	free(vars);
}

static void _ain_free_function_types(struct ain_function_type *funs, int n)
{
	for (int i = 0; i < n; i++) {
		free(funs[i].name);
		free(funs[i].return_type.array_type);
		ain_free_variables(funs[i].variables, funs[i].nr_variables);
	}
	free(funs);
}

static void ain_free_vmstrings(struct string **strings, int n)
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

void ain_free_functions(struct ain *ain)
{
	for (int f = 0; f < ain->nr_functions; f++) {
		free(ain->functions[f].name);
		free(ain->functions[f].return_type.array_type);
		ain_free_variables(ain->functions[f].vars, ain->functions[f].nr_vars);
	}
	free(ain->functions);
	ain->functions = NULL;
	ain->nr_functions = 0;
}

void ain_free_globals(struct ain *ain)
{
	ain_free_variables(ain->globals, ain->nr_globals);
	ain->globals = NULL;
	ain->nr_globals = 0;
}

void ain_free_initvals(struct ain *ain)
{
	free(ain->global_initvals);
	ain->global_initvals = NULL;
	ain->nr_initvals = 0;
}

void ain_free_structures(struct ain *ain)
{
	for (int s = 0; s < ain->nr_structures; s++) {
		free(ain->structures[s].name);
		free(ain->structures[s].interfaces);
		ain_free_variables(ain->structures[s].members, ain->structures[s].nr_members);
	}
	free(ain->structures);
	ain->structures = NULL;
	ain->nr_structures = 0;
}

void ain_free_messages(struct ain *ain)
{
	ain_free_vmstrings(ain->messages, ain->nr_messages);
	ain->messages = NULL;
	ain->nr_messages = 0;
}

void ain_free_libraries(struct ain *ain)
{
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
	ain->libraries = NULL;
	ain->nr_libraries = 0;
}

void ain_free_switches(struct ain *ain)
{
	for (int i = 0; i < ain->nr_switches; i++) {
		free(ain->switches[i].cases);
	}
	free(ain->switches);
	ain->switches = NULL;
	ain->nr_switches = 0;
}

void ain_free_strings(struct ain *ain)
{
	ain_free_vmstrings(ain->strings, ain->nr_strings);
	ain->strings = NULL;
	ain->nr_strings = 0;
}

void ain_free_filenames(struct ain *ain)
{
	ain_free_cstrings(ain->filenames, ain->nr_filenames);
	ain->filenames = NULL;
	ain->nr_filenames = 0;
}

void ain_free_function_types(struct ain *ain)
{
	_ain_free_function_types(ain->function_types, ain->nr_function_types);
	ain->function_types = NULL;
	ain->nr_function_types = 0;
}

void ain_free_delegates(struct ain *ain)
{
	_ain_free_function_types(ain->delegates, ain->nr_delegates);
	ain->delegates = NULL;
	ain->nr_delegates = 0;
}

void ain_free_global_groups(struct ain *ain)
{
	ain_free_cstrings(ain->global_group_names, ain->nr_global_groups);
	ain->global_group_names = NULL;
	ain->nr_global_groups = 0;
}

void ain_free_enums(struct ain *ain)
{
	for (int i = 0; i < ain->nr_enums; i++) {
		free(ain->enums[i].name);
		free(ain->enums[i].symbols);
	}
	free(ain->enums);
	ain->enums = NULL;
	ain->nr_enums = 0;
}

void ain_free(struct ain *ain)
{
	free(ain->code);

	ain_free_functions(ain);
	ain_free_globals(ain);
	ain_free_initvals(ain);
	ain_free_structures(ain);
	ain_free_messages(ain);
	ain_free_libraries(ain);
	ain_free_switches(ain);
	ain_free_strings(ain);
	ain_free_filenames(ain);
	ain_free_function_types(ain);
	ain_free_delegates(ain);
	ain_free_global_groups(ain);
	ain_free_enums(ain);

	struct func_list *list;
	kh_foreach_value(ain->_func_ht, list, free(list));
	kh_destroy(func_ht, ain->_func_ht);
	kh_destroy(struct_ht, ain->_struct_ht);

	free(ain);
}
