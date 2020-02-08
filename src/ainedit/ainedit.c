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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include "ainedit.h"
#include "system4.h"
#include "system4/ain.h"

static void usage(void)
{
	puts("Usage: ainedit [options...] input-file");
	puts("    Edit AIN files.");
	puts("");
	puts("    -h, --help                   Display this message and exit");
	puts("    -c, --code <jam-file>        Update the CODE section (assemble .jam file)");
	puts("    -j, --json <json-file>       Update AIN file from JSON data");
	puts("    -o, --output <path>          Set output file path");
	puts("        --raw                    Read code in raw mode");
	puts("        --no-strings             Read code in no-strings mode");
	//puts("    -p,--project <pje-file>      Build AIN from project file");
}

int main(int argc, char *argv[])
{
	struct ain *ain;
	int err = AIN_SUCCESS;
	const char *code_file = NULL;
	const char *decl_file = NULL;
	const char *output_file = NULL;
	uint32_t flags = 0;
	while (1) {
		static struct option long_options[] = {
			{ "help",       no_argument,       0, 'h' },
			{ "code",       required_argument, 0, 'c' },
			{ "json",       required_argument, 0, 'j' },
			{ "output",     required_argument, 0, 'o' },
			{ "raw",        no_argument,       0, 'R' },
			{ "no-strings", no_argument,       0, 'N' },
		};
		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "hc:j:o:", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'c':
			code_file = optarg;
			break;
		case 'j':
			decl_file = optarg;
			break;
		case 'o':
			output_file = optarg;
			break;
		case 'R':
			flags |= ASM_RAW;
			break;
		case 'N':
			flags |= ASM_NO_STRINGS;
			break;
		case '?':
			ERROR("Unknown command line argument");
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage();
		ERROR("Wrong number of arguments.");
	}

	if (!output_file) {
		usage();
		ERROR("No output file given");
	}

	if (!(ain = ain_open(argv[0], &err))) {
		ERROR("Failed to open ain file: %s", ain_strerror(err));
	}

	if (code_file) {
		asm_assemble_jam(code_file, ain, flags);
	}

	if (decl_file) {
		read_declarations(decl_file, ain);
	}

	NOTICE("Writing AIN file...");
	ain_write(output_file, ain);

	ain_free(ain);
	return 0;
}
