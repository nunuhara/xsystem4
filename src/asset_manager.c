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
#include "gfx/font.h"

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
		} else if (!strcasecmp(ext, "fnl")) {
			if (!config.fnl_path)
				config.fnl_path = path_join(config.game_dir, d_name);
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
	return archives[type] && archive_exists(archives[type], no-1);
}

struct archive_data *asset_get(enum asset_type type, int no)
{
	if (!archives[type])
		return NULL;
	return archive_get(archives[type], no-1);
}

struct archive_data *asset_get_by_name(enum asset_type type, const char *name)
{
	if (!archives[type])
		return NULL;
	return archive_get_by_name(archives[type], name);
}

int asset_name_to_index(enum asset_type type, const char *name)
{
	if (!archives[type])
		return 0;
	// FIXME: should have archive_get_descriptor_by_name function to avoid loading data
	struct archive_data *data = archive_get_by_name(archives[type], name);
	if (!data)
		return 0;
	int index = data->no + 1;
	archive_free_data(data);
	return index;
}

struct cg *asset_cg_load(int no)
{
	struct archive_data *data = asset_get(ASSET_CG, no);
	if (!data)
		return NULL;

	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	return cg;
}

struct cg *asset_cg_load_by_name(const char *name, int *no)
{
	struct archive_data *data = asset_get_by_name(ASSET_CG, name);
	if (!data)
		return NULL;

	struct cg *cg = cg_load_data(data);
	*no = data->no + 1;
	archive_free_data(data);
	return cg;
}

bool asset_cg_get_metrics(int no, struct cg_metrics *metrics)
{
	struct archive_data *data = asset_get(ASSET_CG, no);
	if (!data)
		return false;

	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}
