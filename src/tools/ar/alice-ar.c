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
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "system4.h"
#include "system4/afa.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static void usage(void)
{
	puts("Usage: alice-ar [options...] input-file");
	puts("    Manipulate AliceSoft archive files (ald, afa)");
	puts("");
	puts("    -h, --help                 Display this message and exit");
	puts("    -x, --extract              Extract archive");
	puts("    -c, --create <manifest>    Create an archive");
	puts("    -a, --add <file>           Add <file> to archive");
	puts("    -d, --delete               Delete a file from the archive");
	puts("    -o, --output               Specify output file/directory");
	puts("    -i, --index <index>        Specify file index");
	puts("    -n, --name <name>          Specify file name");
	puts("    -f, --force                Allow replacing existing files when using the --add option");
}

enum {
	LOPT_HELP = 256,
	LOPT_EXTRACT,
	LOPT_CREATE,
	LOPT_ADD,
	LOPT_DELETE,
	LOPT_LIST,
	LOPT_OUTPUT,
	LOPT_INDEX,
	LOPT_NAME,
	LOPT_FORCE,
};

int main(int argc, char *argv[])
{
	char *output_file = NULL;
	char *input_file = NULL;
	char *file_name = NULL;
	int file_index = -1;

	bool extract = false;
	bool create = false;
	bool add = false;
	bool delete = false;
	bool list = false;
	bool force = false;

	while (1) {
		static struct option long_options[] = {
			{ "help",    no_argument,       0, LOPT_HELP },
			{ "extract", no_argument,       0, LOPT_EXTRACT },
			{ "create",  required_argument, 0, LOPT_CREATE },
			{ "add",     required_argument, 0, LOPT_ADD },
			{ "delete",  no_argument,       0, LOPT_DELETE },
			{ "list",    no_argument,       0, LOPT_LIST },
			{ "output",  required_argument, 0, LOPT_OUTPUT },
			{ "index",   required_argument, 0, LOPT_INDEX },
			{ "name",    required_argument, 0, LOPT_NAME },
			{ "force",   no_argument,       0, LOPT_FORCE },
		};
		int option_index = 0;
		int c = getopt_long(argc, argv, "hxc:a:dlo:i:n:", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case LOPT_HELP:
			usage();
			return 0;
		case 'x':
		case LOPT_EXTRACT:
			extract = true;
			break;
		case 'c':
		case LOPT_CREATE:
			create = true;
			break;
		case 'a':
		case LOPT_ADD:
			input_file = optarg;
			break;
		case 'd':
		case LOPT_DELETE:
			delete = true;
			break;
		case 'l':
		case LOPT_LIST:
			list = true;
			break;
		case 'o':
		case LOPT_OUTPUT:
			output_file = optarg;
			break;
		case 'i':
		case LOPT_INDEX:
			// FIXME: use strtol with error checking
			file_index = atoi(optarg);
			break;
		case 'n':
			file_name = optarg;
			break;
		case '?':
			ERROR("Unrecognized command line argument");
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage();
		ERROR("Wrong number of arguments");
	}

	int nr_commands = extract + create + add + delete + list;
	if (nr_commands > 1)
		ERROR("Multiple commands given in single command line");
	if (!nr_commands)
		ERROR("No command given");

	if (create) {
		WARNING("archive creation not yet implemented");
		return 0;
	}

	int error;
	struct afa_archive *ar = afa_open(argv[0], ARCHIVE_MMAP, &error);
	if (!ar) {
		ERROR("Opening archive: %s", archive_strerror(error));
	}

	if (extract) {
		WARNING("archive extraction not yet implemented");
	} else if (add) {
		WARNING("adding files to archive not yet implemented");
	} else if (delete) {
		WARNING("deleting files from archive not yet implemented");
	} else if (list) {
		for (uint32_t i = 0; i < ar->nr_files; i++) {
			char *u = sjis2utf(ar->files[i].name->text, ar->files[i].name->size);
			printf("%u: %s\n", i, u);
			free(u);
		}
	}

	archive_free(&ar->ar);
	return 0;
}
