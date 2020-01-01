/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * Based on code from xsystem35
 * Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
 *               1998-                           <masaki-c@is.aist-nara.ac.jp>
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

#ifndef SYSTEM4_ALD_H
#define SYSTEM4_ALD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ALD_FILEMAX 255
#define ALD_DATAMAX 65535

enum ald_error {
	ALD_SUCCESS,
	ALD_FILE_ERROR,
	ALD_BAD_ARCHIVE_ERROR,
	ALD_MAX_ERROR
};

enum {
	ALD_MMAP = 1
};

struct ald_archive {
	bool mmapped;
	int nr_files;
	struct {
		uint8_t *data;
		size_t size;
		char *name;
	} files[ALD_FILEMAX];
	// upper limit on how many files could be referenced by this archive
	int maxfile;
	// file numbers (from the file map)
	char *map_disk;
	// ptrmap indices (from the file map)
	short *map_ptr;
	// pointer maps
	int *fileptr[ALD_FILEMAX];
};

struct archive_data {
	size_t size;
	uint8_t *data;
	uint8_t *data_raw;
	char *name;
	struct ald_archive *archive;
};

enum {
	ALDFILE_BGM,
	ALDFILE_CG,
	ALDFILE_WAVE,
	ALDFILETYPE_MAX
};

struct ald_archive *ald[ALDFILETYPE_MAX];

/*
 * Returns a human readable description of an error.
 */
const char *ald_strerror(int error);

/*
 * Open an ALD archive.
 */
struct ald_archive *ald_open(char **files, int count, int flags, int *error);

/*
 * Check if data exists in an ALD archive.
 */
bool ald_data_exists(struct ald_archive *ar, int no);

/*
 * Retrieve a piece of data from an ALD archive.
 */
struct archive_data *ald_get(struct ald_archive *archive, int no);

/*
 * Free an ald_data structure returned by `ald_get`.
 */
void ald_free_data(struct archive_data *data);

/*
 * Free an ald_archive structure returned by `ald_open`.
 */
void ald_free_archive(struct ald_archive *ar);

#endif /* SYSTEM4_ALD_H */
