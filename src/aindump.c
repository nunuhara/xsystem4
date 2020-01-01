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
#include <getopt.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static void usage(void)
{
	puts("Usage: aindump <options> <ainfile>");
	puts("    Display information from AIN files.");
	puts("");
	puts("    -h, --help               Display this message and exit");
	puts("    -c, --code               Dump code section");
	puts("    -f, --functions          Dump functions section");
	puts("    -g, --globals            Dump globals section");
	puts("    -o, --structures         Dump structures section");
	puts("    -m, --messages           Dump messages section");
	puts("    -l, --libraries          Dump libraries section");
	puts("        --function-types     Dump function type section");
	puts("        --global-group-names Dump global group names section");
	puts("    -e, --enums              Dump enums section");
	//puts("    -j, --json               Dump to JSON format"); // TODO
}

static void print_sjis(const char *s)
{
	char *u = sjis2utf(s, strlen(s));
	printf("%s", u);
	free(u);
}

static void ain_dump_version(struct ain *ain)
{
	printf("AIN VERSION %d\n", ain->version);
}

static void ain_dump_code(struct ain *ain)
{
	for (size_t i = 0; i < ain->code_size;) {
		uint16_t opcode = LittleEndian_getW(ain->code, i);
		const struct instruction *instr = &instructions[opcode];
		printf("%s", instr->name);
		for (int j = 0; j < instr->nr_args; j++) {
			printf(" 0x%x", LittleEndian_getDW(ain->code, i + 2 + j*4));
		}
		putchar('\n');
		i += instruction_width(opcode);
	}
}

static void print_arglist(struct ain *ain, struct ain_variable *args, int nr_args)
{
	if (!nr_args) {
		printf("(void)");
		return;
	}
	putchar('(');
	for (int i = 0; i < nr_args; i++) {
		if (args[i].data_type == AIN_VOID)
			continue;
		if (i > 0)
			printf(", ");
		print_sjis(ain_strtype(ain, args[i].data_type, args[i].struct_type));
		putchar(' ');
		print_sjis(args[i].name);
	}
	putchar(')');
}

static void print_function(struct ain *ain, struct ain_function *f)
{
	print_sjis(ain_strtype(ain, f->data_type, f->struct_type));
	putchar(' ');
	print_sjis(f->name);
	print_arglist(ain, f->vars, f->nr_args);
	putchar('\n');
}

static void ain_dump_functions(struct ain *ain)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		print_function(ain, &ain->functions[i]);
	}
}

static void ain_dump_globals(struct ain *ain)
{
	for (int i = 0; i < ain->nr_globals; i++) {
		struct ain_global *g = &ain->globals[i];
		if (g->data_type == AIN_VOID)
			continue;
		print_sjis(ain_strtype(ain, g->data_type, g->struct_type));
		putchar(' ');
		print_sjis(g->name);
		putchar('\n');
	}
}

static void print_structure(struct ain *ain, struct ain_struct *s)
{
	printf("struct ");
	print_sjis(s->name);
	printf(" {\n");
	for (int i = 0; i < s->nr_members; i++) {
		struct ain_variable *m = &s->members[i];
		if (m->data_type == AIN_VOID)
			continue;
		printf("    ");
		print_sjis(ain_strtype(ain, m->data_type, m->struct_type));
		putchar(' ');
		print_sjis(m->name);
		printf(";\n");
	}
	printf("};\n\n");
}

static void ain_dump_structures(struct ain *ain)
{
	for (int i = 0; i < ain->nr_structures; i++) {
		print_structure(ain, &ain->structures[i]);
	}
}

static void ain_dump_messages(struct ain *ain)
{
	for (int i = 0; i < ain->nr_messages; i++) {
		print_sjis(ain->messages[i]->text);
		//printf("\n---\n");
	}
}

static void ain_dump_libraries(struct ain *ain)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		printf("--- ");
		print_sjis(ain->libraries[i].name);
		printf(" ---\n");
		for (int j = 0; j < ain->libraries[i].nr_functions; j++) {
			struct ain_hll_function *f = &ain->libraries[i].functions[j];
			print_sjis(ain_strtype(ain, f->data_type, -1));
			putchar(' ');
			print_sjis(f->name);
			putchar('(');
			for (int k = 0; k < f->nr_arguments; k++) {
				struct ain_hll_argument *a = &f->arguments[k];
				if (a->data_type == AIN_VOID)
					continue;
				if (k > 0)
					printf(", ");
				print_sjis(ain_strtype(ain, a->data_type, -1));
				putchar(' ');
				print_sjis(a->name);
			}
			printf(")\n");
		}
	}
}

static void ain_dump_strings(struct ain *ain)
{
	for (int i = 0; i < ain->nr_strings; i++) {
		print_sjis(ain->strings[i]->text);
		//printf("\n---\n");
	}
}

static void ain_dump_filenames(struct ain *ain)
{
	for (int i = 0; i < ain->nr_filenames; i++) {
		print_sjis(ain->filenames[i]);
		putchar('\n');
	}
}

static void ain_dump_functypes(struct ain *ain)
{
	for (int i = 0; i < ain->nr_function_types; i++) {
		struct ain_function_type *t = &ain->function_types[i];
		printf("functype ");
		print_sjis(ain_strtype(ain, t->data_type, t->struct_type));
		putchar(' ');
		print_sjis(t->name);
		print_arglist(ain, t->variables, t->nr_arguments);
		putchar('\n');
	}
}

static void ain_dump_global_group_names(struct ain *ain)
{
	for (int i = 0; i < ain->nr_global_groups; i++) {
		print_sjis(ain->global_group_names[i]);
		putchar('\n');
	}
}

static void ain_dump_enums(struct ain *ain)
{
	for (int i = 0; i < ain->nr_enums; i++) {
		print_sjis(ain->enums[i]);
		putchar('\n');
	}
}

int main(int argc, char *argv[])
{
	bool dump_version = false;
	bool dump_code = false;
	bool dump_functions = false;
	bool dump_globals = false;
	bool dump_structures = false;
	bool dump_messages = false;
	bool dump_libraries = false;
	bool dump_strings = false;
	bool dump_filenames = false;
	bool dump_functypes = false;
	bool dump_global_group_names = false;
	bool dump_enums = false;
	int err = AIN_SUCCESS;
	struct ain *ain;

	while (1) {
		static struct option long_options[] = {
			{ "help",               no_argument, 0, 'h' },
			{ "ain-version",        no_argument, 0, 'a' },
			{ "code",               no_argument, 0, 'c' },
			{ "functions",          no_argument, 0, 'f' },
			{ "globals",            no_argument, 0, 'g' },
			{ "structures",         no_argument, 0, 'o' },
			{ "messages",           no_argument, 0, 'm' },
			{ "libraries",          no_argument, 0, 'l' },
			{ "strings",            no_argument, 0, 's' },
			{ "filenames",          no_argument, 0, 'n' },
			{ "function-types",     no_argument, 0, 't' },
			{ "global-group-names", no_argument, 0, 'r' },
			{ "enums",              no_argument, 0, 'e' },
		};
		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "hacfgomlse", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'a':
			dump_version = true;
			break;
		case 'c':
			dump_code = true;
			break;
		case 'f':
			dump_functions = true;
			break;
		case 'g':
			dump_globals = true;
			break;
		case 'o':
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
		case 'n':
			dump_filenames = true;
			break;
		case 't':
			dump_functypes = true;
			break;
		case 'r':
			dump_global_group_names = true;
			break;
		case 'e':
			dump_enums = true;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		printf("ERROR: Wrong number of arguments.\n");
		usage();
		return 1;
	}

	if (!(ain = ain_open(argv[0], &err))) {
		printf("ERROR: %s\n", ain_strerror(err));
		return 1;
	}

	if (dump_version)
		ain_dump_version(ain);
	if (dump_structures)
		ain_dump_structures(ain);
	if (dump_functypes)
		ain_dump_functypes(ain);
	if (dump_global_group_names)
		ain_dump_global_group_names(ain);
	if (dump_globals)
		ain_dump_globals(ain);
	if (dump_libraries)
		ain_dump_libraries(ain);
	if (dump_functions)
		ain_dump_functions(ain);
	if (dump_messages)
		ain_dump_messages(ain);
	if (dump_strings)
		ain_dump_strings(ain);
	if (dump_filenames)
		ain_dump_filenames(ain);
	if (dump_enums)
		ain_dump_enums(ain);
	if (dump_code)
		ain_dump_code(ain);
}
