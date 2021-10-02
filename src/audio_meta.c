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
#include "system4.h"
#include "mixer.h"

#define BGI_MAX 100

static int bgi_nfile;
static struct bgi bgi_data[BGI_MAX];

static char *bgi_gets(char *buf, int n, FILE *fp)
{
	char *s = buf;
	int c;
	while (--n > 0 && (c = fgetc(fp)) != EOF) {
		c = c >> 4 | (c & 0xf) << 4;  // decrypt
		*s++ = c;
		if (c == '\n')
			break;
	}
	if (s == buf && c == EOF)
		return NULL;
	*s = '\0';
	return buf;
}

void bgi_read(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		WARNING("Failed to open bgi file: %s", path);
		return;
	}

	char buf[256];
	bgi_nfile = 0;
	while (bgi_nfile < BGI_MAX && bgi_gets(buf, sizeof(buf), fp)) {
		int terminator;
		if (sscanf(buf, " %d, %d, %d, %d, %d",
			   &bgi_data[bgi_nfile].no,
			   &bgi_data[bgi_nfile].loop_count,
			   &bgi_data[bgi_nfile].loop_start,
			   &bgi_data[bgi_nfile].loop_end,
			   &terminator) != 5) {
			continue;
		} else if (terminator != -1) {
			goto new_format;
		}
		bgi_data[bgi_nfile].channel = -1;
		bgi_data[bgi_nfile].volume = 100;
		bgi_nfile++;
	}
	return;
new_format:
	fseek(fp, 0, SEEK_SET);
	bgi_nfile = 0;
	while (bgi_nfile < BGI_MAX && bgi_gets(buf, sizeof(buf), fp)) {
		if (sscanf(buf, " %d, %d, %d, %d, %d, %d",
			   &bgi_data[bgi_nfile].no,
			   &bgi_data[bgi_nfile].loop_count,
			   &bgi_data[bgi_nfile].loop_start,
			   &bgi_data[bgi_nfile].loop_end,
			   &bgi_data[bgi_nfile].channel,
			   &bgi_data[bgi_nfile].volume) != 6) {
			continue;
		}
		bgi_nfile++;
	}
}

struct bgi *bgi_get(int no)
{
	for (int i = 0; i < bgi_nfile; i++) {
		if (bgi_data[i].no == no)
			return &bgi_data[i];
	}
	return NULL;
}
