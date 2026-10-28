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

#include "asset_manager.h"
#include "audio_meta.h"
#include "xsystem4.h"

#define BGI_MAX 100

struct bgi_entry {
	int no;
	struct audio_meta meta;
};

static int nr_bgi;
static struct bgi_entry bgi_entries[BGI_MAX];

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

static void load_bgi(const char *path)
{
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp) {
		WARNING("Failed to open bgi file: %s", display_utf0(path));
		return;
	}

	char buf[256];
	nr_bgi = 0;
	while (nr_bgi < BGI_MAX && bgi_gets(buf, sizeof(buf), fp)) {
		struct bgi_entry *e = &bgi_entries[nr_bgi];
		int terminator;
		if (sscanf(buf, " %d, %d, %d, %d, %d",
			   &e->no,
			   &e->meta.loop_count,
			   &e->meta.loop_start,
			   &e->meta.loop_end,
			   &terminator) != 5) {
			continue;
		} else if (terminator != -1) {
			goto new_format;
		}
		e->meta.fields = AUDIO_META_LOOP_COUNT | AUDIO_META_LOOP_RANGE;
		nr_bgi++;
	}
	fclose(fp);
	return;
new_format:
	fseek(fp, 0, SEEK_SET);
	nr_bgi = 0;
	while (nr_bgi < BGI_MAX && bgi_gets(buf, sizeof(buf), fp)) {
		struct bgi_entry *e = &bgi_entries[nr_bgi];
		if (sscanf(buf, " %d, %d, %d, %d, %d, %d",
			   &e->no,
			   &e->meta.loop_count,
			   &e->meta.loop_start,
			   &e->meta.loop_end,
			   &e->meta.group,
			   &e->meta.volume) != 6) {
			continue;
		}
		e->meta.fields = AUDIO_META_GROUP | AUDIO_META_LOOP_COUNT |
			AUDIO_META_LOOP_RANGE | AUDIO_META_VOLUME;
		nr_bgi++;
	}
	fclose(fp);
}

static int nr_wai = 0;
static struct audio_meta *wai_meta;

static void load_wai(const char *path)
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

	free(wai_meta);
	wai_meta = xcalloc(count, sizeof(struct audio_meta));
	for (int i = 0; i < count; i++) {
		wai_meta[i] = (struct audio_meta) {
			.fields = AUDIO_META_GROUP,
			.group = LittleEndian_getDW(data, i*record_size + 8),
		};
	}
	nr_wai = count;

end:
	free(file);
}

void audio_meta_load(void)
{
	if (config.bgi_path)
		load_bgi(config.bgi_path);
	if (config.wai_path)
		load_wai(config.wai_path);
}

const struct audio_meta *audio_meta_get_by_number(enum asset_type type, int no)
{
	if (type == ASSET_BGM) {
		for (int i = 0; i < nr_bgi; i++)
			if (bgi_entries[i].no == no)
				return &bgi_entries[i].meta;
	} else if (type == ASSET_SOUND && no > 0 && no <= nr_wai) {
		return &wai_meta[no - 1];
	}
	return NULL;
}
