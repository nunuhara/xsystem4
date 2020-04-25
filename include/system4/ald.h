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
#include <stdio.h>
#include "system4/archive.h"

#define ALD_FILEMAX 255
#define ALD_DATAMAX 65535

struct ald_archive {
	struct archive ar;
	int nr_files;
	struct {
		uint8_t *data;
		size_t size;
		char *name;
		FILE *fp;
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

struct ald_archive_data {
	struct archive_data data;
	int disk;
	int dataptr;
	int hdr_size;
};

enum {
	ALDFILE_BGM,
	ALDFILE_CG,
	ALDFILE_WAVE,
	ALDFILETYPE_MAX
};

// FIXME: this doesn't belong in a libsys4 header
struct archive *ald[ALDFILETYPE_MAX];

/*
 * Open an ALD archive.
 */
struct archive *ald_open(char **files, int count, int flags, int *error);

#endif /* SYSTEM4_ALD_H */
