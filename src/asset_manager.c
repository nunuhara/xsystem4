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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <dirent.h>
#include <limits.h>
#include <assert.h>

#include "system4.h"
#include "system4/ald.h"
#include "system4/afa.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "asset_manager.h"

enum archive_type {
	AR_TYPE_ALD,
	AR_TYPE_AFA,
	AR_TYPE_MAX
};

const char *asset_strtype(enum asset_type type)
{
	switch (type) {
	case ASSET_BGM:   return "BGM";
	case ASSET_SOUND: return "Sound";
	case ASSET_VOICE: return "Voice";
	case ASSET_CG:    return "CG";
	case ASSET_FLAT:  return "Flat";
	case ASSET_PACT:  return "Pact";
	case ASSET_DATA:  return "Data";
	}
	return "Invalid";
}

static struct archive *archives[ASSET_TYPE_MAX] = {0};

static void ald_init(enum asset_type type, char **files, int count)
{
	if (count < 1)
		return;
	if (archives[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));
	int error = ARCHIVE_SUCCESS;
	archives[type] = ald_open(files, count, ARCHIVE_MMAP, &error);
	if (error)
		ERROR("Failed to open ALD file: %s", archive_strerror(error));
	for (int i = 0; i < count; i++) {
		free(files[i]);
	}
}

static void afa_init(enum asset_type type, char *file)
{
	if (!file)
		return;
	if (archives[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));
	int error = ARCHIVE_SUCCESS;
	archives[type] = (struct archive*)afa_open(file, ARCHIVE_MMAP, &error);
	if (error != ARCHIVE_SUCCESS) {
		ERROR("Failed to open AFA file: %s", archive_strerror(error));
	}
	free(file);
}

static char *get_base_name(const char *ain_filename)
{
	char *path = sjis2utf(ain_filename, 0);
	char *dot = strrchr(path, '.');
	if (dot)
		*dot = '\0';
	return path;
}

void asset_manager_init(void)
{
	char *ald_filenames[ASSET_TYPE_MAX][ALD_FILEMAX] = {0};
	int ald_count[ASSET_TYPE_MAX] = {0};
	char *afa_filenames[ASSET_TYPE_MAX] = {0};

	UDIR *dir;
	char *d_name;

	if (!(dir = opendir_utf8(config.game_dir))) {
		ERROR("Failed to open directory: %s", display_utf0(config.game_dir));
	}

	char *base = get_base_name(config.ain_filename);
	size_t base_len = strlen(base);

	// get ALD filenames
	while ((d_name = readdir_utf8(dir))) {
		// archive filename must begin with ain filename
		if (strncmp(base, d_name, base_len))
			goto loop_next;
		const char *ext = file_extension(d_name);
		if (!ext)
			goto loop_next;
		if (!strcasecmp(ext, "ald")) {
			int dno = toupper(d_name[base_len+1]) - 'A';
			if (dno < 0 || dno >= ALD_FILEMAX) {
				WARNING("Invalid ALD index: %s", display_utf0(d_name));
				goto loop_next;
			}

			switch (d_name[base_len]) {
			case 'b':
			case 'B':
				ald_filenames[ASSET_BGM][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_BGM] = max(ald_count[ASSET_BGM], dno+1);
				break;
			case 'g':
			case 'G':
				ald_filenames[ASSET_CG][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_CG] = max(ald_count[ASSET_CG], dno+1);
				break;
			case 'w':
			case 'W':
				ald_filenames[ASSET_SOUND][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_SOUND] = max(ald_count[ASSET_SOUND], dno+1);
				break;
			case 'd':
			case 'D':
				ald_filenames[ASSET_DATA][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_DATA] = max(ald_count[ASSET_DATA], dno+1);
				break;
			default:
				WARNING("Unhandled ALD file: %s", display_utf0(d_name));
				break;
			}
		} else if (!strcasecmp(ext, "bgi")) {
			if (config.bgi_path) {
				WARNING("Multiple bgi files");
				goto loop_next;
			}
			config.bgi_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "wai")) {
			if (config.wai_path) {
				WARNING("Multiple wai files");
				goto loop_next;
			}
			config.wai_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "ex")) {
			if (config.ex_path) {
				WARNING("Multiple ex files");
				goto loop_next;
			}
			config.ex_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "afa")) {
			const char *type = d_name + base_len;
			if (!strcmp(type, "BA.afa")) {
				afa_filenames[ASSET_BGM] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "WA.afa") || !strcmp(type, "Sound.afa")) {
				afa_filenames[ASSET_SOUND] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Voice.afa")) {
				afa_filenames[ASSET_VOICE] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "CG.afa")) {
				afa_filenames[ASSET_CG] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Flat.afa")) {
				afa_filenames[ASSET_FLAT] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Pact.afa")) {
				afa_filenames[ASSET_PACT] = path_join(config.game_dir, d_name);
			}
		}
	loop_next:
		free(d_name);
	}
	closedir_utf8(dir);
	free(base);

	// open ALD archives
	ald_init(ASSET_BGM, ald_filenames[ASSET_BGM], ald_count[ASSET_BGM]);
	ald_init(ASSET_SOUND, ald_filenames[ASSET_SOUND], ald_count[ASSET_SOUND]);
	ald_init(ASSET_VOICE, ald_filenames[ASSET_VOICE], ald_count[ASSET_VOICE]);
	ald_init(ASSET_CG, ald_filenames[ASSET_CG], ald_count[ASSET_CG]);
	ald_init(ASSET_FLAT, ald_filenames[ASSET_FLAT], ald_count[ASSET_FLAT]);
	ald_init(ASSET_PACT, ald_filenames[ASSET_PACT], ald_count[ASSET_PACT]);
	ald_init(ASSET_DATA, ald_filenames[ASSET_DATA], ald_count[ASSET_DATA]);

	// open AFA archives
	afa_init(ASSET_BGM, afa_filenames[ASSET_BGM]);
	afa_init(ASSET_SOUND, afa_filenames[ASSET_SOUND]);
	afa_init(ASSET_VOICE, afa_filenames[ASSET_VOICE]);
	afa_init(ASSET_CG, afa_filenames[ASSET_CG]);
	afa_init(ASSET_FLAT, afa_filenames[ASSET_FLAT]);
	afa_init(ASSET_PACT, afa_filenames[ASSET_PACT]);
	afa_init(ASSET_DATA, afa_filenames[ASSET_DATA]);
}

bool asset_exists(enum asset_type type, int no)
{
	return archives[type] && archive_exists(archives[type], no);
}

struct archive_data *asset_get(enum asset_type type, int no)
{
	if (!archives[type])
		return NULL;
	return archive_get(archives[type], no);
}

static struct hash_table *cg_name_index = NULL;
static struct hash_table *cg_id_index = NULL;

// Convert the last sequence of digits in a string to an integer.
static int cg_name_to_int(const char *name)
{
	size_t len = strlen(name);
	int i;
	for (i = len - 1; i >= 0 && !isdigit(name[i]); i--);
	if (i < 0)
		return -1;
	assert(isdigit(name[i]));
	for (; i > 0 && isdigit(name[i-1]); i--);
	assert(isdigit(name[i]));

	if (!(i = atoi(name+i)))
		return -1;
	return i;
}

static void add_cg_to_index(struct archive_data *data, possibly_unused void *_)
{
	int logical_no = cg_name_to_int(data->name);
	if (logical_no < 0) {
		WARNING("Can't determine logical index for CG: %s", display_sjis0(data->name));
		return;
	}

	struct ht_slot *slot = ht_put_int(cg_id_index, logical_no, NULL);
	if (slot->value)
		ERROR("Duplicate CG numbers");
	slot->value = (void*)(uintptr_t)(data->no + 1);
}

/*
 * NOTE: Starting in Shaman's Sanctuary, .afa files are used for CGs but the
 *       library APIs still use integers rather than names to reference them.
 *       Thus it is necessary to parse file names and create an index.
 */
void asset_cg_index_init(void)
{
	if (!archives[ASSET_CG])
		return;

	cg_id_index = ht_create(4096);
	archive_for_each(archives[ASSET_CG], add_cg_to_index, NULL);
}

static void add_cg_name_to_index(struct archive_data *data, possibly_unused void *_)
{
	// copy name and strip file extension
	char *name = strdup(data->name);
	char *dot = strrchr(name, '.');
	if (dot)
		*dot = '\0';

	struct ht_slot *slot = ht_put(cg_name_index, name, NULL);
	if (slot->value)
		ERROR("Duplicate CG names");
	slot->value = (void*)(uintptr_t)(data->no + 1);

	free(name);
}

static void init_cg_name_index(void)
{
	if (!archives[ASSET_CG])
		return;

	cg_name_index = ht_create(4096);
	archive_for_each(archives[ASSET_CG], add_cg_name_to_index, NULL);
}

int asset_cg_name_to_index(const char *name)
{
	if (!cg_name_index)
		init_cg_name_index();
	return (uintptr_t)ht_get(cg_name_index, name, NULL);
}

static int cg_translate_index(int no)
{
	if (!cg_id_index)
		return no;
	return (uintptr_t)ht_get_int(cg_id_index, no, NULL);
}

struct cg *asset_cg_load(int no)
{
	if (!archives[ASSET_CG])
		return NULL;
	if (!(no = cg_translate_index(no)))
		return NULL;
	return cg_load(archives[ASSET_CG], no - 1);
}

struct cg *asset_cg_load_by_name(const char *name, int *no)
{
	if (!archives[ASSET_CG])
		return NULL;
	if (!(*no = asset_cg_name_to_index(name)))
		return NULL;
	return cg_load(archives[ASSET_CG], *no - 1);
}

bool asset_cg_exists(int no)
{
	if (!archives[ASSET_CG])
		return false;
	if (!(no = cg_translate_index(no)))
		return false;
	return archive_exists(archives[ASSET_CG], no - 1);
}

bool asset_cg_get_metrics(int no, struct cg_metrics *metrics)
{
	if (!archives[ASSET_CG])
		return false;
	if (!(no = cg_translate_index(no)))
		return NULL;
	return cg_get_metrics(archives[ASSET_CG], no - 1, metrics);
}

