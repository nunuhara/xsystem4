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

#include "system4.h"
#include "ain.h"
#include "little_endian.h"
#include "instructions.h"

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

static struct ain_variable *read_variables(struct ain_reader *r, int count)
{
	struct ain_variable *variables = calloc(count, sizeof(struct ain_variable));
	for (int i = 0; i < count; i++) {
		variables[i].name = read_string(r);
		variables[i].data_type = read_int32(r);
		variables[i].struct_type = read_int32(r);
		variables[i].array_dimensions = read_int32(r);
	}
	return variables;
}

static struct ain_function *read_functions(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_function *funs = calloc(count, sizeof(struct ain_function));
	for (int i = 0; i < count; i++) {
		funs[i].address = read_int32(r);
		funs[i].name = read_string(r);
		if (ain->version > 0 && ain->version < 7) {
			funs[i].is_label = read_int32(r);
		}
		funs[i].data_type = read_int32(r);
		funs[i].struct_type = read_int32(r);
		funs[i].nr_args = read_int32(r);
		funs[i].nr_vars = read_int32(r);
		if (ain->version > 0) {
			funs[i].crc = read_int32(r);
		}
		if (funs[i].nr_vars > 0) {
			funs[i].vars = read_variables(r, funs[i].nr_vars);
		}
	}
	return funs;
}

static struct ain_global *read_globals(struct ain_reader *r, int count, struct ain *ain)
{
	struct ain_global *globals = calloc(count, sizeof(struct ain_global));
	for (int i = 0; i < count; i++) {
		globals[i].name = read_string(r);
		globals[i].data_type = read_int32(r);
		globals[i].struct_type = read_int32(r);
		globals[i].array_dimensions = read_int32(r);
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

static struct ain_struct *read_structures(struct ain_reader *r, int count)
{
	struct ain_struct *structures = calloc(count, sizeof(struct ain_struct));
	for (int i = 0; i < count; i++) {
		structures[i].name = read_string(r);
		structures[i].constructor = read_int32(r);
		structures[i].destructor = read_int32(r);
		structures[i].nr_members = read_int32(r);
		structures[i].members = read_variables(r, structures[i].nr_members);
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

static struct ain_function_type *read_function_types(struct ain_reader *r, int count)
{
	struct ain_function_type *types = calloc(count, sizeof(struct ain_function_type));
	for (int i = 0; i < count; i++) {
		types[i].name = read_string(r);
		types[i].data_type = read_int32(r);
		types[i].struct_type = read_int32(r);
		types[i].nr_arguments = read_int32(r);
		types[i].nr_variables = read_int32(r);
		types[i].variables = read_variables(r, types[i].nr_variables);
	}
	return types;
}

static bool read_tag(struct ain_reader *r, struct ain *ain)
{
	uint8_t *tag_loc = r->buf + r->index;
	r->index += 4;

#define TAG_EQ(tag) !strncmp((char*)tag_loc, tag, 4)
	// FIXME: need to check len or could segfault on currupt AIN file
	if (TAG_EQ("VERS")) {
		ain->version = read_int32(r);
	} else if (TAG_EQ("KEYC")) {
		ain->keycode = read_int32(r);
	} else if (TAG_EQ("CODE")) {
		ain->code_size = read_int32(r);
		ain->code = read_bytes(r, ain->code_size);
	} else if (TAG_EQ("FUNC")) {
		int32_t count = read_int32(r);
		ain->functions = read_functions(r, count, ain);
	} else if (TAG_EQ("GLOB")) {
		int32_t count = read_int32(r);
		ain->globals = read_globals(r, count, ain);
	} else if (TAG_EQ("GSET")) {
		int32_t count = read_int32(r);
		ain->global_initvals = read_initvals(r, count);
	} else if (TAG_EQ("STRT")) {
		int32_t count = read_int32(r);
		ain->structures = read_structures(r, count);
	} else if (TAG_EQ("MSG0")) {
		ain->messages = read_strings(r, read_int32(r));
	} else if (TAG_EQ("MAIN")) {
		ain->main = read_int32(r);
	} else if (TAG_EQ("MSGF")) {
		ain->msgf = read_int32(r);
	} else if (TAG_EQ("HLL0")) {
		int32_t count = read_int32(r);
		ain->libraries = read_libraries(r, count);
	} else if (TAG_EQ("SWI0")) {
		int32_t count = read_int32(r);
		ain->switches = read_switches(r, count);
	} else if (TAG_EQ("GVER")) {
		ain->game_version = read_int32(r);
	} else if (TAG_EQ("STR0")) {
		ain->strings = read_strings(r, read_int32(r));
	} else if (TAG_EQ("FNAM")) {
		ain->filenames = read_strings(r, read_int32(r));
	} else if (TAG_EQ("OJMP")) {
		ain->ojmp = read_int32(r);
	} else if (TAG_EQ("FNCT")) {
		//int32_t length = read_int32(r) - 4; // ???
		read_int32(r);
		int32_t count = read_int32(r);
		ain->function_types = read_function_types(r, count);
	//} else if (TAG_EQ("DELG")) {
	} else if (TAG_EQ("OBJG")) {
		ain->global_group_names = read_strings(r, read_int32(r));
	//} else if (TAG_EQ("MSG1")) {
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

struct ain *ain_open(const char *path, int *error)
{
	FILE *fp;
	long len;
	uint8_t *buf = NULL;
	struct ain *ain = NULL;

	if (!(fp = fopen(path, "rb"))) {
		*error = AIN_FILE_ERROR;
		goto err;
	}

	// get size of AIN file
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// read AIN file into memory
	buf = xmalloc(len + 4); // why +4?
	fread(buf, 1, len, fp);
	fclose(fp);

	// check magic
	if (!strncmp((char*)buf, "AI2\0\0\0\0", 8)) {
		uint8_t *uc = decompress_ain(buf, &len);
		if (!uc) {
			*error = AIN_INVALID;
			goto err;
		}
		free(buf);
		buf = uc;
	} else if (ain_is_encrypted(buf)) {
		decrypt_ain(buf, len);
	} else {
		printf("%.4s\n", buf);
		*error = AIN_UNRECOGNIZED_FORMAT;
		goto err;
	}

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

void ain_free(struct ain *ain)
{
	free(ain);
}
