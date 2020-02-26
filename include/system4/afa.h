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

#ifndef SYSTEM4_AFA_H
#define SYSTEM4_AFA_H

#include <stdint.h>
#include <stddef.h>
#include "system4/archive.h"

struct string;

struct afa_entry {
	struct string *name;
	uint32_t off;
	uint32_t size;
	uint32_t unknown0;
	uint32_t unknown1;
	uint32_t unknown2;
};

struct afa_archive {
	struct archive ar;
	char *filename;
	size_t file_size;
	uint32_t version;
	uint32_t unknown;
	uint32_t data_start;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
	uint32_t nr_files;
	struct afa_entry *files;
	uint32_t data_size;
	void *mmap_ptr;
	uint8_t *data;
};

struct afa_archive *afa_open(char *file, int flags, int *error);

#endif /* SYSTEM4_AFA_H */
