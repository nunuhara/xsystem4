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

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include "aindump.h"
#include "little_endian.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

void disassemble_ain(FILE *out, struct ain *ain, bool raw);

static void usage(void)
{
	puts("Usage: aindump <options> <ainfile>");
	puts("    Display information from AIN files.");
	puts("");
	puts("    -h, --help               Display this message and exit");
	puts("    -c, --code               Dump code section");
	puts("    -C, --raw-code           Dump code section (raw)");
	puts("    -f, --functions          Dump functions section");
	puts("    -g, --globals            Dump globals section");
	puts("    -S, --structures         Dump structures section");
	puts("    -m, --messages           Dump messages section");
	puts("    -l, --libraries          Dump libraries section");
	puts("        --function-types     Dump function types section");
	puts("        --delegates          Dump delegate types section");
	puts("        --global-group-names Dump global group names section");
	puts("    -e, --enums              Dump enums section");
	puts("    -A, --audit              Audit AIN file for xsystem4 compatibility");
	puts("    -d, --decrypt            Dump decrypted AIN file");
	puts("    -j, --json               Dump to JSON format");
	puts("        --map                Dump AIN file map");
}

static void print_sjis(FILE *f, const char *s)
{
	char *u = sjis2utf(s, strlen(s));
	fprintf(f, "%s", u);
	free(u);
}

static void print_type(FILE *f, struct ain *ain, struct ain_type *t)
{
	char *str = ain_strtype_d(ain, t);
	print_sjis(f, str);
	free(str);
}

static void ain_dump_version(FILE *f, struct ain *ain)
{
	fprintf(f, "AIN VERSION %d\n", ain->version);
}

static void print_arglist(FILE *f, struct ain *ain, struct ain_variable *args, int nr_args)
{
	if (!nr_args) {
		fprintf(f, "(void)");
		return;
	}
	fputc('(', f);
	for (int i = 0; i < nr_args; i++) {
		if (args[i].type.data == AIN_VOID)
			continue;
		if (i > 0)
			fprintf(f, ", ");
		print_sjis(f, ain_variable_to_string(ain, &args[i]));
	}
	fputc(')', f);
}

static void print_varlist(FILE *f, struct ain *ain, struct ain_variable *vars, int nr_vars)
{
	for (int i = 0; i < nr_vars; i++) {
		if (i > 0)
			fputc(',', f);
		fputc(' ', f);
		print_sjis(f, ain_variable_to_string(ain, &vars[i]));
	}
}

void ain_dump_function(FILE *out, struct ain *ain, struct ain_function *f)
{
	print_type(out, ain, &f->return_type);
	fputc(' ', out);
	print_sjis(out, f->name);
	print_arglist(out, ain, f->vars, f->nr_args);
	print_varlist(out, ain, f->vars+f->nr_args, f->nr_vars - f->nr_args);
}

static void ain_dump_functions(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		fprintf(f, "/* 0x%08x */\t", i);
		ain_dump_function(f, ain, &ain->functions[i]);
		fprintf(f, ";\n");
	}
}

static void ain_dump_globals(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_globals; i++) {
		fprintf(f, "/* 0x%08x */\t", i);
		struct ain_variable *g = &ain->globals[i];
		if (g->type.data == AIN_VOID)
			continue;
		print_sjis(f, ain_variable_to_string(ain, g));
		fprintf(f, ";\n");
	}
}

static void print_structure(FILE *f, struct ain *ain, struct ain_struct *s)
{
	fprintf(f, "struct ");
	print_sjis(f, s->name);

	if (s->nr_interfaces) {
		fprintf(f, " implements");
		for (int i = 0; i < s->nr_interfaces; i++) {
			if (i > 0)
				fputc(',', f);
			fputc(' ', f);
			print_sjis(f, ain->structures[s->interfaces[i].struct_type].name);
		}
	}

	fprintf(f, " {\n");
	for (int i = 0; i < s->nr_members; i++) {
		struct ain_variable *m = &s->members[i];
		if (m->type.data == AIN_VOID)
			continue;
		fprintf(f, "    ");
		print_sjis(f, ain_variable_to_string(ain, m));
		fprintf(f, ";\n");
	}
	fprintf(f, "};\n\n");
}

static void ain_dump_structures(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_structures; i++) {
		fprintf(f, "// %d\n", i);
		print_structure(f, ain, &ain->structures[i]);
	}
}

static void ain_dump_messages(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_messages; i++) {
		fprintf(f, "0x%08x:\t", i);
		print_sjis(f, ain->messages[i]->text);
	}
}

static void ain_dump_libraries(FILE *out, struct ain *ain)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		fprintf(out, "--- ");
		print_sjis(out, ain->libraries[i].name);
		fprintf(out, " ---\n");
		for (int j = 0; j < ain->libraries[i].nr_functions; j++) {
			struct ain_hll_function *f = &ain->libraries[i].functions[j];
			print_sjis(out, ain_strtype(ain, f->data_type, -1));
			fputc(' ', out);
			print_sjis(out, f->name);
			fputc('(', out);
			for (int k = 0; k < f->nr_arguments; k++) {
				struct ain_hll_argument *a = &f->arguments[k];
				if (a->data_type == AIN_VOID)
					continue;
				if (k > 0)
					fprintf(out, ", ");
				print_sjis(out, ain_strtype(ain, a->data_type, -1));
				fputc(' ', out);
				print_sjis(out, a->name);
			}
			fprintf(out, ")\n");
		}
	}
}

static void ain_dump_strings(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_strings; i++) {
		fprintf(f, "0x%08x:\t", i);
		print_sjis(f, ain->strings[i]->text);
		fputc('\n', f);
	}
}

static void ain_dump_filenames(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_filenames; i++) {
		fprintf(f, "0x%08x:\t", i);
		print_sjis(f, ain->filenames[i]);
		fputc('\n', f);
	}
}

static void ain_dump_functypes(FILE *f, struct ain *ain, bool delegates)
{
	struct ain_function_type *types = delegates ? ain->delegates : ain->function_types;
	int n = delegates ? ain->nr_delegates : ain->nr_function_types;
	for (int i = 0; i < n; i++) {
		fprintf(f, "/* 0x%08x */\t", i);
		struct ain_function_type *t = &types[i];
		fprintf(f, delegates ? "delegate " : "functype ");

		print_type(f, ain, &t->return_type);
		fputc(' ', f);
		print_sjis(f, t->name);
		print_arglist(f, ain, t->variables, t->nr_arguments);
		print_varlist(f, ain, t->variables+t->nr_arguments, t->nr_variables-t->nr_arguments);
		fputc('\n', f);
	}
}

static void ain_dump_global_group_names(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_global_groups; i++) {
		fprintf(f, "0x%08x:\t", i);
		print_sjis(f, ain->global_group_names[i]);
		fputc('\n', f);
	}
}

static void ain_dump_enums(FILE *f, struct ain *ain)
{
	for (int i = 0; i < ain->nr_enums; i++) {
		fprintf(f, "// %d\nenum ", i);
		print_sjis(f, ain->enums[i].name);
		fprintf(f, " {");
		for (int j = 0; j < ain->enums[i].nr_symbols; j++) {
			if (j)
				fputc(',', f);
			fprintf(f, "\n\t");
			print_sjis(f, ain->enums[i].symbols[j]);
		}
		fprintf(f, "\n};\n\n");
	}
}

static void ain_audit(FILE *f, struct ain *ain)
{
	for (size_t addr = 0; addr < ain->code_size;) {
		uint16_t opcode = LittleEndian_getW(ain->code, addr);
		const struct instruction *instr = &instructions[opcode];
		if (opcode >= NR_OPCODES) {
			ERROR("0x%08lx: Invalid/unknown opcode: %x", opcode);
		}
		if (!instr->implemented) {
			fprintf(f, "0x%08lx: %s (unimplemented instruction)\n", addr, instr->name);
		}
		if (opcode == CALLSYS) {
			uint32_t syscode = LittleEndian_getDW(ain->code, addr + 2);
			if (syscode >= NR_SYSCALLS) {
				ERROR("0x%08lx: CALLSYS system.(0x%x)\n", addr, syscode);
			}
			const char * const name = syscalls[syscode].name;
			if (!name) {
				fprintf(f, "0x%08lx: CALLSYS system.(0x%x)\n", addr, syscode);
			} else if (!syscalls[syscode].implemented) {
				fprintf(f, "0x%08lx: CALLSYS %s (unimplemented system call)\n", addr, name);
			}

		}
		// TODO: audit library calls
		addr += instruction_width(opcode);
	}
	fflush(f);
}

static void print_section(FILE *f, const char *name, struct ain_section *section)
{
	if (section->present)
		fprintf(f, "%s: %08x -> %08x\n", name, section->addr, section->addr + section->size);
}

static void ain_dump_map(FILE *f, struct ain *ain)
{
	print_section(f, "VERS", &ain->VERS);
	print_section(f, "KEYC", &ain->KEYC);
	print_section(f, "CODE", &ain->CODE);
	print_section(f, "FUNC", &ain->FUNC);
	print_section(f, "GLOB", &ain->GLOB);
	print_section(f, "GSET", &ain->GSET);
	print_section(f, "STRT", &ain->STRT);
	print_section(f, "MSG0", &ain->MSG0);
	print_section(f, "MSG1", &ain->MSG1);
	print_section(f, "MAIN", &ain->MAIN);
	print_section(f, "MSGF", &ain->MSGF);
	print_section(f, "HLL0", &ain->HLL0);
	print_section(f, "SWI0", &ain->SWI0);
	print_section(f, "GVER", &ain->GVER);
	print_section(f, "STR0", &ain->STR0);
	print_section(f, "FNAM", &ain->FNAM);
	print_section(f, "OJMP", &ain->OJMP);
	print_section(f, "FNCT", &ain->FNCT);
	print_section(f, "DELG", &ain->DELG);
	print_section(f, "OBJG", &ain->OBJG);
	print_section(f, "ENUM", &ain->ENUM);

	fprintf(f, "FNCT_SIZE = %d\n", ain->delg_size);
	fprintf(f, "FNCT.SIZE = %d\n", ain->DELG.size);
}

static void dump_decrypted(FILE *f, const char *path)
{
	int err;
	long len;
	uint8_t *ain;

	if (!(ain = ain_read(path, &len, &err))) {
		ERROR("Failed to open ain file: %s\n", ain_strerror(err));
	}

	fwrite(ain, len, 1, f);
	fflush(f);
	free(ain);
}

int main(int argc, char *argv[])
{
	bool dump_version = false;
	bool dump_code = false;
	bool dump_raw_code = false;
	bool dump_functions = false;
	bool dump_globals = false;
	bool dump_structures = false;
	bool dump_messages = false;
	bool dump_libraries = false;
	bool dump_strings = false;
	bool dump_filenames = false;
	bool dump_functypes = false;
	bool dump_delegates = false;
	bool dump_global_group_names = false;
	bool dump_enums = false;
	bool dump_json = false;
	bool dump_map = false;
	bool audit = false;
	bool decrypt = false;
	char *output_file = NULL;
	FILE *output = stdout;
	int err = AIN_SUCCESS;
	struct ain *ain;

	while (1) {
		static struct option long_options[] = {
			{ "help",               no_argument,       0, 'h' },
			{ "ain-version",        no_argument,       0, 'V' },
			{ "code",               no_argument,       0, 'c' },
			{ "raw-code",           no_argument,       0, 'C' },
			{ "functions",          no_argument,       0, 'f' },
			{ "globals",            no_argument,       0, 'g' },
			{ "structures",         no_argument,       0, 'S' },
			{ "messages",           no_argument,       0, 'm' },
			{ "libraries",          no_argument,       0, 'l' },
			{ "strings",            no_argument,       0, 's' },
			{ "filenames",          no_argument,       0, 'F' },
			{ "function-types",     no_argument,       0, 't' },
			{ "delegates",          no_argument,       0, 'D' },
			{ "global-group-names", no_argument,       0, 'r' },
			{ "enums",              no_argument,       0, 'e' },
			{ "audit",              no_argument,       0, 'A' },
			{ "decrypt",            no_argument,       0, 'd' },
			{ "json",               no_argument,       0, 'j' },
			{ "map",                no_argument,       0, 'M' },
			{ "output",             required_argument, 0, 'o' },
		};
		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "hVcCfgSmlsFeAdjo:", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'V':
			dump_version = true;
			break;
		case 'c':
			dump_code = true;
			break;
		case 'C':
			dump_raw_code = true;
			break;
		case 'f':
			dump_functions = true;
			break;
		case 'g':
			dump_globals = true;
			break;
		case 'S':
			dump_structures = true;
			break;
		case 'm':
			dump_messages = true;
			break;
		case 'l':
			dump_libraries = true;
			break;
		case 's':
			dump_strings = true;
			break;
		case 'F':
			dump_filenames = true;
			break;
		case 't':
			dump_functypes = true;
			break;
		case 'D':
			dump_delegates = true;
			break;
		case 'G':
			dump_global_group_names = true;
			break;
		case 'e':
			dump_enums = true;
			break;
		case 'A':
			audit = true;
			break;
		case 'd':
			decrypt = true;
			break;
		case 'j':
			dump_json = true;
			break;
		case 'M':
			dump_map = true;
			break;
		case 'o':
			output_file = xstrdup(optarg);
			break;
		case '?':
			ERROR("Unkown command line argument");
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage();
		ERROR("Wrong number of arguments.\n");
		return 1;
	}

	if (output_file) {
		if (!(output = fopen(output_file, "w"))) {
			ERROR("Failed to open output file '%s': %s", output_file, strerror(errno));
		}
		free(output_file);
	}

	if (decrypt) {
		dump_decrypted(output, argv[0]);
		return 0;
	}

	if (!(ain = ain_open(argv[0], &err))) {
		ERROR("Failed to open ain file: %s\n", ain_strerror(err));
		return 1;
	}

	if (dump_version)
		ain_dump_version(output, ain);
	if (dump_structures)
		ain_dump_structures(output, ain);
	if (dump_functypes)
		ain_dump_functypes(output, ain, false);
	if (dump_delegates)
		ain_dump_functypes(output, ain, true);
	if (dump_global_group_names)
		ain_dump_global_group_names(output, ain);
	if (dump_globals)
		ain_dump_globals(output, ain);
	if (dump_libraries)
		ain_dump_libraries(output, ain);
	if (dump_functions)
		ain_dump_functions(output, ain);
	if (dump_messages)
		ain_dump_messages(output, ain);
	if (dump_strings)
		ain_dump_strings(output, ain);
	if (dump_filenames)
		ain_dump_filenames(output, ain);
	if (dump_enums)
		ain_dump_enums(output, ain);
	if (dump_code)
		disassemble_ain(output, ain, false);
	if (dump_raw_code)
		disassemble_ain(output, ain, true);
	if (dump_json)
		ain_dump_json(output, ain);
	if (dump_map)
		ain_dump_map(output, ain);
	if (audit)
		ain_audit(output, ain);

	ain_free(ain);
	return 0;
}
