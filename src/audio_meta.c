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
#include <string.h>
#include "system4.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/little_endian.h"
#include "system4/string.h"

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

struct timemap_entry {
	struct string *name;
	struct audio_meta meta;
};

static struct timemap_entry *timemap;
static size_t nr_timemap;
static size_t timemap_capacity;
static struct hash_table *timemap_meta_by_name;
static struct hash_table *timemap_name_by_number;

static struct ex_value *get_cell(struct ex_table *table, unsigned row, int col,
				 enum ex_value_type type)
{
	struct ex_value *v = ex_table_get(table, row, col);
	return v && v->type == type ? v : NULL;
}

static void load_timemap(enum asset_type type, const char *name)
{
	struct archive_data *data = asset_get_by_name(type, name, NULL);
	if (!data)
		return;
	struct ex *ex = ex_read(data->data, data->size);
	archive_free_data(data);
	if (!ex)
		return;
	struct ex_table *table = ex_get_table(ex, "SoundInfo");
	if (!table)
		goto done;
	int name_col = ex_col_from_name(table, "Name");
	int group_col = ex_col_from_name(table, "Group");
	int begin_col = ex_col_from_name(table, "LoopBeginSample");
	int end_col = ex_col_from_name(table, "LoopEndSample");
	if (name_col < 0 || group_col < 0 || begin_col < 0 || end_col < 0)
		goto done;
	for (unsigned row = 0; row < table->nr_rows; row++) {
		struct ex_value *name_v = get_cell(table, row, name_col, EX_STRING);
		struct ex_value *group_v = get_cell(table, row, group_col, EX_INT);
		struct ex_value *begin_v = get_cell(table, row, begin_col, EX_INT);
		struct ex_value *end_v = get_cell(table, row, end_col, EX_INT);
		if (!name_v || !group_v || !begin_v || !end_v)
			continue;
		if (nr_timemap == timemap_capacity) {
			timemap_capacity = timemap_capacity ? timemap_capacity * 2 : 64;
			timemap = xrealloc(timemap, timemap_capacity * sizeof(*timemap));
		}
		timemap[nr_timemap++] = (struct timemap_entry) {
			.name = string_ref(name_v->s),
			.meta = { .fields = AUDIO_META_GROUP | AUDIO_META_LOOP_RANGE,
				.group = group_v->i, .loop_start = begin_v->i,
				.loop_end = end_v->i },
		};
	}
done:
	ex_free(ex);
}

static void index_timemap(void)
{
	timemap_meta_by_name = ht_create(nr_timemap * 3 / 2);
	timemap_name_by_number = ht_create(nr_timemap * 3 / 2);
	for (size_t i = 0; i < nr_timemap; i++) {
		struct timemap_entry *e = &timemap[i];
		// When two entries have the same name, use the entry that was loaded first.
		struct ht_slot *name_slot = ht_put(timemap_meta_by_name, e->name->text, &e->meta);
		if (name_slot->value != &e->meta)
			continue;

		int number;
		if (sscanf(e->name->text, "%d", &number) != 1)
			continue;
		// When two names start with the same number, use the name that is first in name order.
		struct ht_slot *number_slot = ht_put_int(timemap_name_by_number, number, e->name);
		struct string *first = number_slot->value;
		if (strcmp(e->name->text, first->text) < 0)
			number_slot->value = e->name;
	}
}

void audio_meta_load(void)
{
	if (config.bgi_path)
		load_bgi(config.bgi_path);
	if (config.wai_path)
		load_wai(config.wai_path);
}

void audio_meta_load_timemap(void)
{
	if (timemap_meta_by_name)
		return;
	load_timemap(ASSET_SOUND, "timemap_sound");
	load_timemap(ASSET_VOICE, "timemap_voice");
	index_timemap();
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

const struct audio_meta *audio_meta_get_by_name(const char *name)
{
	if (!timemap_meta_by_name)
		return NULL;
	return ht_get(timemap_meta_by_name, name, NULL);
}

struct string *audio_meta_get_sound_name_by_number(int number)
{
	if (!timemap_name_by_number)
		return NULL;
	return ht_get_int(timemap_name_by_number, number, NULL);
}
