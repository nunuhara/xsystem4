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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "system4.h"

void *file_read(const char *path, size_t *len_out)
{
	FILE *fp;
	long len;
	uint8_t *buf;

	if (!(fp = fopen(path, "rb")))
		return NULL;

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	buf = xmalloc(len + 1);
	if (fread(buf, len, 1, fp) != 1) {
		free(buf);
		return NULL;
	}
	if (fclose(fp)) {
		free(buf);
		return NULL;
	}

	if (len_out)
		*len_out = len;

	buf[len] = 0;
	return buf;
}

bool file_exists(const char *path)
{
	return access(path, F_OK) != -1;
}
