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
#include <getopt.h>
#include "system4.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "ast.h"

void ex_write(FILE *out, struct ex *ex);

static void usage(void)
{
	puts("Usage: exbuild [options...] input-file");
	puts("    Build EX files.");
	puts("");
	puts("    -h, --help      Display this message and exit");
	puts("    -o, --output    Set the output file path");
}

enum {
	LOPT_HELP = 256,
	LOPT_OUTPUT,
};

int main(int argc, char *argv[])
{
	const char *output_file = NULL;

	while (1) {
		static struct option long_options[] = {
			{ "help",   no_argument,       0, LOPT_HELP },
			{ "output", required_argument, 0, LOPT_OUTPUT },
		};
		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "ho:", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case LOPT_HELP:
			usage();
			return 0;
		case 'o':
		case LOPT_OUTPUT:
			output_file = optarg;
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

	FILE *out = stdout;
	if (output_file)
		out = fopen(output_file, "wb");
	if (!out)
		ERROR("fopen failed: %s", strerror(errno));

	FILE *in;
	if (!strcmp(argv[0], "-"))
		in = stdin;
	else
		in = fopen(argv[0], "rb");
	if (!in)
		ERROR("fopen failed: %s", strerror(errno));

	struct ex *ex = ex_parse(in);
	ex_write(out, ex);
	ex_free(ex);
	return 0;
}
