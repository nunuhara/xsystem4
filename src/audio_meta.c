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
#include <stdlib.h>
#include "system4.h"
#include "system4/file.h"
#include "system4/little_endian.h"

#include "mixer.h"
#include "xsystem4.h"

#define BGI_MAX 100

static int bgi_nfile;
static struct bgi bgi_data[BGI_MAX];

static char *bgi_gets(char *buf, int n, FILE *fp)
{
	char *s = buf;
	int c = EOF;
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
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp) {
		WARNING("Failed to open bgi file: %s", display_utf0(path));
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

static struct wai *wai_data = NULL;
static int nr_wai = 0;

void wai_load(const char *path)
{
	size_t len;
	uint8_t *file = file_read(path, &len);

	if (len < 48 || file[0] != 'X' || file[1] != 'I' || file[2] != '2' || file[3] != '\0') {
		WARNING("Not a .wai file: %s", display_utf0(path));
		goto end;
	}
	int count = LittleEndian_getDW(file, 8) - 1;
	if (count < 0) {
		WARNING("Invalid .wai count: %d", count);
		goto end;
	}
	int version = LittleEndian_getDW(file, 12);
	if (version < 3 || version > 4) {
		WARNING("Unsupported .wai version: %d", version);
		goto end;
	}

	uint8_t *data;
	int record_size;
	if (version == 4) {
		data = file + 40;
		record_size = 4*4;
	} else {
		data = file + 36;
		record_size = 3*4;
	}

	if (len - (version == 4 ? 40 : 36) < (size_t)count*record_size) {
		WARNING(".wai file truncated");
		goto end;
	}

	wai_data = xcalloc(count, sizeof(struct wai));
	for (int i = 0; i < count; i++) {
		wai_data[i].channel = LittleEndian_getDW(data, i*record_size + 8);
	}
	nr_wai = count;

end:
	free(file);
}

struct wai *wai_get(int no)
{
	no--;
	if (no < 0 || no >= nr_wai)
		return NULL;
	return &wai_data[no];
}
