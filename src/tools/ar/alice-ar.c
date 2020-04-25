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
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <limits.h>
#include <libgen.h>
#include "system4.h"
#include "system4/afa.h"
#include "system4/ald.h"
#include "file.h"
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

// dirname is allowed to return a pointer to static memory OR modify its input.
// This works around the braindamage by ALWAYS returning a pointer to static
// memory, at the cost of a string copy.
char *xdirname(const char *path)
{
	static char buf[PATH_MAX];
	strncpy(buf, path, PATH_MAX-1);
	return dirname(buf);
}

char *xbasename(const char *path)
{
	static char buf[PATH_MAX];
	strncpy(buf, path, PATH_MAX-1);
	return basename(buf);
}

static struct archive *open_ald_archive(const char *path, int *error)
{
	int count = 0;
	char *dir_name = xdirname(path);
	char *base_name = xbasename(path);
	char *ald_filenames[ALD_FILEMAX];
	int prefix_len = strlen(base_name) - 5;
	if (prefix_len <= 0)
		return NULL;

	memset(ald_filenames, 0, sizeof(char*) * ALD_FILEMAX);

	DIR *dir;
	struct dirent *d;
	char filepath[PATH_MAX];

	if (!(dir = opendir(dir_name))) {
		ERROR("Failed to open directory: %s", path);
	}

	while ((d = readdir(dir))) {
		printf("checking %s\n", d->d_name);
		int len = strlen(d->d_name);
		if (len < prefix_len + 5 || strcasecmp(d->d_name+len-4, ".ald"))
			continue;
		if (strncasecmp(d->d_name, base_name, prefix_len))
			continue;

		int dno = toupper(*(d->d_name+len-5)) - 'A';
		if (dno < 0 || dno >= ALD_FILEMAX)
			continue;

		snprintf(filepath, PATH_MAX-1, "%s/%s", dir_name, d->d_name);
		ald_filenames[dno] = strdup(filepath);
		count = max(count, dno+1);
	}

	struct archive *ar = ald_open(ald_filenames, count, ARCHIVE_MMAP, error);

	for (int i = 0; i < ALD_FILEMAX; i++) {
		free(ald_filenames[i]);
	}

	return ar;
}

static struct archive *open_archive(const char *path, int *error)
{
	size_t len = strlen(path);
	if (len < 4)
		goto err;

	const char *ext = path + len - 4;
	if (!strcasecmp(ext, ".ald")) {
		return open_ald_archive(path, error);
	} else if (!strcasecmp(ext, ".afa")) {
		return (struct archive*)afa_open(path, ARCHIVE_MMAP, error);
	}

err:
	usage();
	ERROR("Couldn't determine archive type for '%s'", path);
}

static void mkdir_for_file(const char *filename)
{
	char *tmp = strdup(filename);
	char *dir = dirname(tmp);
	mkdir_p(dir);
	free(tmp);
}

static void write_file(struct archive_data *data, const char *output_file)
{
	FILE *f = NULL;

	if (!output_file) {
		char *u = sjis2utf(data->name, strlen(data->name));
		mkdir_for_file(u);
		if (!(f = fopen(u, "wb")))
			ERROR("fopen failed: %s", strerror(errno));
		free(u);
	} else if (strcmp(output_file, "-")) {
		if (!(f = fopen(output_file, "wb")))
			ERROR("fopen failed: %s", strerror(errno));
	}

	if (fwrite(data->data, data->size, 1, f ? f : stdout) != 1)
		ERROR("fwrite failed: %s", strerror(errno));

	if (f)
		fclose(f);
}

static void extract_all_iter(struct archive_data *data, void *_output_dir)
{
	char *output_dir = _output_dir ? _output_dir : ".";
	char *file_name = sjis2utf(data->name, 0);
	char output_file[PATH_MAX];

	snprintf(output_file, PATH_MAX, "%s/%s", output_dir, file_name);
	free(file_name);

	mkdir_for_file(output_file);

	printf("%s\n", output_file);

	if (!archive_load_file(data)) {
		WARNING("Error loading file: %s", output_file);
		return;
	}
	write_file(data, output_file);
}

static void list_all_iter(struct archive_data *data, possibly_unused void *_)
{
	char *name = sjis2utf(data->name, 0);
	printf("%d: %s\n", data->no, name);
	free(name);
}

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
	//bool force = false;

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
	struct archive *ar = open_archive(argv[0], &error);
	if (!ar) {
		ERROR("Opening archive: %s", archive_strerror(error));
	}

	if (extract) {
		if (file_index >= 0) {
			struct archive_data *d = archive_get(ar, file_index);
			if (!d)
				ERROR("No file with index %d", file_index);
			write_file(d, output_file);
		} else if (file_name) {
			char *u = utf2sjis(file_name, strlen(file_name));
			struct archive_data *d = archive_get_by_name(ar, u);
			write_file(d, output_file);
		} else {
			archive_for_each(ar, extract_all_iter, output_file);
		}
	} else if (add) {
		WARNING("adding files to archive not yet implemented");
	} else if (delete) {
		WARNING("deleting files from archive not yet implemented");
	} else if (list) {
		archive_for_each(ar, list_all_iter, NULL);
	}

	archive_free(ar);
	return 0;
}
