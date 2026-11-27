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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef __SWITCH__
#include <sys/stat.h>
#endif
#include <zlib.h>
#ifdef _WIN32
#include <io.h>
#else
#include <strings.h>
#include <unistd.h>
#endif
#include "cJSON.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modifier.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "xsystem4.h"

static int current_global;

// Preserve static EX-database caches initialized at startup (such as Rance 10's
// g_questCollection and g_quest) that were placed in the local save group, so
// loading pre-MOD / older saves does not overwrite active MOD quest tables.
static bool should_preserve_static_global(int global_index)
{
	if (global_index < 0 || global_index >= ain->nr_globals)
		return false;
	const char *name = ain->globals[global_index].name;
	if (!name)
		return false;
	if (!strcmp(name, "g_questCollection")) {
		struct page *p = heap_get_page(global_get(global_index).i);
		if (p && p->type == STRUCT_PAGE && p->nr_vars >= 2) {
			struct page *quests = heap_get_page(p->values[0].i);
			if (quests && quests->type == ARRAY_PAGE && quests->nr_vars > 0)
				return true;
		}
	} else if (!strcmp(name, "g_quest")) {
		struct page *arr = heap_get_page(global_get(global_index).i);
		if (arr && arr->type == ARRAY_PAGE && arr->nr_vars > 0)
			return true;
	}
	return false;
}

#define invalid_save_data(msg, data) do {				\
		char *str = (data) ? cJSON_Print(data) : xstrdup("NULL"); \
		if (!str) str = xstrdup("PRINTING FAILED");		\
		WARNING("Invalid save data (%d): " msg ": %s", current_global, str); \
		free(str);						\
	} while (0)

static char *make_atomic_tmp_path(const char *path)
{
	const char *slash1 = strrchr(path, '/');
	const char *slash2 = strrchr(path, '\\');
	const char *slash = slash1;
	if (slash2 && (!slash || slash2 > slash))
		slash = slash2;
	size_t dir_len = slash ? (size_t)(slash - path + 1) : 0;
	const char *base = slash ? slash + 1 : path;
	size_t base_len = strlen(base);
	char *tmp = xmalloc(dir_len + 1 + base_len + 4 + 1);
	if (dir_len)
		memcpy(tmp, path, dir_len);
	tmp[dir_len] = '.';
	memcpy(tmp + dir_len + 1, base, base_len);
	memcpy(tmp + dir_len + 1 + base_len, ".tmp", 5);
	return tmp;
}

static char *make_backup_path(const char *path)
{
	size_t len = strlen(path);
	char *bak = xmalloc(len + 5);
	memcpy(bak, path, len);
	memcpy(bak + len, ".bak", 5);
	return bak;
}

static bool is_critical_global_save(const char *path)
{
	if (!path)
		return false;
	const char *slash1 = strrchr(path, '/');
	const char *slash2 = strrchr(path, '\\');
	const char *base = slash1;
	if (slash2 && (!base || slash2 > base))
		base = slash2;
	base = base ? base + 1 : path;
#ifdef _WIN32
	return !_stricmp(base, "GlobalSave.asd") || !_stricmp(base, "GlobalSaveData.asd") || !_stricmp(base, "Collection.asd") || !_stricmp(base, "GameConfig.asd");
#else
	return !strcasecmp(base, "GlobalSave.asd") || !strcasecmp(base, "GlobalSaveData.asd") || !strcasecmp(base, "Collection.asd") || !strcasecmp(base, "GameConfig.asd");
#endif
}

static bool is_game_config_save(const char *path)
{
	if (!path)
		return false;
	const char *slash1 = strrchr(path, '/');
	const char *slash2 = strrchr(path, '\\');
	const char *base = slash1;
	if (slash2 && (!base || slash2 > base))
		base = slash2;
	base = base ? base + 1 : path;
#ifdef _WIN32
	return !_stricmp(base, "GameConfig.asd");
#else
	return !strcasecmp(base, "GameConfig.asd");
#endif
}

#define MAX_TRACKED_GLOBAL_SAVES 8
struct tracked_global_save {
	char *keyname;
	char *filename;
	char *group_name;
};
static struct tracked_global_save tracked_global_saves[MAX_TRACKED_GLOBAL_SAVES];
static int nr_tracked_global_saves = 0;
static bool flushing_background_saves = false;

static const char *save_basename(const char *path)
{
	if (!path)
		return "";
	const char *slash1 = strrchr(path, '/');
	const char *slash2 = strrchr(path, '\\');
	const char *base = slash1;
	if (slash2 && (!base || slash2 > base))
		base = slash2;
	return base ? base + 1 : path;
}

static bool is_background_flush_save(const char *path)
{
	const char *base = save_basename(path);
	if (!base[0])
		return false;
#ifdef _WIN32
	return !_stricmp(base, "GlobalSave.asd") ||
	       !_stricmp(base, "GlobalSaveData.asd") ||
	       !_stricmp(base, "Collection.asd") ||
	       !_stricmp(base, "GameConfig.asd") ||
	       !_stricmp(base, "GameEnquete.asd") ||
	       !_stricmp(base, "AFCommon.asd") ||
	       !_stricmp(base, "AFConfig.asd") ||
	       !_stricmp(base, "AFInfo.asd");
#else
	return !strcasecmp(base, "GlobalSave.asd") ||
	       !strcasecmp(base, "GlobalSaveData.asd") ||
	       !strcasecmp(base, "Collection.asd") ||
	       !strcasecmp(base, "GameConfig.asd") ||
	       !strcasecmp(base, "GameEnquete.asd") ||
	       !strcasecmp(base, "AFCommon.asd") ||
	       !strcasecmp(base, "AFConfig.asd") ||
	       !strcasecmp(base, "AFInfo.asd");
#endif
}

static void track_global_save(const char *keyname, const char *filename, const char *group_name)
{
	if (flushing_background_saves || !keyname || !filename || !group_name || !group_name[0])
		return;
	if (!is_background_flush_save(filename))
		return;
	for (int i = 0; i < nr_tracked_global_saves; i++) {
		if (!strcmp(tracked_global_saves[i].filename, filename)) {
			free(tracked_global_saves[i].keyname);
			free(tracked_global_saves[i].group_name);
			tracked_global_saves[i].keyname = xstrdup(keyname);
			tracked_global_saves[i].group_name = xstrdup(group_name);
			return;
		}
	}
	if (nr_tracked_global_saves >= MAX_TRACKED_GLOBAL_SAVES)
		return;
	tracked_global_saves[nr_tracked_global_saves].keyname = xstrdup(keyname);
	tracked_global_saves[nr_tracked_global_saves].filename = xstrdup(filename);
	tracked_global_saves[nr_tracked_global_saves].group_name = xstrdup(group_name);
	nr_tracked_global_saves++;
}

static void untrack_global_save(const char *filename)
{
	if (!filename)
		return;
	for (int i = 0; i < nr_tracked_global_saves; i++) {
		if (!strcmp(tracked_global_saves[i].filename, filename)) {
			free(tracked_global_saves[i].keyname);
			free(tracked_global_saves[i].filename);
			free(tracked_global_saves[i].group_name);
			for (int j = i; j + 1 < nr_tracked_global_saves; j++)
				tracked_global_saves[j] = tracked_global_saves[j + 1];
			nr_tracked_global_saves--;
			return;
		}
	}
}

void savedata_reset(void)
{
	for (int i = 0; i < nr_tracked_global_saves; i++) {
		free(tracked_global_saves[i].keyname);
		free(tracked_global_saves[i].filename);
		free(tracked_global_saves[i].group_name);
		tracked_global_saves[i].keyname = NULL;
		tracked_global_saves[i].filename = NULL;
		tracked_global_saves[i].group_name = NULL;
	}
	nr_tracked_global_saves = 0;
}

FILE *atomic_save_open(const char *path, const char *mode, char **tmp_path_out)
{
	char *tmp = make_atomic_tmp_path(path);
	char *tmp_copy = xstrdup(tmp);
	char *dir = path_dirname(tmp_copy);
	if (dir && dir[0])
		mkdir_p(dir);
	free(tmp_copy);
	FILE *fp = file_open_utf8(tmp, mode);
	if (!fp) {
		free(tmp);
		*tmp_path_out = NULL;
		return NULL;
	}
	*tmp_path_out = tmp;
	return fp;
}

void atomic_save_abort(FILE *fp, char *tmp_path)
{
	if (fp)
		fclose(fp);
	if (tmp_path) {
		remove_utf8(tmp_path);
		free(tmp_path);
	}
}

#ifdef __SWITCH__
static int commit_switch_save(const char *tmp_path, const char *path)
{
	int rc = rename(tmp_path, path);
	if (rc == 0 || errno != EEXIST)
		return rc;

	struct stat st;
	if (stat(path, &st) != 0)
		return -1;
	if (!S_ISREG(st.st_mode)) {
		errno = EISDIR;
		return -1;
	}

	char *recovery = xmalloc(strlen(path) + sizeof(".switch-old"));
	sprintf(recovery, "%s.switch-old", path);
	if ((unlink(recovery) != 0 && errno != ENOENT) || rename(path, recovery) != 0) {
		int error = errno;
		free(recovery);
		errno = error;
		return -1;
	}

	rc = rename(tmp_path, path);
	int error = errno;
	if (rc == 0) {
		unlink(recovery);
	} else if (rename(recovery, path) != 0) {
		WARNING("Previous save remains at %s: %s", display_utf0(recovery), strerror(errno));
	}
	free(recovery);
	errno = error;
	return rc;
}
#endif

bool atomic_save_commit(FILE *fp, const char *path, char *tmp_path, bool keep_backup)
{
	bool volatile_mem = savedir_is_memory_save_path(path);
	bool ok = !ferror(fp) && fflush(fp) == 0;
#ifdef _WIN32
	if (ok && !volatile_mem && _commit(_fileno(fp)) != 0)
		ok = false;
#else
	if (ok && !volatile_mem && fsync(fileno(fp)) != 0 && errno != EINVAL && errno != EROFS)
		ok = false;
#endif
	if (fclose(fp) != 0)
		ok = false;
	if (!ok) {
		WARNING("Error flushing temporary save file %s: %s", display_utf0(tmp_path), strerror(errno));
		remove_utf8(tmp_path);
		free(tmp_path);
		return false;
	}

	if (!volatile_mem && keep_backup && file_exists(path) && file_size(path) > 16) {
		char *bak = make_backup_path(path);
		file_copy(path, bak);
		free(bak);
	}

#ifdef _WIN32
	wchar_t *wtmp = utf8_to_wchar(tmp_path);
	wchar_t *wpath = utf8_to_wchar(path);
	remove_utf8(path);
	int rc = _wrename(wtmp, wpath);
	free(wtmp);
	free(wpath);
#elif defined(__SWITCH__)
	int rc = commit_switch_save(tmp_path, path);
#else
	int rc = rename(tmp_path, path);
#endif
	if (rc != 0) {
		WARNING("Failed renaming %s -> %s: %s", display_utf0(tmp_path), display_utf0(path), strerror(errno));
		remove_utf8(tmp_path);
		free(tmp_path);
		return false;
	}
	file_note_fs_mutation();
	free(tmp_path);
	return true;
}

int save_json(const char *filename, cJSON *json)
{
	char *str = cJSON_Print(json);
	if (!str)
		return 0;

	char *path = savedir_path(filename);
	char *tmp_path = NULL;
	FILE *f = atomic_save_open(path, "wb", &tmp_path);
	if (!f) {
		WARNING("Failed to open save file: %s: %s", display_utf0(filename), strerror(errno));
		free(path);
		free(str);
		return 0;
	}

	size_t len = strlen(str);
	if (len > 0 && fwrite(str, len, 1, f) != 1) {
		WARNING("Failed to write save file: %s", strerror(errno));
		atomic_save_abort(f, tmp_path);
		free(path);
		free(str);
		return 0;
	}
	bool ok = atomic_save_commit(f, path, tmp_path, is_critical_global_save(filename));
	free(path);
	free(str);
	return ok ? 1 : 0;
}

cJSON *load_json(const char *filename)
{
	char *path = savedir_path(filename);
	char *json = file_read(path, NULL);
	free(path);
	if (!json)
		return NULL;
	cJSON *root = cJSON_Parse(json);
	free(json);
	return root;
}

struct cjk_kanji_pair {
	uint16_t from;
	uint16_t to;
};

static const struct cjk_kanji_pair shinjitai_canon_map[] = {
	{ 0x4E21, 0x4E24 }, { 0x4E57, 0x4E58 }, { 0x4E80, 0x9F9F }, { 0x4E9C, 0x4E9A },
	{ 0x4EEE, 0x5047 }, { 0x4F1D, 0x4F20 }, { 0x4FA1, 0x4EF7 }, { 0x5075, 0x4FA6 },
	{ 0x507D, 0x4F2A }, { 0x5099, 0x5907 }, { 0x50B7, 0x4F24 }, { 0x50CD, 0x52A8 },
	{ 0x512A, 0x4F18 }, { 0x5150, 0x513F }, { 0x5186, 0x5706 }, { 0x51E6, 0x5904 },
	{ 0x5263, 0x5251 }, { 0x5264, 0x5242 }, { 0x5270, 0x5269 }, { 0x5275, 0x521B },
	{ 0x52B4, 0x52B3 }, { 0x52B9, 0x6548 }, { 0x52D5, 0x52A8 }, { 0x52D9, 0x52A1 },
	{ 0x52DD, 0x80DC }, { 0x52E2, 0x52BF }, { 0x52E7, 0x529D }, { 0x52F2, 0x52CB },
	{ 0x5354, 0x534F }, { 0x5358, 0x5355 }, { 0x53B3, 0x4E25 }, { 0x53CE, 0x6536 },
	{ 0x54E1, 0x5458 }, { 0x55B6, 0x8425 }, { 0x56E3, 0x56E2 }, { 0x56F2, 0x56F4 },
	{ 0x56F3, 0x56FE }, { 0x570F, 0x5708 }, { 0x5712, 0x56ED }, { 0x57F7, 0x6267 },
	{ 0x5805, 0x575A }, { 0x5831, 0x62A5 }, { 0x5834, 0x573A }, { 0x5841, 0x5792 },
	{ 0x584A, 0x5757 }, { 0x5857, 0x6D82 }, { 0x5869, 0x76D0 }, { 0x5897, 0x589E },
	{ 0x589C, 0x5760 }, { 0x58B3, 0x575F }, { 0x58CA, 0x574F }, { 0x58CC, 0x58E4 },
	{ 0x58F1, 0x58F9 }, { 0x58F2, 0x5356 }, { 0x5909, 0x53D8 }, { 0x5968, 0x5956 },
	{ 0x596A, 0x593A }, { 0x596E, 0x594B }, { 0x5A66, 0x5987 }, { 0x5B9F, 0x5B9E },
	{ 0x5BAE, 0x5BAB }, { 0x5BE9, 0x5BA1 }, { 0x5BFE, 0x5BF9 }, { 0x5C02, 0x4E13 },
	{ 0x5C0E, 0x5BFC }, { 0x5C64, 0x5C42 }, { 0x5CF6, 0x5C9B }, { 0x5DE3, 0x5DE2 },
	{ 0x5DFB, 0x5377 }, { 0x5E25, 0x5E05 }, { 0x5E2F, 0x5E26 }, { 0x5E30, 0x5F52 },
	{ 0x5E81, 0x5385 }, { 0x5E83, 0x5E7F }, { 0x5EC3, 0x5E9F }, { 0x5F10, 0x8D30 },
	{ 0x5F35, 0x5F20 }, { 0x5F37, 0x5F3A }, { 0x5F3E, 0x5F39 }, { 0x5F59, 0x6C47 },
	{ 0x5F6B, 0x96D5 }, { 0x5F8C, 0x540E }, { 0x5F93, 0x4ECE }, { 0x5FA9, 0x590D },
	{ 0x5FB3, 0x5FB7 }, { 0x5FB4, 0x5F81 }, { 0x5FB9, 0x5F7B }, { 0x5FDC, 0x5E94 },
	{ 0x6075, 0x60E0 }, { 0x60A9, 0x607C }, { 0x60AA, 0x6076 }, { 0x614B, 0x6001 },
	{ 0x6163, 0x60EF }, { 0x616E, 0x8651 }, { 0x6182, 0x5FE7 }, { 0x61A4, 0x6124 },
	{ 0x61B2, 0x5BAA }, { 0x61D0, 0x6000 }, { 0x61F2, 0x60E9 }, { 0x61F8, 0x60AC },
	{ 0x6226, 0x6218 }, { 0x622F, 0x620F }, { 0x6238, 0x6237 }, { 0x629C, 0x62D4 },
	{ 0x629E, 0x62E9 }, { 0x62DD, 0x62DC }, { 0x62E0, 0x636E }, { 0x62E1, 0x6269 },
	{ 0x6319, 0x4E3E }, { 0x633F, 0x63D2 }, { 0x635C, 0x641C }, { 0x6368, 0x820D },
	{ 0x6383, 0x626B }, { 0x63A1, 0x91C7 }, { 0x63B2, 0x63ED }, { 0x63DB, 0x6362 },
	{ 0x63FA, 0x6447 }, { 0x640D, 0x635F }, { 0x6442, 0x6444 }, { 0x6483, 0x51FB },
	{ 0x64EC, 0x62DF }, { 0x6557, 0x8D25 }, { 0x6575, 0x654C }, { 0x6589, 0x9F50 },
	{ 0x658E, 0x658B }, { 0x65E2, 0x5DF2 }, { 0x6642, 0x65F6 }, { 0x6681, 0x6653 },
	{ 0x66A6, 0x5386 }, { 0x66F8, 0x4E66 }, { 0x6804, 0x8363 }, { 0x685C, 0x6A31 },
	{ 0x685F, 0x6808 }, { 0x691C, 0x68C0 }, { 0x696D, 0x4E1A }, { 0x6975, 0x6781 },
	{ 0x697D, 0x4E50 }, { 0x69CB, 0x6784 }, { 0x69D8, 0x6837 }, { 0x6A19, 0x6807 },
	{ 0x6A29, 0x6743 }, { 0x6A4B, 0x6865 }, { 0x6A5F, 0x673A }, { 0x6B04, 0x680F },
	{ 0x6B53, 0x6B22 }, { 0x6B6F, 0x9F7F }, { 0x6B73, 0x5C81 }, { 0x6B74, 0x5386 },
	{ 0x6BB2, 0x6B7C }, { 0x6BBA, 0x6740 }, { 0x6BBB, 0x58F3 }, { 0x6BCE, 0x6BCF },
	{ 0x6C17, 0x6C14 }, { 0x6C37, 0x51B0 }, { 0x6C5A, 0x6C61 }, { 0x6C7A, 0x51B3 },
	{ 0x6D44, 0x51C0 }, { 0x6D5C, 0x6EE8 }, { 0x6D99, 0x6CEA }, { 0x6E07, 0x6E34 },
	{ 0x6E08, 0x6D4E }, { 0x6E09, 0x6D89 }, { 0x6E0B, 0x6DA9 }, { 0x6E13, 0x6EAA },
	{ 0x6E1B, 0x51CF }, { 0x6E26, 0x6DA1 }, { 0x6E2C, 0x6D4B }, { 0x6E80, 0x6EE1 },
	{ 0x6E96, 0x51C6 }, { 0x6EC5, 0x706D }, { 0x6F01, 0x6E14 }, { 0x6F22, 0x6C49 },
	{ 0x6F38, 0x6E10 }, { 0x6F64, 0x6DA6 }, { 0x6FC1, 0x6D4A }, { 0x6FC3, 0x6D53 },
	{ 0x6FEB, 0x6EE5 }, { 0x7015, 0x6FD2 }, { 0x702C, 0x6FD1 }, { 0x70BA, 0x4E3A },
	{ 0x7121, 0x65E0 }, { 0x713C, 0x70E7 }, { 0x7159, 0x70DF }, { 0x71B1, 0x70ED },
	{ 0x731F, 0x730E }, { 0x7363, 0x517D }, { 0x7372, 0x83B7 }, { 0x73FE, 0x73B0 },
	{ 0x7523, 0x4EA7 }, { 0x7570, 0x5F02 }, { 0x7573, 0x53E0 }, { 0x7642, 0x7597 },
	{ 0x7652, 0x6108 }, { 0x767A, 0x53D1 }, { 0x76E3, 0x76D1 }, { 0x770C, 0x53BF },
	{ 0x7815, 0x788E }, { 0x7832, 0x70AE }, { 0x78BA, 0x786E }, { 0x7A2E, 0x79CD },
	{ 0x7A32, 0x7A3B }, { 0x7A4D, 0x79EF }, { 0x7A4F, 0x7A33 }, { 0x7A6B, 0x83B7 },
	{ 0x7A93, 0x7A97 }, { 0x7ADC, 0x9F99 }, { 0x7AF6, 0x7ADE }, { 0x7B46, 0x7B14 },
	{ 0x7B87, 0x4E2A }, { 0x7BC0, 0x8282 }, { 0x7BC4, 0x8303 }, { 0x7BC9, 0x7B51 },
	{ 0x7C21, 0x7B80 }, { 0x7C8B, 0x7CB9 }, { 0x7CE7, 0x7CAE }, { 0x7CF8, 0x4E1D },
	{ 0x7D19, 0x7EB8 }, { 0x7D1A, 0x7EA7 }, { 0x7D4C, 0x7ECF }, { 0x7D75, 0x7ED8 },
	{ 0x7D99, 0x7EE7 }, { 0x7D9A, 0x7EED }, { 0x7DCF, 0x603B }, { 0x7DD1, 0x7EFF },
	{ 0x7DD2, 0x7EEA }, { 0x7DDA, 0x7EBF }, { 0x7DE8, 0x7F16 }, { 0x7E01, 0x7F18 },
	{ 0x7E04, 0x7EF3 }, { 0x7E26, 0x7EB5 }, { 0x7E2E, 0x7F29 }, { 0x7E3E, 0x7EE9 },
	{ 0x7E4A, 0x7EA4 }, { 0x7E54, 0x7EC7 }, { 0x7F70, 0x7F5A }, { 0x7F85, 0x7F57 },
	{ 0x7FA9, 0x4E49 }, { 0x8056, 0x5723 }, { 0x805E, 0x95FB }, { 0x8074, 0x542C },
	{ 0x8077, 0x804C }, { 0x8108, 0x8109 }, { 0x8133, 0x8111 }, { 0x81D3, 0x810F },
	{ 0x81E8, 0x4E34 }, { 0x820E, 0x820D }, { 0x8217, 0x94FA }, { 0x8266, 0x8230 },
	{ 0x8276, 0x8273 }, { 0x82B8, 0x827A }, { 0x8358, 0x5E84 }, { 0x83EF, 0x534E },
	{ 0x8449, 0x53F6 }, { 0x8535, 0x85CF }, { 0x85AB, 0x85B0 }, { 0x85AC, 0x836F },
	{ 0x865C, 0x864F }, { 0x86CD, 0x8424 }, { 0x8846, 0x4F17 }, { 0x885B, 0x536B },
	{ 0x885D, 0x51B2 }, { 0x88CF, 0x91CC }, { 0x88DC, 0x8865 }, { 0x88FD, 0x5236 },
	{ 0x8907, 0x590D }, { 0x8972, 0x88AD }, { 0x8996, 0x89C6 }, { 0x899A, 0x89C9 },
	{ 0x89A7, 0x89C8 }, { 0x89AA, 0x4EB2 }, { 0x89B3, 0x89C2 }, { 0x8A02, 0x8BA2 },
	{ 0x8A0E, 0x8BA8 }, { 0x8A13, 0x8BAD }, { 0x8A17, 0x6258 }, { 0x8A18, 0x8BB0 },
	{ 0x8A1F, 0x8BBC }, { 0x8A2A, 0x8BBF }, { 0x8A2D, 0x8BBE }, { 0x8A31, 0x8BB8 },
	{ 0x8A33, 0x8BD1 }, { 0x8A34, 0x8BC9 }, { 0x8A3A, 0x8BCA }, { 0x8A3C, 0x8BC1 },
	{ 0x8A50, 0x8BC8 }, { 0x8A55, 0x8BC4 }, { 0x8A5E, 0x8BCD }, { 0x8A60, 0x548F },
	{ 0x8A66, 0x8BD5 }, { 0x8A69, 0x8BD7 }, { 0x8A71, 0x8BDD }, { 0x8A72, 0x8BE5 },
	{ 0x8A73, 0x8BE6 }, { 0x8A87, 0x5938 }, { 0x8A8C, 0x5FD7 }, { 0x8A8D, 0x8BA4 },
	{ 0x8A95, 0x8BDE }, { 0x8A98, 0x8BF1 }, { 0x8A9E, 0x8BED }, { 0x8AA0, 0x8BDA },
	{ 0x8AA4, 0x8BEF }, { 0x8AAC, 0x8BF4 }, { 0x8AAD, 0x8BFB }, { 0x8AB0, 0x8C01 },
	{ 0x8AB2, 0x8BFE }, { 0x8ABF, 0x8C03 }, { 0x8AC7, 0x8C08 }, { 0x8ACB, 0x8BF7 },
	{ 0x8AD6, 0x8BBA }, { 0x8AF8, 0x8BF8 }, { 0x8AFE, 0x8BFA }, { 0x8B00, 0x8C0B },
	{ 0x8B01, 0x8C12 }, { 0x8B04, 0x8A8A }, { 0x8B0E, 0x8C1C }, { 0x8B19, 0x8C26 },
	{ 0x8B1B, 0x8BB2 }, { 0x8B1D, 0x8C22 }, { 0x8B21, 0x8C23 }, { 0x8B39, 0x8C28 },
	{ 0x8B58, 0x8BC6 }, { 0x8B5C, 0x8C31 }, { 0x8B70, 0x8BAE }, { 0x8B72, 0x8BA9 },
	{ 0x8B77, 0x62A4 }, { 0x8C4A, 0x4E30 }, { 0x8C9D, 0x8D1D }, { 0x8CA0, 0x8D1F },
	{ 0x8CA1, 0x8D22 }, { 0x8CA7, 0x8D2B }, { 0x8CA8, 0x8D27 }, { 0x8CA9, 0x8D29 },
	{ 0x8CAB, 0x8D2F }, { 0x8CAC, 0x8D23 }, { 0x8CAF, 0x8D2E }, { 0x8CB4, 0x8D35 },
	{ 0x8CB7, 0x4E70 }, { 0x8CB8, 0x8D37 }, { 0x8CBB, 0x8D39 }, { 0x8CBF, 0x8D38 },
	{ 0x8CC0, 0x8D3A }, { 0x8CC3, 0x8D41 }, { 0x8CC4, 0x8D3F }, { 0x8CC7, 0x8D44 },
	{ 0x8CCA, 0x8D3C }, { 0x8CD3, 0x5BBE }, { 0x8CDB, 0x8D5E }, { 0x8CDC, 0x8D50 },
	{ 0x8CDE, 0x8D4F }, { 0x8CE0, 0x8D54 }, { 0x8CE2, 0x8D24 }, { 0x8CE6, 0x8D4B },
	{ 0x8CEA, 0x8D28 }, { 0x8CED, 0x8D4C }, { 0x8CFC, 0x8D2D }, { 0x8D08, 0x8D60 },
	{ 0x8E8D, 0x8DC3 }, { 0x8ECA, 0x8F66 }, { 0x8ECC, 0x8F68 }, { 0x8ECD, 0x519B },
	{ 0x8ED2, 0x8F69 }, { 0x8EDF, 0x8F6F }, { 0x8EE2, 0x8F6C }, { 0x8EF8, 0x8F74 },
	{ 0x8EFD, 0x8F7B }, { 0x8F03, 0x8F83 }, { 0x8F09, 0x8F7D }, { 0x8F1D, 0x8F89 },
	{ 0x8F29, 0x8F88 }, { 0x8F2A, 0x8F6E }, { 0x8F38, 0x8F93 }, { 0x8F44, 0x8F96 },
	{ 0x8FB2, 0x519C }, { 0x8FBA, 0x8FB9 }, { 0x9023, 0x8FDE }, { 0x9032, 0x8FDB },
	{ 0x9045, 0x8FDF }, { 0x904B, 0x8FD0 }, { 0x904E, 0x8FC7 }, { 0x9054, 0x8FBE },
	{ 0x9055, 0x8FDD }, { 0x9060, 0x8FDC }, { 0x9069, 0x9002 }, { 0x9078, 0x9009 },
	{ 0x907A, 0x9057 }, { 0x9084, 0x8FD8 }, { 0x90F5, 0x90AE }, { 0x90F7, 0x4E61 },
	{ 0x9154, 0x9189 }, { 0x919C, 0x4E11 }, { 0x91B8, 0x917F }, { 0x91C8, 0x91CA },
	{ 0x91DD, 0x9488 }, { 0x91E3, 0x9493 }, { 0x920D, 0x949D }, { 0x9234, 0x94C3 },
	{ 0x9244, 0x94C1 }, { 0x925B, 0x94C5 }, { 0x9262, 0x94B5 }, { 0x9271, 0x77FF },
	{ 0x9280, 0x94F6 }, { 0x9283, 0x67AA }, { 0x9285, 0x94DC }, { 0x92AD, 0x94B1 },
	{ 0x92FC, 0x94A2 }, { 0x932C, 0x70BC }, { 0x932F, 0x9519 }, { 0x9332, 0x5F55 },
	{ 0x9396, 0x9501 }, { 0x93AE, 0x9547 }, { 0x93E1, 0x955C }, { 0x9418, 0x949F },
	{ 0x9451, 0x9274 }, { 0x9577, 0x957F }, { 0x9580, 0x95E8 }, { 0x9589, 0x95ED },
	{ 0x958B, 0x5F00 }, { 0x9591, 0x95F2 }, { 0x9593, 0x95F4 }, { 0x95A2, 0x5173 },
	{ 0x95A3, 0x9601 }, { 0x95A5, 0x9600 }, { 0x95B2, 0x9605 }, { 0x95D8, 0x6597 },
	{ 0x9665, 0x9677 }, { 0x9670, 0x9634 }, { 0x9673, 0x9648 }, { 0x9678, 0x9646 },
	{ 0x967A, 0x9669 }, { 0x967D, 0x9633 }, { 0x968A, 0x961F }, { 0x968E, 0x9636 },
	{ 0x969B, 0x9645 }, { 0x96A0, 0x9690 }, { 0x96A3, 0x90BB }, { 0x96BB, 0x53EA },
	{ 0x96D1, 0x6742 }, { 0x96E2, 0x79BB }, { 0x96E3, 0x96BE }, { 0x96F2, 0x4E91 },
	{ 0x96FB, 0x7535 }, { 0x970A, 0x7075 }, { 0x9727, 0x96FE }, { 0x97D3, 0x97E9 },
	{ 0x97FB, 0x97F5 }, { 0x97FF, 0x54CD }, { 0x9802, 0x9876 }, { 0x9803, 0x9877 },
	{ 0x9805, 0x9879 }, { 0x9806, 0x987A }, { 0x9810, 0x9884 }, { 0x9811, 0x987D },
	{ 0x9812, 0x9881 }, { 0x9818, 0x9886 }, { 0x982D, 0x5934 }, { 0x983B, 0x9891 },
	{ 0x983C, 0x8D56 }, { 0x984C, 0x9898 }, { 0x984D, 0x989D }, { 0x9854, 0x989C },
	{ 0x9855, 0x663E }, { 0x9858, 0x613F }, { 0x985E, 0x7C7B }, { 0x9867, 0x987E },
	{ 0x98A8, 0x98CE }, { 0x98DB, 0x98DE }, { 0x98E2, 0x9965 }, { 0x98EF, 0x996D },
	{ 0x98F2, 0x996E }, { 0x98FC, 0x9972 }, { 0x98FD, 0x9971 }, { 0x98FE, 0x9970 },
	{ 0x990A, 0x517B }, { 0x9928, 0x9986 }, { 0x99AC, 0x9A6C }, { 0x99C4, 0x9A6E },
	{ 0x99C5, 0x7AD9 }, { 0x99C6, 0x9A71 }, { 0x99D0, 0x9A7B }, { 0x9A0E, 0x9A91 },
	{ 0x9A12, 0x9A9A }, { 0x9A13, 0x9A8C }, { 0x9A5A, 0x60CA }, { 0x9ACF, 0x9AC5 },
	{ 0x9AD1, 0x9AB7 }, { 0x9AEA, 0x53D1 }, { 0x9B5A, 0x9C7C }, { 0x9BAE, 0x9C9C },
	{ 0x9BE8, 0x9CB8 }, { 0x9CE5, 0x9E1F }, { 0x9CF4, 0x9E23 }, { 0x9D8F, 0x9E21 },
	{ 0x9DB4, 0x9E64 }, { 0x9E97, 0x4E3D }, { 0x9EBA, 0x9762 }, { 0x9ED2, 0x9ED1 },
	{ 0x9ED9, 0x9ED8 }, { 0x9F62, 0x9F84 }, { 0x9F8D, 0x9F99 },
};

static bool cjk_is_ascii_only(const char *s)
{
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (*p >= 0x80)
			return false;
	}
	return true;
}

static uint16_t cjk_canon_bmp(uint16_t cp)
{
	size_t lo = 0;
	size_t hi = sizeof(shinjitai_canon_map) / sizeof(shinjitai_canon_map[0]);
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		uint16_t key = shinjitai_canon_map[mid].from;
		if (cp < key)
			hi = mid;
		else if (cp > key)
			lo = mid + 1;
		else
			return shinjitai_canon_map[mid].to;
	}
	return cp;
}

/* All mapped codepoints are BMP >= 0x4E00 (3 UTF-8 bytes), so in-place rewrite preserves byte length. */
static void canon_kanji_utf8_inplace(char *utf8)
{
	uint8_t *p = (uint8_t *)utf8;
	while (*p) {
		if (*p < 0x80) {
			p++;
		} else if ((*p & 0xe0) == 0xc0 && p[1]) {
			p += 2;
		} else if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
			uint16_t cp = (uint16_t)(((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f));
			uint16_t mapped = cjk_canon_bmp(cp);
			if (mapped != cp) {
				p[0] = (uint8_t)(0xe0 | (mapped >> 12));
				p[1] = (uint8_t)(0x80 | ((mapped >> 6) & 0x3f));
				p[2] = (uint8_t)(0x80 | (mapped & 0x3f));
			}
			p += 3;
		} else {
			p++;
		}
	}
}

static void decode_cjk_forms_buf(const char *s, bool canonicalize,
		char *sjis_buf, size_t sjis_cap, char **sjis_out,
		char *gbk_buf, size_t gbk_cap, char **gbk_out)
{
	bool ok_s = false, ok_g = false;
	size_t slen = strlen(s);
	*sjis_out = cjk2utf_strict_buf(s, slen, false, sjis_buf, sjis_cap, &ok_s);
	*gbk_out = cjk2utf_strict_buf(s, slen, true, gbk_buf, gbk_cap, &ok_g);
	if (!ok_s) {
		if (*sjis_out != sjis_buf)
			free(*sjis_out);
		*sjis_out = NULL;
	} else if (canonicalize) {
		canon_kanji_utf8_inplace(*sjis_out);
	}
	if (!ok_g) {
		if (*gbk_out != gbk_buf)
			free(*gbk_out);
		*gbk_out = NULL;
	} else if (canonicalize) {
		canon_kanji_utf8_inplace(*gbk_out);
	}
}

static bool save_cjk_match_impl(const char *a, const char *b, bool canonicalize)
{
	if (a == b)
		return true;
	if (!a || !b)
		return false;
	if (!strcmp(a, b))
		return true;
	if (cjk_is_ascii_only(a) && cjk_is_ascii_only(b))
		return false;

	char as_buf[512], ag_buf[512], bs_buf[512], bg_buf[512];
	char *as = NULL, *ag = NULL, *bs = NULL, *bg = NULL;
	decode_cjk_forms_buf(a, canonicalize, as_buf, sizeof(as_buf), &as, ag_buf, sizeof(ag_buf), &ag);
	decode_cjk_forms_buf(b, canonicalize, bs_buf, sizeof(bs_buf), &bs, bg_buf, sizeof(bg_buf), &bg);

	bool match = (as && bg && !strcmp(as, bg))
		|| (ag && bs && !strcmp(ag, bs))
		|| (as && bs && !strcmp(as, bs))
		|| (ag && bg && !strcmp(ag, bg));

	if (as != as_buf)
		free(as);
	if (ag != ag_buf)
		free(ag);
	if (bs != bs_buf)
		free(bs);
	if (bg != bg_buf)
		free(bg);
	return match;
}

bool save_names_equal(const char *a, const char *b)
{
	return save_cjk_match_impl(a, b, false);
}

bool save_symbols_equal(const char *a, const char *b)
{
	return save_cjk_match_impl(a, b, true);
}

void save_cjk_canon_forms_buf(const char *s, char *sjis_buf, size_t sjis_cap, char **sjis_out,
		char *gbk_buf, size_t gbk_cap, char **gbk_out)
{
	if (!s) {
		*sjis_out = NULL;
		*gbk_out = NULL;
		return;
	}
	decode_cjk_forms_buf(s, true, sjis_buf, sjis_cap, sjis_out, gbk_buf, gbk_cap, gbk_out);
}

void save_cjk_canon_forms(const char *s, char **sjis_out, char **gbk_out)
{
	save_cjk_canon_forms_buf(s, NULL, 0, sjis_out, NULL, 0, gbk_out);
}

bool save_symbol_prefix_equal(const char *full_sym, const char *prefix_buf, size_t prefix_len)
{
	if (!full_sym || !prefix_buf)
		return false;
	char *tmp = xmalloc(prefix_len + 1);
	memcpy(tmp, prefix_buf, prefix_len);
	tmp[prefix_len] = '\0';
	bool eq = save_symbols_equal(full_sym, tmp);
	free(tmp);
	return eq;
}

static uint16_t hankaku_to_zenkaku_u16(uint16_t cp)
{
	static const uint16_t map[0xa0 - 0x61] = {
		0x3002, 0x300C, 0x300D, 0x3001, 0x00B7, 0x30F2, 0x30A1, 0x30A3, // ｡｢｣､･ｦｧｨ
		0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30C3, 0x30FC, // ｩｪｫｬｭｮｯｰ
		0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD, 0x30AF, // ｱｲｳｴｵｶｷｸ
		0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD, 0x30BF, // ｹｺｻｼｽｾｿﾀ
		0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD, // ﾁﾂﾃﾄﾅﾆﾇﾈ
		0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE, 0x30DF, // ﾉﾊﾋﾌﾍﾎﾏﾐ
		0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9, 0x30EA, // ﾑﾒﾓﾔﾕﾖﾗﾘ
		0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3, 0x309B, 0x309C          // ﾙﾚﾛﾜﾝﾞﾟ
	};
	if (cp >= 0xff61 && cp <= 0xff9f)
		return map[cp - 0xff61];
	return 0;
}

static int encode_utf8_cp_with_fallback(uint32_t u, bool dst_is_gbk)
{
	if (u <= 0x7f)
		return (int)u;
	if (u < 65536) {
		int c = cjk_encode_codepoint((int)u, dst_is_gbk);
		if (c)
			return c;
	}
	if (dst_is_gbk) {
		uint16_t zen = hankaku_to_zenkaku_u16((uint16_t)u);
		if (zen) {
			int c = cjk_encode_codepoint(zen, true);
			if (c)
				return c;
		}
		if (u == 0x30fb)
			return cjk_encode_codepoint(0x00b7, true);
		if (u == 0x301c)
			return cjk_encode_codepoint(0xff5e, true);
		if (u == 0x2212)
			return cjk_encode_codepoint(0xff0d, true);
	} else {
		if (u == 0x00b7)
			return cjk_encode_codepoint(0x30fb, false);
	}
	if (u > 0x7f && u < 65536) {
		uint16_t canon = cjk_canon_bmp((uint16_t)u);
		if (canon != u) {
			int c = cjk_encode_codepoint(canon, dst_is_gbk);
			if (c)
				return c;
		}
		size_t n = sizeof(shinjitai_canon_map) / sizeof(shinjitai_canon_map[0]);
		for (size_t i = 0; i < n; i++) {
			if (shinjitai_canon_map[i].to == canon || shinjitai_canon_map[i].from == canon) {
				int c = cjk_encode_codepoint(shinjitai_canon_map[i].from, dst_is_gbk);
				if (c)
					return c;
				c = cjk_encode_codepoint(shinjitai_canon_map[i].to, dst_is_gbk);
				if (c)
					return c;
			}
		}
	}
	return '?';
}

static char *decode_cjk_lenient(const char *s, size_t len, bool is_gbk)
{
	bool ok = false;
	char *u = cjk2utf_strict_buf(s, len, is_gbk, NULL, 0, &ok);
	if (ok && u)
		return u;
	free(u);
	// Lenient fallback: step character-by-character so one stray byte never drops a whole string.
	char *tmp = xmalloc(len + 1);
	memcpy(tmp, s, len);
	tmp[len] = '\0';
	size_t cap = len * 3 + 4;
	uint8_t *dst = xmalloc(cap);
	uint8_t *dstp = dst;
	const uint8_t *p = (const uint8_t *)tmp;
	while (*p) {
		if ((size_t)(dstp - dst) + 4 >= cap) {
			size_t used = (size_t)(dstp - dst);
			cap *= 2;
			dst = xrealloc(dst, cap);
			dstp = dst + used;
		}
		char one[3] = { (char)p[0], 0, 0 };
		size_t step = 1;
		if (p[0] >= 0x80 && (is_gbk || !(p[0] >= 0xa1 && p[0] <= 0xdf)) && p[1]) {
			one[1] = (char)p[1];
			step = 2;
		}
		bool one_ok = false;
		char *cu = cjk2utf_strict(one, is_gbk, &one_ok);
		if (one_ok && cu) {
			size_t clen = strlen(cu);
			memcpy(dstp, cu, clen);
			dstp += clen;
		} else {
			*dstp++ = '?';
		}
		free(cu);
		p += step;
	}
	*dstp = '\0';
	free(tmp);
	return (char *)dst;
}

int save_text_gbk_bias(const char *s)
{
	if (!s || !s[0] || cjk_is_ascii_only(s))
		return 0;
	bool ok_s = false, ok_g = false;
	char *us = sys4_has_mai_word_table() ? mai_cjk2utf_strict(s, &ok_s) : cjk2utf_strict(s, false, &ok_s);
	char *ug = cjk2utf_strict(s, true, &ok_g);
	if (ok_s && (sys4_has_mai_word_table() || !ok_g)) {
		free(us);
		free(ug);
		return -100;
	}
	if (!ok_s && ok_g) {
		free(us);
		free(ug);
		return 100;
	}

	int bias = 0;
	if (us) {
		const uint8_t *p = (const uint8_t *)us;
		while (*p) {
			uint32_t cp = 0;
			if (*p < 0x80) {
				cp = *p++;
			} else if ((*p & 0xe0) == 0xc0 && p[1]) {
				cp = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
				p += 2;
			} else if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
				cp = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
				p += 3;
			} else {
				p++;
			}
			if ((cp >= 0x3000 && cp <= 0x30ff) || (cp >= 0xff01 && cp <= 0xff5e))
				bias -= 12;
			else if (cp >= 0xff61 && cp <= 0xff9f)
				bias += 8;
		}
	}
	if (ug) {
		const uint8_t *p = (const uint8_t *)ug;
		while (*p) {
			uint32_t cp = 0;
			if (*p < 0x80) {
				cp = *p++;
			} else if ((*p & 0xe0) == 0xc0 && p[1]) {
				cp = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
				p += 2;
			} else if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
				cp = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
				p += 3;
			} else {
				p++;
			}
			if ((cp >= 0x3000 && cp <= 0x30ff) || (cp >= 0xff01 && cp <= 0xff5e))
				bias += 12;
		}
	}
	for (const uint8_t *r = (const uint8_t *)s; *r;) {
		if (*r < 0x80) {
			r++;
		} else if (r[0] >= 0xb0 && r[0] <= 0xf7 && r[1] >= 0xa1 && r[1] <= 0xfe) {
			bias += 4;
			r += 2;
		} else if (r[1]) {
			r += 2;
		} else {
			break;
		}
	}
	free(us);
	free(ug);
	return bias;
}

static char *encode_utf8_to_cjk_fallback(const char *utf8, bool dst_is_gbk, size_t *out_len)
{
	size_t cap = strlen(utf8) * 2 + 4;
	uint8_t *dst = xmalloc(cap);
	uint8_t *dstp = dst;
	const uint8_t *p = (const uint8_t *)utf8;
	while (*p) {
		if (*p <= 0x7f) {
			*dstp++ = *p++;
			continue;
		}
		uint32_t u = '?';
		if ((*p & 0xe0) == 0xc0 && p[1]) {
			u = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
			p += 2;
		} else if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
			u = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
			p += 3;
		} else if ((*p & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
			p += 4;
		} else {
			p++;
		}
		int c = encode_utf8_cp_with_fallback(u, dst_is_gbk);
		if (c > 0xff) {
			*dstp++ = (uint8_t)(c >> 8);
			*dstp++ = (uint8_t)(c & 0xff);
		} else {
			*dstp++ = (uint8_t)(c ? c : '?');
		}
	}
	*dstp = '\0';
	if (out_len)
		*out_len = (size_t)(dstp - dst);
	return (char *)dst;
}

static char *copy_ascii_or_same_cstr(const char *s, size_t len, bool src_is_gbk,
		bool dst_is_gbk, size_t *out_len, bool *handled_out)
{
	*handled_out = false;
	bool ascii = true;
	for (size_t i = 0; i < len; i++) {
		if ((uint8_t)s[i] >= 0x80) {
			ascii = false;
			break;
		}
	}
	if (ascii || src_is_gbk == dst_is_gbk || len == 0) {
		char *copy = xmalloc(len + 1);
		memcpy(copy, s, len);
		copy[len] = '\0';
		if (out_len)
			*out_len = len;
		*handled_out = true;
		return copy;
	}
	return NULL;
}

char *save_transcode_cstr(const char *s, size_t len, bool src_is_gbk, bool dst_is_gbk, size_t *out_len)
{
	if (!s) {
		if (out_len)
			*out_len = 0;
		return xstrdup("");
	}
	bool handled = false;
	char *fast = copy_ascii_or_same_cstr(s, len, src_is_gbk, dst_is_gbk, out_len, &handled);
	if (handled)
		return fast;

	char *utf8 = decode_cjk_lenient(s, len, src_is_gbk);
	char *res = encode_utf8_to_cjk_fallback(utf8, dst_is_gbk, out_len);
	free(utf8);
	return res;
}

/*
 * Like save_transcode_cstr, but fails fast (returns NULL) when strict SJIS/GBK
 * decoding encounters an invalid byte sequence instead of leniently retrying per character.
 */
char *save_transcode_cstr_strict(const char *s, size_t len, bool src_is_gbk, bool dst_is_gbk, size_t *out_len)
{
	if (!s) {
		if (out_len)
			*out_len = 0;
		return NULL;
	}
	bool handled = false;
	char *fast = copy_ascii_or_same_cstr(s, len, src_is_gbk, dst_is_gbk, out_len, &handled);
	if (handled)
		return fast;

	char utf_stack[1536];
	bool ok = false;
	char *utf8 = cjk2utf_strict_buf(s, len, src_is_gbk, utf_stack, sizeof(utf_stack), &ok);
	if (!ok || !utf8) {
		if (utf8 != utf_stack)
			free(utf8);
		if (out_len)
			*out_len = 0;
		return NULL;
	}
	char *res = encode_utf8_to_cjk_fallback(utf8, dst_is_gbk, out_len);
	if (utf8 != utf_stack)
		free(utf8);
	return res;
}

static struct string *save_decode_vm_string_to(const char *s, size_t len, bool src_is_gbk, bool dst_is_gbk)
{
	if (!s || len == 0)
		return make_string("", 0);
	size_t out_len = 0;
	char *conv = save_transcode_cstr(s, len, src_is_gbk, dst_is_gbk, &out_len);
	struct string *str = make_string(conv, out_len);
	free(conv);
	return str;
}

struct string *save_decode_vm_string(const char *s, size_t len, bool src_is_gbk)
{
	return save_decode_vm_string_to(s, len, src_is_gbk, sys4_text_is_gbk);
}

static size_t utf8_to_canon_u32(const char *utf8, uint32_t *out, size_t max_len)
{
	size_t n = 0;
	const uint8_t *p = (const uint8_t *)utf8;
	while (*p && n < max_len) {
		uint32_t cp;
		if (*p < 0x80) {
			cp = *p++;
		} else if ((*p & 0xe0) == 0xc0 && p[1]) {
			cp = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
			p += 2;
		} else if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
			uint16_t bmp = (uint16_t)(((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f));
			cp = cjk_canon_bmp(bmp);
			p += 3;
		} else if ((*p & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
			cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
			p += 4;
		} else {
			cp = *p++;
		}
		out[n++] = cp;
	}
	return n;
}

static char *decode_best_utf8(const char *s, bool prefer_gbk)
{
	bool ok = false;
	char *u = cjk2utf_strict(s, prefer_gbk, &ok);
	if (ok && u)
		return u;
	free(u);
	u = cjk2utf_strict(s, !prefer_gbk, &ok);
	if (ok && u)
		return u;
	free(u);
	bool prev = sys4_text_is_gbk;
	sys4_set_text_gbk(prefer_gbk);
	u = sjis2utf(s ? s : "", 0);
	sys4_set_text_gbk(prev);
	return u;
}

int save_symbol_similarity(const char *a, const char *b)
{
	if (!a || !b)
		return 0;
	char *au = decode_best_utf8(a, sys4_text_is_gbk);
	char *bu = decode_best_utf8(b, !sys4_text_is_gbk);
	uint32_t ua[256], ub[256];
	size_t na = utf8_to_canon_u32(au, ua, 256);
	size_t nb = utf8_to_canon_u32(bu, ub, 256);
	free(au);
	free(bu);

	size_t lcp = 0;
	while (lcp < na && lcp < nb && ua[lcp] == ub[lcp])
		lcp++;
	size_t lcs = 0;
	while (lcs < na - lcp && lcs < nb - lcp && ua[na - 1 - lcs] == ub[nb - 1 - lcs])
		lcs++;

	int shared = 0;
	for (size_t i = lcp; i < na - lcs; i++) {
		for (size_t j = lcp; j < nb - lcs; j++) {
			if (ua[i] == ub[j]) {
				shared++;
				break;
			}
		}
	}
	int unmatched = (int)((na - lcp - lcs) + (nb - lcp - lcs));
	return (int)((lcp + lcs) * 100) + shared * 15 - unmatched * 10;
}

static int find_named_entry_compat(const void *base, size_t stride, int count, size_t name_off, const char *name)
{
	const char *p = (const char *)base;
	for (int i = 0; i < count; i++, p += stride) {
		const char *entry_name = *(const char * const *)(p + name_off);
		if (entry_name && save_symbols_equal(entry_name, name))
			return i;
	}
	return -1;
}

int save_find_struct(const char *name)
{
	if (!name || !name[0])
		return -1;
	int id = ain_get_struct(ain, (char *)name);
	if (id >= 0)
		return id;
	return find_named_entry_compat(ain->structures, sizeof(*ain->structures), ain->nr_structures,
		offsetof(struct ain_struct, name), name);
}

int save_find_enum(const char *name)
{
	if (!name || !name[0])
		return -1;
	int id = ain_get_enum(ain, (char *)name);
	if (id >= 0)
		return id;
	return find_named_entry_compat(ain->enums, sizeof(*ain->enums), ain->nr_enums,
		offsetof(struct ain_enum, name), name);
}

int save_find_delegate(const char *name)
{
	if (!name || !name[0])
		return -1;
	int id = ain_get_delegate(ain, (char *)name);
	if (id >= 0)
		return id;
	return find_named_entry_compat(ain->delegates, sizeof(*ain->delegates), ain->nr_delegates,
		offsetof(struct ain_function_type, name), name);
}

int save_find_functype(const char *name)
{
	if (!name || !name[0])
		return -1;
	int id = ain_get_functype(ain, (char *)name);
	if (id >= 0)
		return id;
	return find_named_entry_compat(ain->function_types, sizeof(*ain->function_types), ain->nr_function_types,
		offsetof(struct ain_function_type, name), name);
}

static int get_group_index(const char *name)
{
	for (int i = 0; i < ain->nr_global_groups; i++) {
		if (!strcmp(ain->global_group_names[i], name))
			return i;
	}
	for (int i = 0; i < ain->nr_global_groups; i++) {
		if (save_symbols_equal(ain->global_group_names[i], name))
			return i;
	}
	return -1;
}

static int32_t add_value_to_gsave(enum ain_data_type type, union vm_value val, struct gsave *save);

static bool nested_array(struct page *page)
{
	return page->a_type == AIN_ARRAY ? page->array.element_type.data == AIN_ARRAY : page->array.rank > 1;
}

static int page_array_rank(struct page *page)
{
	if (page->a_type != AIN_ARRAY) return page->array.rank;
	int rank = 1;
	for (const struct ain_type *t = &page->array.element_type; t && t->data == AIN_ARRAY; t = t->array_type)
		rank++;
	return rank;
}

static enum ain_data_type page_array_leaf_type(struct page *page)
{
	if (!page)
		return AIN_INT;
	if (page->a_type != AIN_ARRAY)
		return array_type(page->a_type);
	const struct ain_type *t = &page->array.element_type;
	while (t && t->data == AIN_ARRAY)
		t = t->array_type;
	return t ? t->data : AIN_INT;
}

static bool is_gsave_heap_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_ARRAY:
	case AIN_ARRAY_TYPE:
		return true;
	default:
		return false;
	}
}

static struct gsave_flat_array *collect_flat_arrays(struct page *page, struct gsave_flat_array *fa,
		struct gsave_flat_array *fa_end, int dim, const int32_t *dims,
		enum ain_data_type leaf_type, struct gsave *save)
{
	if (dim > 0) {
		int count = dims[dim];
		for (int i = 0; i < count; i++) {
			struct page *child = (page && page->type == ARRAY_PAGE && i < page->nr_vars)
				? heap_get_page(page->values[i].i) : NULL;
			fa = collect_flat_arrays(child, fa, fa_end, dim - 1, dims, leaf_type, save);
		}
		return fa;
	}
	if (fa >= fa_end)
		return fa;
	int count = dims[0];
	enum ain_data_type type = (page && page->type == ARRAY_PAGE && page->nr_vars > 0)
		? variable_type(page, 0, NULL, NULL) : leaf_type;
	fa->nr_values = count;
	fa->type = type;
	fa->values = xcalloc(count, sizeof(struct gsave_array_value));
	for (int i = 0; i < count; i++) {
		bool has_elem = (page && page->type == ARRAY_PAGE && i < page->nr_vars);
		union vm_value elem = has_elem ? page->values[i] : (union vm_value){ .i = is_gsave_heap_type(type) ? -1 : 0 };
		fa->values[i].type = type;
		fa->values[i].value = add_value_to_gsave(type, elem, save);
	}
	return fa + 1;
}

static int32_t add_empty_struct_to_gsave(struct gsave *save)
{
	int32_t sidx = 0;
	if (save->version >= 7) {
		sidx = gsave_get_struct_def(save, "");
		if (sidx < 0) {
			struct ain_struct dummy = { .name = "" };
			sidx = gsave_add_struct_def(save, &dummy);
		}
	}
	struct gsave_record rec = {
		.type = GSAVE_RECORD_STRUCT,
		.struct_name = xstrdup(""),
		.struct_index = sidx,
		.nr_indices = 0,
		.indices = NULL,
	};
	return gsave_add_record(save, &rec);
}

static int32_t add_value_to_gsave(enum ain_data_type type, union vm_value val, struct gsave *save)
{
	switch (type) {
	case AIN_VOID:
	case AIN_INT:
	case AIN_BOOL:
	case AIN_FUNC_TYPE:
	case AIN_LONG_INT:
	case AIN_FLOAT:
	case AIN_ENUM:
	case AIN_ENUM2:
		return val.i;
	case AIN_DELEGATE:
		// Group saves contain persistent data; the original v12 engine clears events.
		return ain->version >= 11 ? 0 : val.i;
	case AIN_STRING:
		return gsave_add_string(save, string_index_valid(val.i) ? heap[val.i].s : &EMPTY_STRING);
	case AIN_STRUCT:
		{
			struct page *page = heap_get_page(val.i);
			if (!page || page->type != STRUCT_PAGE || page->index < 0 || page->index >= ain->nr_structures)
				return add_empty_struct_to_gsave(save);
			struct ain_struct *st = &ain->structures[page->index];
			int nr_members = st->nr_members;
			struct gsave_record rec = {
				.type = GSAVE_RECORD_STRUCT,
				.struct_name = xstrdup(st->name),
				.nr_indices = nr_members,
				.indices = xcalloc(nr_members, sizeof(int32_t)),
			};
			if (save->version >= 7) {
				rec.struct_index = gsave_get_struct_def(save, st->name);
				if (rec.struct_index < 0)
					rec.struct_index = gsave_add_struct_def(save, st);
			}
			for (int i = 0; i < nr_members; i++) {
				enum ain_data_type ftype = st->members[i].type.data;
				const char *field_name = st->members[i].name2 ? st->members[i].name2 : st->members[i].name;
				int32_t value = (field_name && field_name[0] && i < page->nr_vars)
					? add_value_to_gsave(ftype, page->values[i], save) : 0;
				struct gsave_keyval kv = {
					.name = xstrdup(field_name ? field_name : ""),
					.type = ftype,
					.value = value,
				};
				rec.indices[i] = gsave_add_keyval(save, &kv);
			}
			return gsave_add_record(save, &rec);
		}
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY:
		{
			struct page *page = heap_get_page(val.i);
			if (!page || page->type != ARRAY_PAGE) {
				struct gsave_array array = { .rank = -1 };
				return gsave_add_array(save, &array);
			}
			int rank = page_array_rank(page);
			if (rank <= 0 || rank > 16) {
				struct gsave_array array = { .rank = -1 };
				return gsave_add_array(save, &array);
			}
			struct gsave_array array = {
				.rank = rank,
				.dimensions = xcalloc(rank, sizeof(int32_t)),
				.nr_flat_arrays = 1
			};
			struct page *p = page;
			for (int dim = rank - 1; dim >= 0; dim--) {
				int count = (p && p->type == ARRAY_PAGE && p->nr_vars >= 0) ? p->nr_vars : 0;
				array.dimensions[dim] = count;
				if (dim) {
					if (__builtin_mul_overflow(array.nr_flat_arrays, count, &array.nr_flat_arrays)) {
						free(array.dimensions);
						struct gsave_array empty_arr = { .rank = -1 };
						return gsave_add_array(save, &empty_arr);
					}
					struct page *next_p = NULL;
					if (p && p->type == ARRAY_PAGE) {
						for (int j = 0; j < count; j++) {
							next_p = heap_get_page(p->values[j].i);
							if (next_p && next_p->type == ARRAY_PAGE)
								break;
						}
					}
					p = next_p;
				}
			}
			array.flat_arrays = xcalloc(array.nr_flat_arrays, sizeof(struct gsave_flat_array));
			collect_flat_arrays(page, array.flat_arrays, array.flat_arrays + array.nr_flat_arrays,
				rank - 1, array.dimensions, page_array_leaf_type(page), save);
			return gsave_add_array(save, &array);
		}
	case AIN_REF_TYPE:
		return -1;
	default:
		WARNING("Unhandled type: %s", ain_strtype(ain, type, -1));
		return -1;
	}
}

static int get_gsave_version(void)
{
	if (ain->version >= 11) return 8;
	if (AIN_VERSION_GTE(ain, 6, 0)) {
		return config.vm_name ? 5 : 7;
	}
	return AIN_VERSION_GTE(ain, 5, 0) ? 5 : 4;
}

int save_globals(const char *keyname, const char *filename, const char *group_name, int *n_out)
{
	int group = -1;
	int nr_vars = 0;
	if (group_name) {
		if ((group = get_group_index(group_name)) < 0) {
			WARNING("Unregistered global group: %s", display_sjis0(group_name));
		} else {
			for (int i = 0; i < ain->nr_globals; i++) {
				if (ain->globals[i].name2 && !ain->globals[i].name2[0])
					continue;
				if (ain->globals[i].group_index == group)
					nr_vars++;
			}
		}
	} else {
		for (int i = 0; i < ain->nr_globals; i++) {
			if (!(ain->globals[i].name2 && !ain->globals[i].name2[0]))
				nr_vars++;
		}
	}

	char *path = savedir_path(filename);
	int version = get_gsave_version();
	int32_t preserved_nr_comments = 0;
	struct string **preserved_comments = NULL;
	if (version >= 7 && file_exists(path)) {
		enum savefile_error prev_err;
		struct gsave *prev = gsave_read(path, &prev_err);
		if (prev) {
			bool same_key = (keyname && prev->key)
				? save_names_equal(keyname, prev->key)
				: ((!keyname || !keyname[0]) && (!prev->key || !prev->key[0]));
			const char *new_grp = group_name ? group_name : "";
			const char *old_grp = prev->group ? prev->group : "";
			bool same_group = save_names_equal(new_grp, old_grp);
			if (same_key && same_group && prev->nr_comments > 0 && prev->comments) {
				bool has_comment = false;
				for (int32_t i = 0; i < prev->nr_comments; i++) {
					if (prev->comments[i] && prev->comments[i]->size > 0) {
						has_comment = true;
						break;
					}
				}
				if (has_comment) {
					if (version < 8)
						version = 8;
					preserved_nr_comments = prev->nr_comments;
					preserved_comments = xcalloc(preserved_nr_comments, sizeof(struct string *));
					for (int32_t i = 0; i < preserved_nr_comments; i++) {
						preserved_comments[i] = prev->comments[i]
							? string_dup(prev->comments[i])
							: string_ref(&EMPTY_STRING);
					}
				}
			}
			gsave_free(prev);
		}
	}

	struct gsave *save = gsave_create(version, keyname, ain->nr_globals, group_name);
	if (preserved_nr_comments > 0) {
		save->nr_comments = preserved_nr_comments;
		save->comments = preserved_comments;
	}
	gsave_add_globals_record(save, nr_vars);

	if (nr_vars > 0) {
		struct gsave_global *global = save->globals;
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].name2 && !ain->globals[i].name2[0])
				continue;
			if (group >= 0 && ain->globals[i].group_index != group)
				continue;
			const char *gname = (ain->globals[i].name2 && ain->globals[i].name2[0]) ? ain->globals[i].name2 : ain->globals[i].name;
			global->name = xstrdup(gname);
			global->type = ain->globals[i].type.data;
			global->value = add_value_to_gsave(global->type, global_get(i), save);
			global++;
		}
	}

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(path), strerror(errno));
		free(path);
		gsave_free(save);
		return 0;
	}

	bool encrypt = !AIN_VERSION_GTE(ain, 6, 0);
	int compression_level = AIN_VERSION_GTE(ain, 6, 0) ? 1 : 9;
	enum savefile_error error = gsave_write(save, fp, encrypt, compression_level);
	gsave_free(save);
	if (error != SAVEFILE_SUCCESS) {
		WARNING("Failed to write save file: %s", savefile_strerror(error));
		atomic_save_abort(fp, tmp_path);
		free(path);
		return 0;
	}
	bool ok = atomic_save_commit(fp, path, tmp_path, is_critical_global_save(filename));
	free(path);
	if (ok) {
		track_global_save(keyname, filename, group_name);
		if (n_out)
			*n_out = nr_vars;
	}
	return ok ? 1 : 0;
}

static union vm_value json_to_vm_value(enum ain_data_type type, enum ain_data_type struct_type, int array_rank, cJSON *json);

void get_array_dims(cJSON *json, int rank, union vm_value *dims)
{
	cJSON *array = json;
	for (int i = 0; i < rank; i++) {
		if (!array || !cJSON_IsArray(array)) {
			dims[i].i = 0;
			array = NULL;
			continue;
		}
		dims[i].i = cJSON_GetArraySize(array);
		array = cJSON_GetArrayItem(array, 0);
	}
}

void json_load_page(struct page *page, cJSON *vars, bool call_dtors)
{
	if (!page || !vars)
		return;
	int i = 0;
	cJSON *v;
	cJSON_ArrayForEach(v, vars) {
		if (i >= page->nr_vars)
			break;
		int struct_type, array_rank;
		enum ain_data_type data_type = variable_type(page, i, &struct_type, &array_rank);
		union vm_value val = json_to_vm_value(data_type, struct_type, array_rank, v);
		if (call_dtors)
			variable_set(page, i, data_type, val);
		else
			page->values[i] = val;
		i++;
	}
}

static union vm_value json_to_vm_value(enum ain_data_type type, enum ain_data_type struct_type, int array_rank, cJSON *json)
{
	char *str;
	int slot;
	union vm_value *dims;
	struct page *page;
	switch (type) {
	case AIN_INT:
	case AIN_BOOL:
	case AIN_LONG_INT:
		if (!cJSON_IsNumber(json)) {
			invalid_save_data("Not a number", json);
			return vm_int(0);
		}
		return vm_int(json->valueint);
	case AIN_FLOAT:
		if (!cJSON_IsNumber(json)) {
			invalid_save_data("Not a number", json);
			return vm_float(0);
		}
		return vm_float(json->valuedouble);
	case AIN_STRING:
		slot = heap_alloc_slot(VM_STRING);
		if (!cJSON_IsString(json)) {
			invalid_save_data("Not a string", json);
			heap[slot].s = string_ref(&EMPTY_STRING);
			return vm_int(slot);
		}
		str = cJSON_GetStringValue(json);
		if (!str || !str[0]) {
			heap[slot].s = string_ref(&EMPTY_STRING);
		} else {
			heap[slot].s = make_string(str, strlen(str));
		}
		return vm_int(slot);
	case AIN_STRUCT:
		if ((int)struct_type < 0 || (int)struct_type >= ain->nr_structures) {
			invalid_save_data("Invalid struct type", json);
			return vm_int(-1);
		}
		slot = alloc_struct(struct_type);
		if (!cJSON_IsArray(json) || cJSON_GetArraySize(json) != ain->structures[struct_type].nr_members) {
			invalid_save_data("Not an array", json);
		} else {
			json_load_page(heap[slot].page, json, false);
		}
		return vm_int(slot);
	case AIN_ARRAY_TYPE:
		slot = heap_alloc_slot(VM_PAGE);
		if (cJSON_IsNull(json)) {
			heap[slot].page = NULL;
			return vm_int(slot);
		}
		if (!cJSON_IsArray(json) || array_rank <= 0 || array_rank > 16) {
			invalid_save_data("Not an array", json);
			heap[slot].page = NULL;
			return vm_int(slot);
		}
		dims = xmalloc(sizeof(union vm_value) * array_rank);
		get_array_dims(json, array_rank, dims);
		page = alloc_array(array_rank, dims, type, struct_type, false);
		heap[slot].page = page;
		free(dims);
		if (page)
			json_load_page(page, json, false);
		return vm_int(slot);
	case AIN_REF_TYPE:
		return vm_int(-1);
	default:
		WARNING("Unhandled data type: %s", ain_strtype(ain, type, -1));
		return vm_int(-1);
	}
}

static cJSON *read_save_file(const char *path)
{
	FILE *f;
	long len;
	char *buf;

	if (!(f = file_open_utf8(path, "rb"))) {
		WARNING("Failed to open save file: %s: %s", display_utf0(path), strerror(errno));
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || len > 256L * 1024L * 1024L || fseek(f, 0, SEEK_SET) != 0) {
		WARNING("Failed to determine save file size: %s", display_utf0(path));
		fclose(f);
		return NULL;
	}

	buf = xmalloc((size_t)len + 1);
	buf[len] = '\0';
	if (len > 0 && fread(buf, (size_t)len, 1, f) != 1) {
		WARNING("Failed to read save file: %s", display_utf0(path));
		fclose(f);
		free(buf);
		return NULL;
	}
	fclose(f);

	cJSON *r = cJSON_Parse(buf);
	free(buf);
	return r;
}

static int load_globals_from_json(const char *path, const char *keyname, const char *group_name, int *n)
{
	int retval = 0;
	cJSON *save = read_save_file(path);
	if (!save)
		return 0;
	if (!cJSON_IsObject(save)) {
		invalid_save_data("Not an object", save);
		goto cleanup;
	}

	cJSON *key = cJSON_GetObjectItem(save, "key");
	if (!key || !save_names_equal(keyname, cJSON_GetStringValue(key)))
		VM_ERROR("Attempted to load save data with wrong key: %s", display_sjis0(keyname));

	if (group_name) {
		// TODO?
	}

	cJSON *globals = cJSON_GetObjectItem(save, "globals");
	if (!globals || !cJSON_IsArray(globals)) {
		invalid_save_data("Not an array", globals);
		goto cleanup;
	}

	cJSON *g;
	cJSON_ArrayForEach(g, globals) {
		cJSON *index, *value;
		if (!(index = cJSON_GetObjectItem(g, "index"))) {
			invalid_save_data("Missing index", g);
			goto cleanup;
		}
		if (!(value = cJSON_GetObjectItem(g, "value"))) {
			invalid_save_data("Missing value", g);
			goto cleanup;
		}
		if (!cJSON_IsNumber(index)) {
			invalid_save_data("Not a number", index);
			goto cleanup;
		}

		int i = index->valueint;
		if (i < 0 || i >= ain->nr_globals) {
			invalid_save_data("Invalid global index", index);
			goto cleanup;
		}
		current_global = i;

		if (should_preserve_static_global(i)) {
			if (n)
				(*n)++;
			continue;
		}
		bool call_dtors = false; // Destructors for old objects are not called.
		global_set(i, json_to_vm_value(ain->globals[i].type.data, ain->globals[i].type.struc, ain->globals[i].type.rank, value), call_dtors);
		if (n)
			(*n)++;
	}

	retval = 1;
cleanup:
	cJSON_Delete(save);
	return retval;
}

static int struct_member_index(struct ain_struct *st, const char *name)
{
	if (!name || !name[0])
		return -1;
	for (int i = 0; i < st->nr_members; i++) {
		if (st->members[i].name2 && st->members[i].name2[0] && !strcmp(st->members[i].name2, name))
			return i;
		if (!strcmp(st->members[i].name, name))
			return i;
	}
	for (int i = 0; i < st->nr_members; i++) {
		if (st->members[i].name2 && st->members[i].name2[0] && save_symbols_equal(st->members[i].name2, name))
			return i;
		if (st->members[i].name && save_symbols_equal(st->members[i].name, name))
			return i;
	}
	return -1;
}

static bool gsave_struct_matches(const struct ain_struct *st, const char *struct_name,
		const struct gsave_record *rec, const struct gsave *save,
		const struct gsave_struct_def *sd)
{
	if (save_symbols_equal(struct_name, st->name))
		return true;
	if (!rec || rec->nr_indices != st->nr_members || st->nr_members <= 0)
		return false;
	if (sd && sd->nr_fields < rec->nr_indices)
		return false;
	for (int i = 0; i < rec->nr_indices; i++) {
		if (rec->indices[i] < 0 || rec->indices[i] >= save->nr_keyvals)
			return false;
		const struct gsave_keyval *kv = &save->keyvals[rec->indices[i]];
		const char *field_name = sd ? sd->fields[i].name : kv->name;
		enum ain_data_type field_type = sd ? sd->fields[i].type : kv->type;
		if (!field_name || !field_name[0]) {
			if (st->members[i].name && st->members[i].name[0])
				return false;
			continue;
		}
		if (st->members[i].type.data != field_type)
			return false;
	}
	return true;
}

static bool ain_has_function_name(const char *name)
{
	if (!ain || !name || !name[0])
		return false;
	for (int i = 0; i < ain->nr_functions; i++) {
		const char *fn = ain->functions[i].name;
		if (fn && fn[0] == name[0] && !strcmp(fn, name))
			return true;
	}
	return false;
}

struct gsave_str_ctx {
	bool src_is_gbk;
	bool dst_is_gbk;
	bool raw_lookup_keys;
};

static struct string *save_decode_raw_lookup_key(const char *s, size_t len,
		bool default_src_is_gbk, bool dst_is_gbk)
{
	if (!s || len == 0)
		return make_string("", 0);
	if (cjk_is_ascii_only(s))
		return make_string(s, len);

	// 1. Exact native AIN function name (Recall / EventSkip keys): never rewrite.
	if (ain_has_function_name(s))
		return make_string(s, len);

	// 2. Opposite-encoding candidate verified against AIN function table.
	size_t cand_len = 0;
	char *cand = save_transcode_cstr_strict(s, len, !dst_is_gbk, dst_is_gbk, &cand_len);
	if (cand && ain_has_function_name(cand)) {
		struct string *str = make_string(cand, cand_len);
		free(cand);
		return str;
	}

	// 3. Per-string encoding classification for non-function keys (e.g. CG keys
	//    from .ex) so a mixed-encoding save never corrupts already-native keys
	//    when opposite-encoding entries outnumber them.
	int bias = save_text_gbk_bias(s);
	bool convert = false;
	if (cand) {
		if (!dst_is_gbk) {
			// Target CP932: convert only keys that clearly look GBK; keep
			// native CP932 and unverified ambiguous bytes untouched.
			convert = (bias > 0);
		} else {
			// Target GBK: convert keys that look CP932 (or neutral in a CP932 save);
			// never corrupt keys that already look GBK.
			convert = (bias < 0) || (bias == 0 && !default_src_is_gbk);
		}
	}
	if (!convert) {
		free(cand);
		return make_string(s, len);
	}
	struct string *str = make_string(cand, cand_len);
	free(cand);
	return str;
}

static struct string *save_decode_gsave_string(const char *s, size_t len, const struct gsave_str_ctx *ctx)
{
	if (ctx->raw_lookup_keys)
		return save_decode_raw_lookup_key(s, len, ctx->src_is_gbk, ctx->dst_is_gbk);
	return save_decode_vm_string_to(s, len, ctx->src_is_gbk, ctx->dst_is_gbk);
}

static union vm_value gsave_to_vm_value(struct gsave *save, enum ain_data_type type, int struct_type, int array_rank, int32_t value, const struct gsave_str_ctx *ctx);
static union vm_value gsave_to_vm_value_typed(struct gsave *save, const struct ain_type *type, int32_t value, const struct gsave_str_ctx *ctx);

static struct gsave_flat_array *gsave_load_array(struct gsave *save, struct page *page,
		struct gsave_flat_array *flat_array, struct gsave_flat_array *flat_end,
		const struct gsave_str_ctx *ctx)
{
	if (!page || page->type != ARRAY_PAGE)
		VM_ERROR("Bad save file: invalid array page");
	if (nested_array(page)) {
		for (int i = 0; i < page->nr_vars; i++)
			flat_array = gsave_load_array(save, heap_get_page(page->values[i].i), flat_array, flat_end, ctx);
		return flat_array;
	}

	if (!flat_array || flat_array >= flat_end)
		VM_ERROR("Bad save file: truncated flat array table");
	if (flat_array->nr_values < 0 || page->nr_vars != flat_array->nr_values)
		VM_ERROR("Bad save file: unexpected number of array elements");
	for (int i = 0; i < page->nr_vars; i++) {
		int struct_type, array_rank;
		enum ain_data_type data_type = variable_type(page, i, &struct_type, &array_rank);
		if (data_type != flat_array->values[i].type &&
		    !(data_type == AIN_REF_STRUCT && flat_array->values[i].type == AIN_STRUCT))
			VM_ERROR("Bad save file: unexpected array element type");
		const struct ain_type *type = variable_full_type(page, i);
		union vm_value val = type ? gsave_to_vm_value_typed(save, type, flat_array->values[i].value, ctx)
			: gsave_to_vm_value(save, data_type, struct_type, array_rank, flat_array->values[i].value, ctx);
		variable_fini(page->values[i], data_type, false);
		page->values[i] = val;
	}
	return flat_array + 1;
}

static union vm_value gsave_to_vm_value(struct gsave *save, enum ain_data_type type, int struct_type, int array_rank, int32_t value, const struct gsave_str_ctx *ctx)
{
	switch (type) {
	case AIN_VOID:
	case AIN_INT:
	case AIN_BOOL:
	case AIN_LONG_INT:
	case AIN_FLOAT:
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_FUNC_TYPE:
		return vm_int(value);
	case AIN_DELEGATE:
		return variable_initval(AIN_DELEGATE);
	case AIN_STRING:
		{
			int slot = heap_alloc_slot(VM_STRING);
			if (value == GSAVE7_EMPTY_STRING) {
				heap[slot].s = string_ref(&EMPTY_STRING);
			} else {
				if (value < 0 || value >= save->nr_strings || !save->strings[value]) {
					heap[slot].s = string_ref(&EMPTY_STRING);
					VM_ERROR("Bad save file: invalid string index %d", value);
				}
				if (!save->strings[value]->size) {
					heap[slot].s = string_ref(&EMPTY_STRING);
				} else {
					heap[slot].s = save_decode_gsave_string(save->strings[value]->text,
						save->strings[value]->size, ctx);
				}
			}
			return vm_int(slot);
		}
	case AIN_STRUCT:
		{
			if (value < 0 || value >= save->nr_records)
				VM_ERROR("Bad save file: invalid record index %d", value);
			if (struct_type < 0 || struct_type >= ain->nr_structures)
				VM_ERROR("Bad save file: invalid struct type %d", struct_type);
			struct gsave_record *rec = &save->records[value];
			struct ain_struct *st = &ain->structures[struct_type];
			struct gsave_struct_def *sd = NULL;
			if (save->version >= 7) {
				if (rec->struct_index < 0 || rec->struct_index >= save->nr_struct_defs)
					VM_ERROR("Bad save file: invalid struct_def index %d", rec->struct_index);
				sd = &save->struct_defs[rec->struct_index];
				if (sd->nr_fields < rec->nr_indices)
					VM_ERROR("Bad save file: struct_def field count mismatch");
			}
			const char *struct_name = sd ? sd->name : rec->struct_name;
			if (!gsave_struct_matches(st, struct_name, rec, save, sd))
				VM_ERROR("Bad save file: structure name mismatch");
			int slot = alloc_struct(struct_type);
			struct page *page = heap[slot].page;
			for (int i = 0; i < rec->nr_indices; i++) {
				if (rec->indices[i] < 0 || rec->indices[i] >= save->nr_keyvals)
					VM_ERROR("Bad save file: invalid keyval index %d", rec->indices[i]);
				struct gsave_keyval *kv = &save->keyvals[rec->indices[i]];
				const char *field_name = sd ? sd->fields[i].name : kv->name;
				enum ain_data_type field_type = sd ? sd->fields[i].type : kv->type;
				if (!field_name || !field_name[0])
					continue;
				int index = struct_member_index(st, field_name);
				if (index < 0 && i < st->nr_members && rec->nr_indices == st->nr_members
						&& st->members[i].name && st->members[i].name[0]) {
					int cand_struct_type, cand_array_rank;
					if (variable_type(page, i, &cand_struct_type, &cand_array_rank) == field_type)
						index = i;
				}
				if (index < 0) {
					WARNING("structure %s has no member named %s", display_sjis0(struct_name), display_sjis1(field_name));
					continue;
				}
				int m_struct_type, m_array_rank;
				enum ain_data_type data_type = variable_type(page, index, &m_struct_type, &m_array_rank);
				if (data_type != field_type)
					VM_ERROR("Bad save file: structure member type mismatch");
				union vm_value val = gsave_to_vm_value_typed(save, &st->members[index].type, kv->value, ctx);
				variable_fini(page->values[index], data_type, false);
				page->values[index] = val;
			}
			return vm_int(slot);
		}
	case AIN_ARRAY_TYPE:
		{
			if (value < 0 || value >= save->nr_arrays)
				VM_ERROR("Bad save file: invalid array index %d", value);
			struct gsave_array *array = &save->arrays[value];
			int slot = heap_alloc_slot(VM_PAGE);
			if (array->rank < 0) {
				heap[slot].page = NULL;
				return vm_int(slot);
			}
			if (array_rank <= 0 || array_rank != array->rank)
				VM_ERROR("Bad save file: array rank mismatch");
			union vm_value *dims = xmalloc(sizeof(union vm_value) * array_rank);
			for (int i = 0; i < array_rank; i++) {
				int32_t d = array->dimensions[array_rank - 1 - i];
				if (d < 0 || d > 16 * 1024 * 1024) {
					free(dims);
					VM_ERROR("Bad save file: invalid array dimension %d", d);
				}
				dims[i].i = d;
			}
			struct page *page = alloc_array(array_rank, dims, type, struct_type, false);
			heap[slot].page = page;
			free(dims);
			struct gsave_flat_array *flat_end = array->flat_arrays
				? array->flat_arrays + array->nr_flat_arrays : NULL;
			gsave_load_array(save, page, array->flat_arrays, flat_end, ctx);
			return vm_int(slot);
		}
	case AIN_REF_TYPE:
		return vm_int(-1);
	default:
		WARNING("Unhandled data type: %s", ain_strtype(ain, type, -1));
		return vm_int(-1);
	}
}

static struct page *v12_saved_array_new(const struct ain_type *type, int rank, int32_t *dimensions)
{
	if (type->data != AIN_ARRAY || rank < 1 || rank > 16 || !type->array_type)
		VM_ERROR("Bad save file: nested array type mismatch");
	if (dimensions[rank - 1] < 0 || dimensions[rank - 1] > 16 * 1024 * 1024)
		VM_ERROR("Bad save file: invalid array dimension %d", dimensions[rank - 1]);
	struct page *page = v12_array_new(type->array_type, dimensions[rank - 1], false);
	if (rank > 1) {
		for (int i = 0; i < page->nr_vars; i++) {
			int slot = page->values[i].i;
			struct page *child = v12_saved_array_new(type->array_type, rank - 1, dimensions);
			delete_page(slot);
			heap_set_page(slot, child);
		}
	}
	return page;
}

static union vm_value gsave_to_vm_value_typed(struct gsave *save, const struct ain_type *type, int32_t value, const struct gsave_str_ctx *ctx)
{
	if (type->data != AIN_ARRAY)
		return gsave_to_vm_value(save, type->data, type->struc, type->rank, value, ctx);
	if (value < 0 || value >= save->nr_arrays)
		VM_ERROR("Bad save file: invalid generic array index %d", value);
	struct gsave_array *array = &save->arrays[value];
	if (array->rank < 1) return variable_initval_typed(type);
	struct page *page = v12_saved_array_new(type, array->rank, array->dimensions);
	struct gsave_flat_array *flat_end = array->flat_arrays
		? array->flat_arrays + array->nr_flat_arrays : NULL;
	gsave_load_array(save, page, array->flat_arrays, flat_end, ctx);
	return vm_int(heap_alloc_page(page));
}

static int *resolve_gsave_globals(struct gsave *save, const char *group_name)
{
	if (!save || save->nr_globals <= 0)
		return NULL;
	int *resolved = xmalloc(sizeof(int) * (size_t)save->nr_globals);
	for (int i = 0; i < save->nr_globals; i++)
		resolved[i] = -1;
	bool *claimed = xcalloc((size_t)(ain->nr_globals > 0 ? ain->nr_globals : 1), sizeof(bool));

	// Grouped saves pack only globals matching group_name in .ain order; ungrouped
	// saves either omit suppressed (blank name2) entries or dump all ain->globals.
	int target_group = (group_name && group_name[0]) ? get_group_index(group_name) : -1;
	int *expected = NULL;
	int nr_expected = 0;
	if (ain->nr_globals > 0 && (!group_name || !group_name[0] || target_group >= 0)) {
		expected = xmalloc(sizeof(int) * (size_t)ain->nr_globals);
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].name2 && !ain->globals[i].name2[0])
				continue;
			if (target_group >= 0 && ain->globals[i].group_index != target_group)
				continue;
			expected[nr_expected++] = i;
		}
		if (target_group < 0 && save->nr_globals == ain->nr_globals && nr_expected != ain->nr_globals) {
			for (int i = 0; i < ain->nr_globals; i++)
				expected[i] = i;
			nr_expected = ain->nr_globals;
		}
	}

	int aligned = 0;
	int shifted = 0;

	// Pass 1: exact name & name2 lookup
	for (int gi = 0; gi < save->nr_globals; gi++) {
		struct gsave_global *g = &save->globals[gi];
		if (g->type == AIN_VOID && !g->name[0])
			continue;
		int idx = ain_get_global(ain, g->name);
		if (idx < 0) {
			for (int i = 0; i < ain->nr_globals; i++) {
				if (ain->globals[i].name2 && ain->globals[i].name2[0] && !strcmp(ain->globals[i].name2, g->name)) {
					idx = i;
					break;
				}
			}
		}
		if (idx >= 0 && idx < ain->nr_globals) {
			resolved[gi] = idx;
			claimed[idx] = true;
			int exp_idx = (expected && gi < nr_expected) ? expected[gi] : -1;
			if (idx == exp_idx)
				aligned++;
			else
				shifted++;
		}
	}

	// Pass 2: cross-encoding UTF-8 + Kanji-normalized lookup
	for (int gi = 0; gi < save->nr_globals; gi++) {
		struct gsave_global *g = &save->globals[gi];
		if (resolved[gi] >= 0 || (g->type == AIN_VOID && !g->name[0]))
			continue;
		int idx = -1;
		int exp_idx = (expected && gi < nr_expected) ? expected[gi] : -1;
		if (exp_idx >= 0 && !claimed[exp_idx] && ain->globals[exp_idx].type.data == g->type) {
			if ((ain->globals[exp_idx].name2 && ain->globals[exp_idx].name2[0] && save_symbols_equal(ain->globals[exp_idx].name2, g->name))
					|| (ain->globals[exp_idx].name && save_symbols_equal(ain->globals[exp_idx].name, g->name)))
				idx = exp_idx;
		}
		if (idx < 0) {
			for (int pass_type = 0; pass_type < 2 && idx < 0; pass_type++) {
				for (int i = 0; i < ain->nr_globals; i++) {
					if (claimed[i])
						continue;
					if (pass_type == 0 && ain->globals[i].type.data != g->type)
						continue;
					if ((ain->globals[i].name2 && ain->globals[i].name2[0] && save_symbols_equal(ain->globals[i].name2, g->name))
							|| (ain->globals[i].name && save_symbols_equal(ain->globals[i].name, g->name))) {
						idx = i;
						break;
					}
				}
			}
		}
		if (idx >= 0) {
			resolved[gi] = idx;
			claimed[idx] = true;
			if (idx == exp_idx)
				aligned++;
			else
				shifted++;
		}
	}

	// Pass 3: aligned structural fallback when localized multi-char terms differ
	if (expected && save->nr_globals <= nr_expected && shifted == 0
			&& (aligned > 0 || (save->nr_globals == 1 && nr_expected == 1))) {
		for (int gi = 0; gi < save->nr_globals; gi++) {
			struct gsave_global *g = &save->globals[gi];
			if (resolved[gi] >= 0 || (g->type == AIN_VOID && !g->name[0]))
				continue;
			int exp_idx = gi < nr_expected ? expected[gi] : -1;
			if (exp_idx >= 0 && !claimed[exp_idx] && ain->globals[exp_idx].name && ain->globals[exp_idx].name[0]
					&& ain->globals[exp_idx].type.data == g->type
					&& (target_group < 0 || ain->globals[exp_idx].group_index == target_group)) {
				resolved[gi] = exp_idx;
				claimed[exp_idx] = true;
			}
		}
	}

	free(expected);
	free(claimed);
	return resolved;
}

static bool gsave_is_raw_lookup_extra_group(const char *keyname, const char *group_name)
{
	if (!keyname || !group_name)
		return false;
	if (game_maman && !strcmp(keyname, "MAMAN_EXTRA") && !strcmp(group_name, "MAMAN_EXTRA_DATA"))
		return true;
	if (game_haharanman
			&& ((!strcmp(keyname, "MIBOUJIN_EXTRA") && !strcmp(group_name, "MIBOUJIN_EXTRA_DATA"))
				|| (!strcmp(keyname, "MIBOUJIN_USER") && !strcmp(group_name, "MIBOUJIN_USER_DATA"))))
		return true;
	if (game_escalayer_reboot
			&& ((!strcmp(keyname, "ESCA_OMAKE") && !strcmp(group_name, "ESCA_OMAKE_DATA"))
				|| (!strcmp(keyname, "ESCA_USER") && !strcmp(group_name, "ESCA_USER_DATA"))))
		return true;
	if (game_pascha3
			&& ((!strcmp(keyname, "PC3_OMAKE") && !strcmp(group_name, "PC3_OMAKE_DATA"))
				|| (!strcmp(keyname, "PC3_USER") && !strcmp(group_name, "PC3_USER_DATA"))))
		return true;
	if (game_night_remaster
			&& !strcmp(keyname, "seEfa0ea?d8sdvaAekfe")
			&& !strcmp(group_name, "SaveGroup_MemModeInfo"))
		return true;
	if (game_rance01
			&& save_names_equal(keyname, "\x89\xbc\x83\x4c\x81\x5b\x96\xbc")
			&& save_names_equal(group_name, "\x8b\xa4\x97\x4c\x83\x66\x81\x5b\x83\x5e"))
		return true;
	if (game_haruka
			&& save_names_equal(keyname, "\x92\xb4\x8d\x56\x91\x4d\x94\x45\x83\x5a\x81\x5b\x83\x75\x83\x4c\x81\x5b")
			&& !strcmp(group_name, "SomeFlag"))
		return true;
	return false;
}

static bool gsave_target_is_gbk(const char *keyname, const char *group_name)
{
	if (!sys4_text_is_gbk)
		return false;
	if (!gsave_is_raw_lookup_extra_group(keyname, group_name))
		return true;
	if (sys4_source_is_utf8)
		return true;
	// Partial CN releases of Heartful Maman / Haha Ranman keep CP932 lookup keys
	// in .ain/.ex identifiers, while fully-GBK builds transcode identifiers too.
	int bias = 0;
	int sampled = 0;
	for (int i = 0; ain && i < ain->nr_global_groups && sampled < 16; i++) {
		const char *g = ain->global_group_names[i];
		if (g && !cjk_is_ascii_only(g)) {
			bias += save_text_gbk_bias(g);
			sampled++;
		}
	}
	for (int i = 0; ain && i < ain->nr_functions && sampled < 64; i++) {
		const char *fn = ain->functions[i].name;
		if (fn && !cjk_is_ascii_only(fn)) {
			bias += save_text_gbk_bias(fn);
			sampled++;
		}
	}
	return bias > 15;
}

static bool gsave_detect_is_gbk(const struct gsave *save, const int *resolved,
		const char *keyname, const char *group_name, bool default_is_gbk)
{
	int native_votes = 0;
	int opp_votes = 0;
	if (keyname && save->key && !cjk_is_ascii_only(save->key)) {
		if (!strcmp(keyname, save->key))
			native_votes += 20;
		else if (save_names_equal(keyname, save->key))
			opp_votes += 20;
	}
	if (group_name && group_name[0] && save->group && !cjk_is_ascii_only(save->group)) {
		if (!strcmp(group_name, save->group))
			native_votes += 20;
		else if (save_names_equal(group_name, save->group))
			opp_votes += 20;
	}
	for (int gi = 0; gi < save->nr_globals && resolved; gi++) {
		int idx = resolved[gi];
		const char *gname = save->globals[gi].name;
		if (idx < 0 || idx >= ain->nr_globals || !gname || cjk_is_ascii_only(gname))
			continue;
		const char *aname = (ain->globals[idx].name2 && ain->globals[idx].name2[0])
			? ain->globals[idx].name2 : ain->globals[idx].name;
		if (!strcmp(aname, gname))
			native_votes += 10;
		else
			opp_votes += 10;
	}
	if (opp_votes > native_votes)
		return !default_is_gbk;
	if (native_votes > opp_votes)
		return default_is_gbk;
	int bias = 0;
	int sampled = 0;
	for (int i = 0; i < save->nr_strings && sampled < 64; i++) {
		if (save->strings[i] && save->strings[i]->size > 0
				&& !cjk_is_ascii_only(save->strings[i]->text)) {
			bias += save_text_gbk_bias(save->strings[i]->text);
			sampled++;
		}
	}
	if (bias > 15)
		return true;
	if (bias < -15)
		return false;
	return default_is_gbk;
}

int load_globals_from_gsave(struct gsave *save, const char *keyname, const char *group_name, int *n)
{
	if (!save_names_equal(keyname, save->key))
		VM_ERROR("Attempted to load save data with wrong key: %s", display_sjis0(keyname));
	if (!group_name)
		group_name = "";
	if (!save->group)
		save->group = xstrdup("");
	if (!save_names_equal(group_name, save->group))
		VM_ERROR("Attempted to load save data with wrong group name: '%s'", display_sjis0(group_name));

	int *resolved = resolve_gsave_globals(save, group_name);
	bool dst_is_gbk = gsave_target_is_gbk(keyname, group_name);
	struct gsave_str_ctx str_ctx = {
		.src_is_gbk = gsave_detect_is_gbk(save, resolved, keyname, group_name, dst_is_gbk),
		.dst_is_gbk = dst_is_gbk,
		.raw_lookup_keys = gsave_is_raw_lookup_extra_group(keyname, group_name),
	};
	int retval = 1;
	for (int gi = 0; gi < save->nr_globals; gi++) {
		struct gsave_global *g = &save->globals[gi];
		if (g->type == AIN_VOID && !g->name[0]) continue;
		int global_index = resolved ? resolved[gi] : -1;
		if (global_index < 0) {
			WARNING("invalid global name %s", display_sjis0(g->name));
			retval = 0;
			goto cleanup;
		}
		struct ain_type *type = &ain->globals[global_index].type;
		if (type->data != g->type) {
			WARNING("%s: type mismatch", display_sjis0(g->name));
			retval = 0;
			goto cleanup;
		}
		if (should_preserve_static_global(global_index)) {
			if (n)
				(*n)++;
			continue;
		}
		bool call_dtors = false; // Destructors for old objects should not be called.
		global_set(global_index, gsave_to_vm_value_typed(save, type, g->value, &str_ctx), call_dtors);
		if (n)
			(*n)++;
	}

cleanup:
	free(resolved);
	return retval;
}

static char *make_comment_sidecar_path(const char *base);

int load_globals(const char *keyname, const char *filename, const char *group_name, int *n)
{
	if (n)
		*n = 0;
	char *path = savedir_path(filename);
	int retval;

	bool is_cfg = is_game_config_save(filename);
	bool primary_valid = false;

	// First, try reading as a gsave.
	enum savefile_error error;
	struct gsave *save = gsave_read(path, &error);
	if (error == SAVEFILE_SUCCESS) {
		primary_valid = true;
		retval = load_globals_from_gsave(save, keyname, group_name, n);
		gsave_free(save);
		if (retval) {
			free(path);
			track_global_save(keyname, filename, group_name);
			if (is_cfg)
				modifier_on_config_loaded();
			modifier_on_globals_loaded();
			return retval;
		}
		if (n)
			*n = 0;
	} else if (error == SAVEFILE_INVALID_SIGNATURE) {
		// If not a System4 save file, try reading as a json.
		retval = load_globals_from_json(path, keyname, group_name, n);
		if (retval) {
			free(path);
			track_global_save(keyname, filename, group_name);
			if (is_cfg)
				modifier_on_config_loaded();
			modifier_on_globals_loaded();
			return retval;
		}
		if (n)
			*n = 0;
	} else {
		WARNING("%s: %s", display_utf0(path), savefile_strerror(error));
	}

	// Fall back to .bak if the primary save failed to parse and a backup exists.
	char *bak_path = make_backup_path(path);
	bool had_any_file = file_exists(path) || file_exists(bak_path);
	if (!primary_valid && file_exists(bak_path)) {
		enum savefile_error bak_err;
		struct gsave *bak_save = gsave_read(bak_path, &bak_err);
		if (bak_err == SAVEFILE_SUCCESS) {
			retval = load_globals_from_gsave(bak_save, keyname, group_name, n);
			gsave_free(bak_save);
			if (retval) {
				WARNING("Recovered corrupted save %s from backup %s",
						display_utf0(path), display_utf0(bak_path));
				file_copy(bak_path, path);
				free(bak_path);
				free(path);
				track_global_save(keyname, filename, group_name);
				if (is_cfg)
					modifier_on_config_loaded();
				modifier_on_globals_loaded();
				return retval;
			}
			if (n)
				*n = 0;
		}
	}
	free(bak_path);
	free(path);
	if (!had_any_file)
		track_global_save(keyname, filename, group_name);
	if (is_cfg)
		modifier_on_config_loaded();
	return 0;
}

int delete_save_file(const char *filename)
{
	untrack_global_save(filename);
	char *path = savedir_path(filename);
	char *bak_path = make_backup_path(path);
	if (file_exists(bak_path))
		remove_utf8(bak_path);
	free(bak_path);
	char *comment_path = make_comment_sidecar_path(path);
	if (file_exists(comment_path))
		remove_utf8(comment_path);
	free(comment_path);
	if (!file_exists(path)) {
		free(path);
		return 0;
	}
	if (remove_utf8(path)) {
		WARNING("remove(\"%s\"): %s", display_utf0(path), strerror(errno));
		free(path);
		return 0;
	}
	free(path);
	return 1;
}

static void (*background_flush_hook)(void) = NULL;

void savedata_set_background_flush_hook(void (*hook)(void))
{
	background_flush_hook = hook;
}

void savedata_flush_on_background(void)
{
	if (flushing_background_saves)
		return;
	flushing_background_saves = true;
	if (background_flush_hook)
		background_flush_hook();
	if (ain && heap && heap_size > 0) {
		for (int i = 0; i < nr_tracked_global_saves; i++) {
			const struct tracked_global_save *entry = &tracked_global_saves[i];
			if (!entry->keyname || !entry->filename || !entry->group_name)
				continue;
			if (get_group_index(entry->group_name) < 0)
				continue;
			save_globals(entry->keyname, entry->filename, entry->group_name, NULL);
		}
	}
	flushing_background_saves = false;
}

/* =========================================================================
 * AIN v14 SerializeStruct / DeserializeStruct & Comment Container (X4SS v1)
 * ========================================================================= */

static const uint8_t X4SS_MAGIC[8] = { 'X', '4', 'S', 'S', 1, 0, 0, 0 };

enum x4ss_obj_kind {
	X4SS_OBJ_STRUCT = 1,
	X4SS_OBJ_ARRAY  = 2,
	X4SS_OBJ_NULL   = 3,
};

enum x4ss_val_tag {
	X4SS_VAL_NONE     = 0,
	X4SS_VAL_SCALAR   = 1,
	X4SS_VAL_STRING   = 2,
	X4SS_VAL_OBJ      = 3,
	X4SS_VAL_IFACE    = 4,
	X4SS_VAL_OPTION   = 5,
	X4SS_VAL_PAGE_VAR = 6,
};

struct x4ss_buf {
	uint8_t *data;
	size_t len;
	size_t cap;
};

static void x4ss_buf_ensure(struct x4ss_buf *b, size_t extra)
{
	if (extra > SIZE_MAX - b->len)
		ERROR("x4ss buffer size overflow");
	size_t needed = b->len + extra;
	if (needed <= b->cap)
		return;
	size_t new_cap = b->cap ? b->cap : 4096;
	while (new_cap < needed) {
		if (new_cap > SIZE_MAX / 2) {
			new_cap = needed;
			break;
		}
		new_cap *= 2;
	}
	b->data = xrealloc(b->data, new_cap);
	b->cap = new_cap;
}

static void x4ss_put_bytes(struct x4ss_buf *b, const void *src, size_t n)
{
	if (!n)
		return;
	x4ss_buf_ensure(b, n);
	memcpy(b->data + b->len, src, n);
	b->len += n;
}

static void x4ss_put_u8(struct x4ss_buf *b, uint8_t v)
{
	x4ss_put_bytes(b, &v, 1);
}

static void x4ss_put_u32(struct x4ss_buf *b, uint32_t v)
{
	uint8_t le[4] = {
		(uint8_t)(v & 0xff),
		(uint8_t)((v >> 8) & 0xff),
		(uint8_t)((v >> 16) & 0xff),
		(uint8_t)((v >> 24) & 0xff),
	};
	x4ss_put_bytes(b, le, 4);
}

static void x4ss_put_i32(struct x4ss_buf *b, int32_t v)
{
	x4ss_put_u32(b, (uint32_t)v);
}

static void x4ss_put_cstr(struct x4ss_buf *b, const char *s)
{
	uint32_t n = s ? (uint32_t)strlen(s) : 0;
	x4ss_put_u32(b, n);
	if (n)
		x4ss_put_bytes(b, s, n);
}

struct x4ss_reader {
	const uint8_t *data;
	size_t len;
	size_t pos;
	bool error;
};

static bool x4ss_get_bytes(struct x4ss_reader *r, void *dst, size_t n)
{
	if (r->error || n > r->len - r->pos) {
		r->error = true;
		return false;
	}
	if (n && dst)
		memcpy(dst, r->data + r->pos, n);
	r->pos += n;
	return true;
}

static uint8_t x4ss_get_u8(struct x4ss_reader *r)
{
	uint8_t v = 0;
	x4ss_get_bytes(r, &v, 1);
	return v;
}

static uint32_t x4ss_get_u32(struct x4ss_reader *r)
{
	uint8_t le[4] = {0};
	if (!x4ss_get_bytes(r, le, 4))
		return 0;
	return (uint32_t)le[0] | ((uint32_t)le[1] << 8)
		| ((uint32_t)le[2] << 16) | ((uint32_t)le[3] << 24);
}

static int32_t x4ss_get_i32(struct x4ss_reader *r)
{
	return (int32_t)x4ss_get_u32(r);
}

static char *x4ss_get_cstr(struct x4ss_reader *r, uint32_t *out_len)
{
	uint32_t n = x4ss_get_u32(r);
	if (r->error || n > 64u * 1024u * 1024u || n > r->len - r->pos) {
		r->error = true;
		if (out_len)
			*out_len = 0;
		return NULL;
	}
	char *s = xmalloc((size_t)n + 1);
	if (n)
		memcpy(s, r->data + r->pos, n);
	s[n] = '\0';
	r->pos += n;
	if (out_len)
		*out_len = n;
	return s;
}

/* Interned ain_type pool for nested array element types reconstructed on load */
struct x4ss_intern_type {
	struct ain_type type;
	struct x4ss_intern_type *next;
};
static struct x4ss_intern_type *x4ss_intern_pool = NULL;

static struct ain_type *x4ss_intern_ain_type(const struct ain_type *src)
{
	for (struct x4ss_intern_type *it = x4ss_intern_pool; it; it = it->next) {
		if (it->type.data == src->data &&
		    it->type.struc == src->struc &&
		    it->type.rank == src->rank &&
		    it->type.array_type == src->array_type) {
			return &it->type;
		}
	}
	struct x4ss_intern_type *node = xcalloc(1, sizeof(*node));
	node->type = *src;
	node->next = x4ss_intern_pool;
	x4ss_intern_pool = node;
	return &node->type;
}

static const char *x4ss_type_symbol_name(enum ain_data_type data, int id)
{
	if (id < 0)
		return "";
	switch (data) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		return id < ain->nr_structures ? ain->structures[id].name : "";
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_REF_ENUM:
		return id < ain->nr_enums ? ain->enums[id].name : "";
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		return id < ain->nr_delegates ? ain->delegates[id].name : "";
	case AIN_FUNC_TYPE:
	case AIN_REF_FUNC_TYPE:
		return id < ain->nr_function_types ? ain->function_types[id].name : "";
	default:
		return "";
	}
}

static int x4ss_resolve_type_symbol(enum ain_data_type data, const char *name, int fallback_id)
{
	if (!name || !name[0])
		return fallback_id;
	int id = -1;
	switch (data) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		id = save_find_struct(name);
		return id >= 0 ? id : fallback_id;
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_REF_ENUM:
		id = save_find_enum(name);
		return id >= 0 ? id : fallback_id;
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		id = save_find_delegate(name);
		return id >= 0 ? id : fallback_id;
	default:
		return fallback_id;
	}
}

static void x4ss_write_ain_type(struct x4ss_buf *b, const struct ain_type *t)
{
	if (!t) {
		x4ss_put_i32(b, AIN_VOID);
		x4ss_put_i32(b, -1);
		x4ss_put_i32(b, 0);
		x4ss_put_cstr(b, "");
		x4ss_put_u8(b, 0);
		return;
	}
	x4ss_put_i32(b, (int32_t)t->data);
	x4ss_put_i32(b, (int32_t)t->struc);
	x4ss_put_i32(b, (int32_t)t->rank);
	x4ss_put_cstr(b, x4ss_type_symbol_name(t->data, t->struc));
	if (t->array_type) {
		x4ss_put_u8(b, 1);
		x4ss_write_ain_type(b, t->array_type);
	} else {
		x4ss_put_u8(b, 0);
	}
}

static struct ain_type x4ss_read_ain_type_depth(struct x4ss_reader *r, int depth)
{
	struct ain_type t = {0};
	if (depth > 16) {
		r->error = true;
		return t;
	}
	t.data = (enum ain_data_type)x4ss_get_i32(r);
	int fallback_id = x4ss_get_i32(r);
	t.rank = x4ss_get_i32(r);
	char *sym = x4ss_get_cstr(r, NULL);
	t.struc = x4ss_resolve_type_symbol(t.data, sym, fallback_id);
	free(sym);
	uint8_t has_inner = x4ss_get_u8(r);
	if (has_inner && !r->error) {
		struct ain_type inner = x4ss_read_ain_type_depth(r, depth + 1);
		t.array_type = x4ss_intern_ain_type(&inner);
	} else {
		t.array_type = NULL;
	}
	return t;
}

static struct ain_type x4ss_read_ain_type(struct x4ss_reader *r)
{
	return x4ss_read_ain_type_depth(r, 0);
}

struct x4ss_save_ctx {
	int *slot_to_id;
	int *id_to_slot;
	int nr_objs;
	int cap_objs;
};

static uint32_t x4ss_register_page(struct x4ss_save_ctx *ctx, int slot)
{
	if (slot < 0 || !page_index_valid(slot))
		return 0;
	struct page *p = heap[slot].page;
	if (p && p->type != STRUCT_PAGE && p->type != ARRAY_PAGE)
		return 0;
	if (ctx->slot_to_id[slot] > 0)
		return (uint32_t)ctx->slot_to_id[slot];
	uint32_t id = (uint32_t)(++ctx->nr_objs);
	if (ctx->nr_objs >= ctx->cap_objs) {
		ctx->cap_objs = ctx->cap_objs ? ctx->cap_objs * 2 : 256;
		ctx->id_to_slot = xrealloc(ctx->id_to_slot, (size_t)ctx->cap_objs * sizeof(int));
	}
	ctx->slot_to_id[slot] = (int)id;
	ctx->id_to_slot[id] = slot;
	return id;
}

static void x4ss_collect_value(struct x4ss_save_ctx *ctx, const union vm_value *vals, const struct ain_type *t)
{
	if (!t)
		return;
	switch (t->data) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_ARRAY:
	case AIN_ARRAY_TYPE:
	case AIN_REF_ARRAY:
	case AIN_REF_ARRAY_TYPE:
		x4ss_register_page(ctx, vals[0].i);
		break;
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		x4ss_register_page(ctx, vals[0].i);
		break;
	case AIN_WRAP:
		if (!t->array_type)
			break;
		if (t->array_type->data == AIN_STRING || t->array_type->data == AIN_DELEGATE)
			break;
		x4ss_register_page(ctx, vals[0].i);
		break;
	case AIN_REF_INT:
	case AIN_REF_FLOAT:
	case AIN_REF_BOOL:
	case AIN_REF_LONG_INT:
	case AIN_REF_ENUM:
	case AIN_REF_OPTION:
		x4ss_register_page(ctx, vals[0].i);
		break;
	case AIN_OPTION: {
		int w = ain_type_slot_width(t);
		if (vals[w - 1].i == 0 && t->array_type)
			x4ss_collect_value(ctx, vals, t->array_type);
		break;
	}
	default:
		break;
	}
}

static void x4ss_write_value(struct x4ss_save_ctx *ctx, struct x4ss_buf *b, const union vm_value *vals, const struct ain_type *t)
{
	if (!t) {
		x4ss_put_u8(b, X4SS_VAL_NONE);
		return;
	}
	switch (t->data) {
	case AIN_VOID:
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		x4ss_put_u8(b, X4SS_VAL_NONE);
		break;
	case AIN_STRING:
	case AIN_REF_STRING: {
		x4ss_put_u8(b, X4SS_VAL_STRING);
		if (string_index_valid(vals[0].i)) {
			struct string *s = heap_get_string(vals[0].i);
			x4ss_put_u32(b, (uint32_t)s->size);
			x4ss_put_bytes(b, s->text, (size_t)s->size);
		} else {
			x4ss_put_u32(b, 0);
		}
		break;
	}
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_ARRAY:
	case AIN_ARRAY_TYPE:
	case AIN_REF_ARRAY:
	case AIN_REF_ARRAY_TYPE: {
		x4ss_put_u8(b, X4SS_VAL_OBJ);
		uint32_t id = (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_id[vals[0].i] > 0)
			? (uint32_t)ctx->slot_to_id[vals[0].i] : 0;
		x4ss_put_u32(b, id);
		break;
	}
	case AIN_IFACE:
	case AIN_IFACE_WRAP: {
		x4ss_put_u8(b, X4SS_VAL_IFACE);
		uint32_t id = (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_id[vals[0].i] > 0)
			? (uint32_t)ctx->slot_to_id[vals[0].i] : 0;
		x4ss_put_u32(b, id);
		x4ss_put_i32(b, id ? vals[1].i : 0);
		break;
	}
	case AIN_WRAP: {
		if (!t->array_type || t->array_type->data == AIN_DELEGATE) {
			x4ss_put_u8(b, X4SS_VAL_NONE);
			break;
		}
		if (t->array_type->data == AIN_STRING) {
			x4ss_put_u8(b, X4SS_VAL_STRING);
			if (string_index_valid(vals[0].i)) {
				struct string *s = heap_get_string(vals[0].i);
				x4ss_put_u32(b, (uint32_t)s->size);
				x4ss_put_bytes(b, s->text, (size_t)s->size);
			} else {
				x4ss_put_u32(b, 0);
			}
			break;
		}
		if (t->array_type->data == AIN_IFACE || t->array_type->data == AIN_IFACE_WRAP) {
			x4ss_put_u8(b, X4SS_VAL_IFACE);
			uint32_t id = (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_id[vals[0].i] > 0)
				? (uint32_t)ctx->slot_to_id[vals[0].i] : 0;
			x4ss_put_u32(b, id);
			x4ss_put_i32(b, id ? vals[1].i : 0);
			break;
		}
		if (ain_type_slot_width(t) == 2) {
			x4ss_put_u8(b, X4SS_VAL_PAGE_VAR);
			uint32_t id = (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_id[vals[0].i] > 0)
				? (uint32_t)ctx->slot_to_id[vals[0].i] : 0;
			x4ss_put_u32(b, id);
			x4ss_put_i32(b, id ? vals[1].i : 0);
			break;
		}
		x4ss_put_u8(b, X4SS_VAL_OBJ);
		uint32_t id = (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_id[vals[0].i] > 0)
			? (uint32_t)ctx->slot_to_id[vals[0].i] : 0;
		x4ss_put_u32(b, id);
		break;
	}
	case AIN_REF_INT:
	case AIN_REF_FLOAT:
	case AIN_REF_BOOL:
	case AIN_REF_LONG_INT:
	case AIN_REF_ENUM:
	case AIN_REF_OPTION: {
		x4ss_put_u8(b, X4SS_VAL_PAGE_VAR);
		uint32_t id = (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_id[vals[0].i] > 0)
			? (uint32_t)ctx->slot_to_id[vals[0].i] : 0;
		x4ss_put_u32(b, id);
		x4ss_put_i32(b, id ? vals[1].i : 0);
		break;
	}
	case AIN_OPTION: {
		int w = ain_type_slot_width(t);
		bool is_none = (vals[w - 1].i != 0);
		x4ss_put_u8(b, X4SS_VAL_OPTION);
		x4ss_put_u8(b, is_none ? 1 : 0);
		if (!is_none)
			x4ss_write_value(ctx, b, vals, t->array_type);
		break;
	}
	default:
		x4ss_put_u8(b, X4SS_VAL_SCALAR);
		x4ss_put_i32(b, vals[0].i);
		break;
	}
}

static void x4ss_skip_value_depth(struct x4ss_reader *r, int depth)
{
	if (depth > 16) {
		r->error = true;
		return;
	}
	uint8_t tag = x4ss_get_u8(r);
	if (r->error)
		return;
	switch (tag) {
	case X4SS_VAL_NONE:
		break;
	case X4SS_VAL_SCALAR:
		x4ss_get_i32(r);
		break;
	case X4SS_VAL_STRING: {
		uint32_t len = x4ss_get_u32(r);
		x4ss_get_bytes(r, NULL, len);
		break;
	}
	case X4SS_VAL_OBJ:
		x4ss_get_u32(r);
		break;
	case X4SS_VAL_IFACE:
	case X4SS_VAL_PAGE_VAR:
		x4ss_get_u32(r);
		x4ss_get_i32(r);
		break;
	case X4SS_VAL_OPTION: {
		uint8_t is_none = x4ss_get_u8(r);
		if (!is_none && !r->error)
			x4ss_skip_value_depth(r, depth + 1);
		break;
	}
	default:
		r->error = true;
		break;
	}
}

static void x4ss_skip_value(struct x4ss_reader *r)
{
	x4ss_skip_value_depth(r, 0);
}

static int x4ss_resolve_vtable_offset(int concrete_sno, int iface_sno, int saved_voff)
{
	if (concrete_sno >= 0 && concrete_sno < ain->nr_structures && iface_sno >= 0) {
		struct ain_struct *st = &ain->structures[concrete_sno];
		for (int i = 0; i < st->nr_interfaces; i++) {
			if (st->interfaces[i].struct_type == iface_sno)
				return st->interfaces[i].vtable_offset;
		}
	}
	return saved_voff;
}

struct x4ss_load_ctx {
	uint32_t nr_objs;
	int *id_to_slot;
	bool *is_root;
};

static void x4ss_skip_tagged_value_depth(struct x4ss_reader *r, uint8_t tag, int depth)
{
	switch (tag) {
	case X4SS_VAL_NONE: break;
	case X4SS_VAL_SCALAR: x4ss_get_i32(r); break;
	case X4SS_VAL_STRING: { uint32_t l = x4ss_get_u32(r); x4ss_get_bytes(r, NULL, l); break; }
	case X4SS_VAL_OBJ: x4ss_get_u32(r); break;
	case X4SS_VAL_IFACE: case X4SS_VAL_PAGE_VAR: x4ss_get_u32(r); x4ss_get_i32(r); break;
	case X4SS_VAL_OPTION: if (!x4ss_get_u8(r)) x4ss_skip_value_depth(r, depth + 1); break;
	default: r->error = true; break;
	}
}

static void x4ss_read_value_depth(struct x4ss_load_ctx *ctx, struct x4ss_reader *r,
		union vm_value *dst, const struct ain_type *t, bool has_existing, int depth)
{
	if (depth > 16) {
		r->error = true;
		return;
	}
	uint8_t tag = x4ss_get_u8(r);
	if (r->error)
		return;
	if (!t || t->data == AIN_VOID || t->data == AIN_DELEGATE || t->data == AIN_REF_DELEGATE) {
		x4ss_skip_tagged_value_depth(r, tag, depth);
		return;
	}

	if (has_existing) {
		elem_value_fini(dst, t, false);
		int w = ain_type_slot_width(t);
		for (int k = 0; k < w; k++)
			dst[k].i = 0;
	}

	if (t->data == AIN_OPTION) {
		int w = ain_type_slot_width(t);
		if (tag != X4SS_VAL_OPTION) {
			/* Consume mismatched payload so stream stays aligned, then default to none */
			x4ss_skip_tagged_value_depth(r, tag, depth);
			for (int k = 0; k < w - 1; k++)
				dst[k].i = -1;
			dst[w - 1].i = 1;
			return;
		}
		uint8_t is_none = x4ss_get_u8(r);
		if (is_none) {
			for (int k = 0; k < w - 1; k++)
				dst[k].i = -1;
			dst[w - 1].i = 1;
		} else {
			x4ss_read_value_depth(ctx, r, dst, t->array_type, false, depth + 1);
			dst[w - 1].i = 0;
		}
		return;
	}

	switch (tag) {
	case X4SS_VAL_NONE:
		variable_init_typed(dst, t, false, false);
		break;
	case X4SS_VAL_SCALAR:
		dst[0].i = x4ss_get_i32(r);
		break;
	case X4SS_VAL_STRING: {
		uint32_t len = x4ss_get_u32(r);
		if (r->error || len > r->len - r->pos) {
			r->error = true;
			dst[0].i = heap_alloc_string(string_ref(&EMPTY_STRING));
			break;
		}
		struct string *s = make_string((const char *)(r->data + r->pos), len);
		r->pos += len;
		dst[0].i = heap_alloc_string(s);
		break;
	}
	case X4SS_VAL_OBJ: {
		uint32_t id = x4ss_get_u32(r);
		int slot = (id > 0 && id <= ctx->nr_objs) ? ctx->id_to_slot[id] : -1;
		if (slot >= 0)
			heap_ref(slot);
		dst[0].i = slot;
		break;
	}
	case X4SS_VAL_IFACE: {
		uint32_t id = x4ss_get_u32(r);
		int32_t saved_voff = x4ss_get_i32(r);
		int slot = (id > 0 && id <= ctx->nr_objs) ? ctx->id_to_slot[id] : -1;
		if (slot >= 0 && page_index_valid(slot) && heap[slot].page) {
			int concrete_sno = heap[slot].page->index;
			int iface_sno = (t->data == AIN_WRAP && t->array_type) ? t->array_type->struc : t->struc;
			heap_ref(slot);
			dst[0].i = slot;
			dst[1].i = x4ss_resolve_vtable_offset(concrete_sno, iface_sno, saved_voff);
		} else {
			dst[0].i = -1;
			dst[1].i = 0;
		}
		break;
	}
	case X4SS_VAL_PAGE_VAR: {
		uint32_t id = x4ss_get_u32(r);
		int32_t varno = x4ss_get_i32(r);
		int slot = (id > 0 && id <= ctx->nr_objs) ? ctx->id_to_slot[id] : -1;
		if (slot >= 0) {
			heap_ref(slot);
			dst[0].i = slot;
			dst[1].i = varno;
		} else {
			dst[0].i = -1;
			dst[1].i = 0;
		}
		break;
	}
	default:
		r->error = true;
		break;
	}
}

static void x4ss_read_value(struct x4ss_load_ctx *ctx, struct x4ss_reader *r,
		union vm_value *dst, const struct ain_type *t, bool has_existing)
{
	x4ss_read_value_depth(ctx, r, dst, t, has_existing, 0);
}

static char *resolve_struct_save_path(const char *filename, bool save_folder)
{
	return save_folder ? savedir_path(filename) : gamedir_path(filename);
}

static const uint8_t GDSS_OUTER_MAGIC[4] = { 'G', 'D', 1, 1 };
static const char GDSS_INNER_MAGIC[17] = "serialize_struct";

struct gd_ss_str {
	const char *ptr;
	size_t len;
};

struct gd_ss_field {
	int32_t type1;
	int32_t type2;
	const char *name;
};

struct gd_ss_struct_def {
	const char *name;
	uint32_t nr_fields;
	struct gd_ss_field *fields;
};

struct gd_ss_obj {
	int32_t struct_def;
	uint32_t nr_vals;
	const uint8_t *val_idxs_le;
};

struct gd_ss_array {
	int32_t rank;
	uint32_t count;
	int32_t elem_type;
	const uint8_t *elems_le;
};

struct gd_ss_parsed {
	uint8_t *raw;
	uint32_t raw_len;
	uint32_t nr_objs;
	struct gd_ss_obj *objs;
	uint32_t nr_roots;
	int32_t *root_ids;
	uint32_t nr_strings;
	struct gd_ss_str *strings;
	uint32_t nr_arrays;
	struct gd_ss_array *arrays;
	uint32_t nr_vals;
	const uint8_t *vals_le;
	uint32_t nr_struct_defs;
	struct gd_ss_struct_def *struct_defs;
	const char *comment;
	uint32_t comment_len;
	bool has_comment;
};

static uint32_t gd_read_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t gd_read_i32_le(const uint8_t *p)
{
	return (int32_t)gd_read_u32_le(p);
}

static void gd_write_u32_le(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
	p[2] = (uint8_t)((v >> 16) & 0xff);
	p[3] = (uint8_t)((v >> 24) & 0xff);
}

static const char *gd_read_cstr_slice(const uint8_t *base, uint32_t limit, uint32_t *pos, size_t *len_out)
{
	if (*pos >= limit)
		return NULL;
	const uint8_t *start = base + *pos;
	const void *nul = memchr(start, '\0', (size_t)(limit - *pos));
	if (!nul)
		return NULL;
	size_t slen = (size_t)((const uint8_t *)nul - start);
	*pos += (uint32_t)(slen + 1);
	if (len_out)
		*len_out = slen;
	return (const char *)start;
}

static void gd_ss_free_parsed(struct gd_ss_parsed *p)
{
	if (!p)
		return;
	free(p->objs);
	free(p->root_ids);
	free(p->strings);
	free(p->arrays);
	if (p->struct_defs) {
		for (uint32_t i = 0; i < p->nr_struct_defs; i++)
			free(p->struct_defs[i].fields);
		free(p->struct_defs);
	}
	free(p->raw);
	memset(p, 0, sizeof(*p));
}

/*
 * Decompress a native Windows PC "GD\x01\x01" + zlib serialize_struct file.
 * Returns true if `path` has GD\x01\x01 + serialize_struct\0 header and
 * decompresses cleanly; returns false otherwise.
 */
static bool gd_ss_decompress_file(const char *path, uint8_t **raw_out, uint32_t *ulen_out)
{
	*raw_out = NULL;
	*ulen_out = 0;
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp)
		return false;

	uint8_t hdr[8];
	if (fread(hdr, 1, 8, fp) != 8 || memcmp(hdr, GDSS_OUTER_MAGIC, 4) != 0) {
		fclose(fp);
		return false;
	}
	uint32_t ulen = gd_read_u32_le(hdr + 4);
	if (ulen < 81 || ulen > 256u * 1024u * 1024u) {
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	long fsize = ftell(fp);
	if (fsize <= 8 || fseek(fp, 8, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}
	size_t zlen = (size_t)(fsize - 8);
	uint8_t *zbuf = xmalloc(zlen);
	if (fread(zbuf, 1, zlen, fp) != zlen) {
		free(zbuf);
		fclose(fp);
		return false;
	}
	fclose(fp);

	uint8_t *raw = xmalloc(ulen);
	uLongf dst_len = (uLongf)ulen;
	int zret = uncompress(raw, &dst_len, zbuf, (uLong)zlen);
	free(zbuf);
	if (zret != Z_OK || dst_len != ulen || memcmp(raw, GDSS_INNER_MAGIC, 17) != 0) {
		free(raw);
		return false;
	}
	*raw_out = raw;
	*ulen_out = ulen;
	return true;
}

static bool gd_ss_parse(uint8_t *raw, uint32_t ulen, struct gd_ss_parsed *out)
{
	memset(out, 0, sizeof(*out));
	out->raw = raw;
	out->raw_len = ulen;

	if (ulen < 81 || memcmp(raw, GDSS_INNER_MAGIC, 17) != 0)
		return false;
	uint32_t version = gd_read_u32_le(raw + 17);
	if (version != 1000)
		return false;

	uint32_t sec_off[7], sec_cnt[7];
	for (int i = 0; i < 7; i++) {
		sec_off[i] = gd_read_u32_le(raw + 25 + i * 8);
		sec_cnt[i] = gd_read_u32_le(raw + 29 + i * 8);
	}
	for (int i = 1; i < 7; i++) {
		if (sec_off[i] > ulen)
			return false;
		if (i > 1 && sec_off[i] < sec_off[i - 1])
			return false;
	}
	if (sec_off[1] < 81)
		return false;

	/* C1: Object index records */
	out->nr_objs = sec_cnt[1];
	if (out->nr_objs > 16u * 1024u * 1024u)
		return false;
	out->objs = xcalloc(max(out->nr_objs, 1u), sizeof(*out->objs));
	uint32_t pos = sec_off[1];
	for (uint32_t i = 0; i < out->nr_objs; i++) {
		if (pos + 8 > sec_off[2])
			goto fail;
		out->objs[i].struct_def = gd_read_i32_le(raw + pos);
		uint32_t nf = gd_read_u32_le(raw + pos + 4);
		pos += 8;
		if (nf > (sec_off[2] - pos) / 4)
			goto fail;
		out->objs[i].nr_vals = nf;
		out->objs[i].val_idxs_le = raw + pos;
		pos += nf * 4;
	}

	/* C2: Root descriptor entries */
	if (sec_cnt[2] > 16u * 1024u * 1024u)
		goto fail;
	uint32_t max_roots = (sec_off[3] - sec_off[2]) / 17u;
	uint32_t root_cap = max(min(sec_cnt[2], max_roots), 1u);
	out->root_ids = xcalloc(root_cap, sizeof(int32_t));
	pos = sec_off[2];
	for (uint32_t i = 0; i < sec_cnt[2] && pos < sec_off[3]; i++) {
		if (pos + 16 > sec_off[3])
			goto fail;
		int32_t type1 = gd_read_i32_le(raw + pos);
		int32_t root_id = gd_read_i32_le(raw + pos + 8);
		pos += 16;
		if (!gd_read_cstr_slice(raw, sec_off[3], &pos, NULL))
			goto fail;
		if (type1 != AIN_VOID && root_id > 0)
			out->root_ids[out->nr_roots++] = root_id;
	}

	/* C3: String pool */
	out->nr_strings = sec_cnt[3];
	if (out->nr_strings > 16u * 1024u * 1024u)
		goto fail;
	out->strings = xcalloc(max(out->nr_strings, 1u), sizeof(*out->strings));
	pos = sec_off[3];
	for (uint32_t i = 0; i < out->nr_strings; i++) {
		size_t slen = 0;
		const char *s = gd_read_cstr_slice(raw, sec_off[4], &pos, &slen);
		if (!s)
			goto fail;
		out->strings[i].ptr = s;
		out->strings[i].len = slen;
	}

	/* C4: Array pool */
	out->nr_arrays = sec_cnt[4];
	if (out->nr_arrays > 16u * 1024u * 1024u)
		goto fail;
	out->arrays = xcalloc(max(out->nr_arrays, 1u), sizeof(*out->arrays));
	pos = sec_off[4];
	for (uint32_t i = 0; i < out->nr_arrays; i++) {
		if (pos + 4 > sec_off[5])
			goto fail;
		int32_t rank = gd_read_i32_le(raw + pos);
		pos += 4;
		if (rank != 1)
			goto fail;
		if (pos + 16 > sec_off[5])
			goto fail;
		uint32_t total = gd_read_u32_le(raw + pos + 8);
		int32_t et = gd_read_i32_le(raw + pos + 12);
		pos += 16;
		if (total > (sec_off[5] - pos) / 4)
			goto fail;
		out->arrays[i].rank = rank;
		out->arrays[i].count = total;
		out->arrays[i].elem_type = et;
		out->arrays[i].elems_le = raw + pos;
		pos += total * 4;
	}

	/* C5: Value table + struct schema definitions */
	out->nr_vals = sec_cnt[5];
	if (out->nr_vals > (sec_off[6] - sec_off[5]) / 4)
		goto fail;
	out->vals_le = raw + sec_off[5];
	pos = sec_off[5] + out->nr_vals * 4;
	if (pos + 4 > sec_off[6])
		goto fail;
	out->nr_struct_defs = gd_read_u32_le(raw + pos);
	pos += 4;
	if (out->nr_struct_defs > 1024u * 1024u)
		goto fail;
	out->struct_defs = xcalloc(max(out->nr_struct_defs, 1u), sizeof(*out->struct_defs));
	for (uint32_t si = 0; si < out->nr_struct_defs; si++) {
		const char *sname = gd_read_cstr_slice(raw, sec_off[6], &pos, NULL);
		if (!sname || pos + 4 > sec_off[6])
			goto fail;
		uint32_t nf = gd_read_u32_le(raw + pos);
		pos += 4;
		if (nf > 1024u * 1024u)
			goto fail;
		out->struct_defs[si].name = sname;
		out->struct_defs[si].nr_fields = nf;
		out->struct_defs[si].fields = xcalloc(max(nf, 1u), sizeof(struct gd_ss_field));
		for (uint32_t fi = 0; fi < nf; fi++) {
			if (pos + 8 > sec_off[6])
				goto fail;
			out->struct_defs[si].fields[fi].type1 = gd_read_i32_le(raw + pos);
			out->struct_defs[si].fields[fi].type2 = gd_read_i32_le(raw + pos + 4);
			pos += 8;
			const char *fname = gd_read_cstr_slice(raw, sec_off[6], &pos, NULL);
			if (!fname)
				goto fail;
			out->struct_defs[si].fields[fi].name = fname;
		}
	}

	/* C6: Optional SaveData comment */
	if (sec_cnt[6] > 0 && sec_off[6] + 4 <= ulen) {
		uint32_t clen = gd_read_u32_le(raw + sec_off[6]);
		if (clen <= ulen - sec_off[6] - 4) {
			out->comment = (const char *)(raw + sec_off[6] + 4);
			out->comment_len = clen;
			out->has_comment = true;
		}
	}
	return true;

fail:
	out->raw = NULL; /* Caller frees `raw` or gd_ss_free_parsed owns it */
	gd_ss_free_parsed(out);
	return false;
}

static bool gd_ss_detect_is_gbk(const struct gd_ss_parsed *p)
{
	int native_votes = 0;
	int opp_votes = 0;
	for (uint32_t si = 0; si < p->nr_struct_defs && (native_votes + opp_votes) < 60; si++) {
		const struct gd_ss_struct_def *sd = &p->struct_defs[si];
		bool st_non_ascii = sd->name && !cjk_is_ascii_only(sd->name);
		bool has_non_ascii_field = false;
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name;
			if (fn && fn[0] && !cjk_is_ascii_only(fn)) {
				has_non_ascii_field = true;
				break;
			}
		}
		if (!st_non_ascii && !has_non_ascii_field)
			continue;
		int sno = save_find_struct(sd->name);
		if (sno < 0 || sno >= ain->nr_structures)
			continue;
		const struct ain_struct *st = &ain->structures[sno];
		if (st_non_ascii) {
			if (!strcmp(st->name, sd->name))
				native_votes += 10;
			else
				opp_votes += 10;
		}
		if (!has_non_ascii_field)
			continue;
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name;
			if (!fn || !fn[0] || cjk_is_ascii_only(fn))
				continue;
			int m = struct_member_index(&ain->structures[sno], fn);
			if (m < 0)
				continue;
			const char *an = (st->members[m].name2 && st->members[m].name2[0])
				? st->members[m].name2 : st->members[m].name;
			if (!strcmp(an, fn))
				native_votes += 10;
			else
				opp_votes += 10;
		}
	}
	if (opp_votes > native_votes)
		return !sys4_text_is_gbk;
	if (native_votes > opp_votes)
		return sys4_text_is_gbk;

	int bias = 0;
	for (uint32_t i = 0, seen = 0; i < p->nr_strings && seen < 64; i++) {
		if (p->strings[i].len > 0 && !cjk_is_ascii_only(p->strings[i].ptr)) {
			bias += save_text_gbk_bias(p->strings[i].ptr);
			seen++;
		}
	}
	if (p->has_comment && p->comment_len > 0) {
		char *cb = xmalloc(p->comment_len + 1);
		memcpy(cb, p->comment, p->comment_len);
		cb[p->comment_len] = '\0';
		bias += save_text_gbk_bias(cb);
		free(cb);
	}
	if (bias > 15)
		return true;
	if (bias < -15)
		return false;
	return sys4_text_is_gbk;
}

struct gd_ss_load_ctx {
	const struct gd_ss_parsed *p;
	bool src_is_gbk;
	int *c1_slots;
	bool *is_root;
	int *arr_slots;
	struct ain_type *arr_types;
};

static void gd_ss_assign_value(struct gd_ss_load_ctx *ctx, union vm_value *dst,
		const struct ain_type *t, int32_t raw_val, bool is_none, bool has_opt_none, bool has_existing);

static int gd_ss_materialize_array(struct gd_ss_load_ctx *ctx, int32_t arr_idx,
		enum ain_data_type a_type, const struct ain_type *et)
{
	if (arr_idx < 0 || (uint32_t)arr_idx >= ctx->p->nr_arrays) {
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, NULL);
		return slot;
	}
	const struct gd_ss_array *arr = &ctx->p->arrays[arr_idx];
	bool can_reuse = ctx->arr_slots[arr_idx] >= 0
		&& ctx->arr_types[arr_idx].data == et->data
		&& ctx->arr_types[arr_idx].struc == et->struc;
	if (can_reuse) {
		heap_ref(ctx->arr_slots[arr_idx]);
		return ctx->arr_slots[arr_idx];
	}

	int w = ain_type_slot_width(et);
	if (w <= 0 || arr->count > 16u * 1024u * 1024u || arr->count > (uint32_t)(INT_MAX / w)) {
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, NULL);
		return slot;
	}
	struct page *ap = alloc_page(ARRAY_PAGE, a_type, (int)arr->count * w);
	ap->array.rank = arr->rank > 0 ? arr->rank : 1;
	ap->array.struct_type = et->struc;
	ap->array.element_type = *et;
	for (uint32_t k = 0; k < arr->count; k++)
		variable_init_typed(&ap->values[k * w], et, false, false);
	int slot = heap_alloc_page(ap);

	bool record_cache = ctx->arr_slots[arr_idx] < 0;
	if (record_cache) {
		ctx->arr_slots[arr_idx] = slot;
		ctx->arr_types[arr_idx] = *et;
		heap_ref(slot); /* table retains one ref released at cleanup for shared cycle safety */
	}

	for (uint32_t k = 0; k < arr->count; k++) {
		int32_t elem = gd_read_i32_le(arr->elems_le + k * 4);
		gd_ss_assign_value(ctx, &ap->values[k * w], et, elem, false, false, true);
	}
	return slot;
}

static bool gd_ss_is_iface_type(const struct ain_type *t)
{
	if (!t)
		return false;
	const struct ain_type *eff = (t->data == AIN_WRAP && t->array_type) ? t->array_type : t;
	return eff->data == AIN_IFACE || eff->data == AIN_IFACE_WRAP;
}

static bool gd_ss_iface_array_unserialized(const struct gd_ss_load_ctx *ctx,
		const union vm_value *dst, const struct ain_type *t, int32_t raw_val)
{
	if (!dst || !t || raw_val < 0 || (uint32_t)raw_val >= ctx->p->nr_arrays)
		return false;
	const struct ain_type *eff = (t->data == AIN_WRAP && t->array_type) ? t->array_type : t;
	if ((eff->data != AIN_ARRAY && eff->data != AIN_REF_ARRAY) || !gd_ss_is_iface_type(eff->array_type))
		return false;
	const struct gd_ss_array *arr = &ctx->p->arrays[raw_val];
	if (arr->count == 0 || (arr->elem_type != AIN_IFACE && arr->elem_type != AIN_IFACE_WRAP))
		return false;
	if (!page_index_valid(dst[0].i) || !heap[dst[0].i].page
			|| heap[dst[0].i].page->type != ARRAY_PAGE || heap[dst[0].i].page->nr_vars <= 0)
		return false;
	for (uint32_t k = 0; k < arr->count; k++) {
		if (gd_read_i32_le(arr->elems_le + k * 4) >= 0)
			return false;
	}
	return true;
}

static void gd_ss_assign_value(struct gd_ss_load_ctx *ctx, union vm_value *dst,
		const struct ain_type *t, int32_t raw_val, bool is_none, bool has_opt_none, bool has_existing)
{
	if (!t || t->data == AIN_VOID || t->data == AIN_DELEGATE || t->data == AIN_REF_DELEGATE)
		return;

	/* PC SerializeStruct writes -1 placeholders for AIN_IFACE array elements; keep pre-built runtime interfaces. */
	if (has_existing && gd_ss_iface_array_unserialized(ctx, dst, t, raw_val))
		return;

	if (has_existing) {
		elem_value_fini(dst, t, false);
		int w = ain_type_slot_width(t);
		for (int k = 0; k < w; k++)
			dst[k].i = 0;
	}

	if (t->data == AIN_OPTION) {
		int w = ain_type_slot_width(t);
		if (!has_opt_none || is_none || !t->array_type) {
			for (int k = 0; k < w - 1; k++)
				dst[k].i = -1;
			dst[w - 1].i = 1;
		} else {
			gd_ss_assign_value(ctx, dst, t->array_type, raw_val, false, false, false);
			dst[w - 1].i = 0;
		}
		return;
	}

	const struct ain_type *eff = t;
	while (eff->data == AIN_WRAP && eff->array_type)
		eff = eff->array_type;
	switch (eff->data) {
	case AIN_VOID:
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		break;
	case AIN_STRING:
	case AIN_REF_STRING: {
		struct string *s;
		if (raw_val == INT32_MAX || raw_val < 0 || (uint32_t)raw_val >= ctx->p->nr_strings)
			s = string_ref(&EMPTY_STRING);
		else
			s = save_decode_vm_string(ctx->p->strings[raw_val].ptr, ctx->p->strings[raw_val].len, ctx->src_is_gbk);
		dst[0].i = heap_alloc_string(s);
		break;
	}
	case AIN_STRUCT:
	case AIN_REF_STRUCT: {
		int slot = (raw_val > 0 && (uint32_t)raw_val < ctx->p->nr_objs) ? ctx->c1_slots[raw_val] : -1;
		if (slot >= 0)
			heap_ref(slot);
		dst[0].i = slot;
		break;
	}
	case AIN_IFACE:
	case AIN_IFACE_WRAP: {
		int slot = (raw_val > 0 && (uint32_t)raw_val < ctx->p->nr_objs) ? ctx->c1_slots[raw_val] : -1;
		if (slot >= 0 && page_index_valid(slot) && heap[slot].page) {
			heap_ref(slot);
			dst[0].i = slot;
			dst[1].i = x4ss_resolve_vtable_offset(heap[slot].page->index, eff->struc, 0);
		} else {
			dst[0].i = -1;
			dst[1].i = 0;
		}
		break;
	}
	case AIN_ARRAY:
	case AIN_REF_ARRAY: {
		if (eff->array_type) {
			dst[0].i = gd_ss_materialize_array(ctx, raw_val, AIN_ARRAY, eff->array_type);
		} else {
			int slot = heap_alloc_slot(VM_PAGE);
			heap_set_page(slot, NULL);
			dst[0].i = slot;
		}
		break;
	}
	case AIN_ARRAY_TYPE: {
		struct ain_type et = { .data = array_type(eff->data), .struc = eff->struc };
		enum ain_data_type a_type = ain->version >= 11 ? AIN_ARRAY : eff->data;
		dst[0].i = gd_ss_materialize_array(ctx, raw_val, a_type, &et);
		break;
	}
	case AIN_REF_ARRAY_TYPE: {
		struct ain_type et = { .data = array_type(eff->data), .struc = eff->struc };
		enum ain_data_type a_type = ain->version >= 11 ? AIN_ARRAY
			: (eff->data == AIN_REF_ARRAY_DELEGATE ? AIN_ARRAY_DELEGATE : eff->data - 8);
		dst[0].i = gd_ss_materialize_array(ctx, raw_val, a_type, &et);
		break;
	}
	default:
		dst[0].i = raw_val;
		break;
	}
}

static bool deserialize_gd_structs(const char *path, const char *filename,
		bool save_folder, struct page *struct_page_list, uint8_t *raw, uint32_t ulen)
{
	struct gd_ss_parsed parsed;
	if (!gd_ss_parse(raw, ulen, &parsed)) {
		free(raw);
		WARNING("DeserializeStruct: invalid GD serialize_struct file %s", display_sjis0(filename));
		return false;
	}

	int nr_caller_roots = v12_array_length(struct_page_list);
	if ((int)parsed.nr_roots != nr_caller_roots) {
		WARNING("DeserializeStruct: root count mismatch (%u vs %d) in %s",
				parsed.nr_roots, nr_caller_roots, display_sjis0(filename));
		gd_ss_free_parsed(&parsed);
		return false;
	}

	int *schema_snos = xcalloc(max(parsed.nr_struct_defs, 1u), sizeof(int));
	for (uint32_t si = 0; si < parsed.nr_struct_defs; si++)
		schema_snos[si] = save_find_struct(parsed.struct_defs[si].name);

	struct gd_ss_load_ctx ctx = {
		.p = &parsed,
		.src_is_gbk = gd_ss_detect_is_gbk(&parsed),
		.c1_slots = xcalloc(max(parsed.nr_objs, 1u), sizeof(int)),
		.is_root = xcalloc(max(parsed.nr_objs, 1u), sizeof(bool)),
		.arr_slots = xcalloc(max(parsed.nr_arrays, 1u), sizeof(int)),
		.arr_types = xcalloc(max(parsed.nr_arrays, 1u), sizeof(struct ain_type)),
	};
	for (uint32_t id = 0; id < parsed.nr_objs; id++)
		ctx.c1_slots[id] = -1;
	for (uint32_t ai = 0; ai < parsed.nr_arrays; ai++)
		ctx.arr_slots[ai] = -1;

	for (int r = 0; r < nr_caller_roots; r++) {
		int32_t rid = parsed.root_ids[r];
		if (rid <= 0 || (uint32_t)rid >= parsed.nr_objs) {
			free(ctx.arr_types);
			free(ctx.arr_slots);
			free(ctx.is_root);
			free(ctx.c1_slots);
			free(schema_snos);
			gd_ss_free_parsed(&parsed);
			return false;
		}
		ctx.c1_slots[rid] = struct_page_list->values[r].i;
		ctx.is_root[rid] = true;
	}

	/* Pass 1: pre-allocate non-root STRUCT_PAGE instances */
	for (uint32_t id = 1; id < parsed.nr_objs; id++) {
		if (ctx.is_root[id])
			continue;
		int32_t sd_idx = parsed.objs[id].struct_def;
		if (sd_idx < 0 || (uint32_t)sd_idx >= parsed.nr_struct_defs)
			continue;
		int sno = schema_snos[sd_idx];
		if (sno >= 0)
			ctx.c1_slots[id] = alloc_struct(sno);
	}

	/* Precompute field -> member mappings once per struct schema */
	int **schema_fmap = xcalloc(max(parsed.nr_struct_defs, 1u), sizeof(int *));
	for (uint32_t si = 0; si < parsed.nr_struct_defs; si++) {
		const struct gd_ss_struct_def *sd = &parsed.struct_defs[si];
		int sno = schema_snos[si];
		if (sno < 0 || sno >= ain->nr_structures || sd->nr_fields == 0)
			continue;
		struct ain_struct *st = &ain->structures[sno];
		schema_fmap[si] = xcalloc(sd->nr_fields, sizeof(int));
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name;
			int m = -1;
			if (fn && strcmp(fn, "<vtable>") == 0) {
				m = -1;
			} else if (fn && fn[0]) {
				m = struct_member_index(st, fn);
			}
			schema_fmap[si][fi] = m;
		}
	}

	/* Pass 2: populate structs and materialize typed arrays */
	for (uint32_t id = 1; id < parsed.nr_objs; id++) {
		int slot = ctx.c1_slots[id];
		if (slot < 0 || !page_index_valid(slot))
			continue;
		struct page *sp = heap[slot].page;
		if (!sp || sp->type != STRUCT_PAGE)
			continue;
		if (ctx.is_root[id]) {
			struct ain_struct *rst = &ain->structures[sp->index];
			for (int m = 0; m < rst->nr_members && m < sp->nr_vars; m++) {
				if (rst->members[m].type.data == AIN_DELEGATE) {
					int dslot = sp->values[m].i;
					if (page_index_valid(dslot) && heap[dslot].page)
						heap_set_page(dslot, delegate_clear(heap[dslot].page));
				}
			}
		}

		int32_t sd_idx = parsed.objs[id].struct_def;
		if (sd_idx < 0 || (uint32_t)sd_idx >= parsed.nr_struct_defs)
			continue;
		const struct gd_ss_struct_def *sd = &parsed.struct_defs[sd_idx];
		struct ain_struct *st = &ain->structures[sp->index];
		uint32_t nfields = min(sd->nr_fields, parsed.objs[id].nr_vals);
		bool use_cached_map = (sp->index == schema_snos[sd_idx] && schema_fmap[sd_idx] != NULL);

		for (uint32_t fi = 0; fi < nfields; fi++) {
			const struct gd_ss_field *f = &sd->fields[fi];
			if (f->type1 == AIN_VOID || (f->name && !strcmp(f->name, "<vtable>")))
				continue;
			int m_idx = use_cached_map ? schema_fmap[sd_idx][fi] : struct_member_index(st, f->name);
			if (m_idx < 0 || m_idx >= st->nr_members)
				continue;
			const struct ain_type *mt = &st->members[m_idx].type;
			int mw = ain_type_slot_width(mt);
			if (mw <= 0 || m_idx + mw > sp->nr_vars)
				continue;
			uint32_t v_idx = gd_read_u32_le(parsed.objs[id].val_idxs_le + fi * 4);
			if (v_idx >= parsed.nr_vals)
				continue;
			int32_t raw_val = gd_read_i32_le(parsed.vals_le + v_idx * 4);

			bool is_none = false;
			bool has_opt_none = false;
			if (mt->data == AIN_OPTION) {
				uint32_t none_off = (mw > 1) ? (uint32_t)(mw - 1) : 1u;
				if (fi + none_off < nfields && sd->fields[fi + none_off].type1 == AIN_VOID) {
					uint32_t nv_idx = gd_read_u32_le(parsed.objs[id].val_idxs_le + (fi + none_off) * 4);
					if (nv_idx < parsed.nr_vals) {
						is_none = (gd_read_i32_le(parsed.vals_le + nv_idx * 4) != 0);
						has_opt_none = true;
					}
				} else if (fi + 1 < nfields && sd->fields[fi + 1].type1 == AIN_VOID) {
					uint32_t nv_idx = gd_read_u32_le(parsed.objs[id].val_idxs_le + (fi + 1) * 4);
					if (nv_idx < parsed.nr_vals) {
						is_none = (gd_read_i32_le(parsed.vals_le + nv_idx * 4) != 0);
						has_opt_none = true;
					}
				}
			}
			gd_ss_assign_value(&ctx, &sp->values[m_idx], mt, raw_val, is_none, has_opt_none, true);
		}
	}

	for (uint32_t id = 1; id < parsed.nr_objs; id++) {
		if (!ctx.is_root[id] && ctx.c1_slots[id] >= 0)
			exit_unref(ctx.c1_slots[id]);
	}
	for (uint32_t ai = 0; ai < parsed.nr_arrays; ai++) {
		if (ctx.arr_slots[ai] >= 0)
			exit_unref(ctx.arr_slots[ai]);
	}

	for (uint32_t si = 0; si < parsed.nr_struct_defs; si++)
		free(schema_fmap[si]);
	free(schema_fmap);
	free(ctx.arr_types);
	free(ctx.arr_slots);
	free(ctx.is_root);
	free(ctx.c1_slots);
	free(schema_snos);
	gd_ss_free_parsed(&parsed);

	if (save_folder && is_game_config_save(filename))
		modifier_on_config_loaded();
	(void)path;
	return true;
}

static bool x4ss_read_existing_parts(const char *path,
		uint8_t **comment_out, uint32_t *comment_len_out,
		uint32_t *uncomp_len_out, uint8_t **comp_out, uint32_t *comp_len_out)
{
	if (comment_out) *comment_out = NULL;
	if (comment_len_out) *comment_len_out = 0;
	if (uncomp_len_out) *uncomp_len_out = 0;
	if (comp_out) *comp_out = NULL;
	if (comp_len_out) *comp_len_out = 0;

	FILE *fp = file_open_utf8(path, "rb");
	if (!fp)
		return false;

	uint8_t hdr[12];
	if (fread(hdr, 1, 12, fp) != 12 || memcmp(hdr, X4SS_MAGIC, 8) != 0) {
		fclose(fp);
		return false;
	}
	uint32_t clen = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8)
		| ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
	if (clen > 16 * 1024 * 1024) {
		fclose(fp);
		return false;
	}
	if (comment_out) {
		uint8_t *cbuf = xmalloc(clen + 1);
		if (clen && fread(cbuf, 1, clen, fp) != clen) {
			free(cbuf);
			fclose(fp);
			return false;
		}
		cbuf[clen] = 0;
		*comment_out = cbuf;
		if (comment_len_out)
			*comment_len_out = clen;
	} else {
		if (clen && fseek(fp, (long)clen, SEEK_CUR) != 0) {
			fclose(fp);
			return false;
		}
		if (comment_len_out)
			*comment_len_out = clen;
	}

	if (!comp_out && !uncomp_len_out && !comp_len_out) {
		fclose(fp);
		return true;
	}

	uint8_t phdr[8];
	if (fread(phdr, 1, 8, fp) != 8) {
		fclose(fp);
		return true;
	}
	uint32_t ulen = (uint32_t)phdr[0] | ((uint32_t)phdr[1] << 8)
		| ((uint32_t)phdr[2] << 16) | ((uint32_t)phdr[3] << 24);
	uint32_t zlen = (uint32_t)phdr[4] | ((uint32_t)phdr[5] << 8)
		| ((uint32_t)phdr[6] << 16) | ((uint32_t)phdr[7] << 24);
	if (ulen > 256u * 1024u * 1024u || zlen > 256u * 1024u * 1024u) {
		if (comment_out && *comment_out) {
			free(*comment_out);
			*comment_out = NULL;
		}
		if (comment_len_out)
			*comment_len_out = 0;
		fclose(fp);
		return false;
	}
	if (uncomp_len_out)
		*uncomp_len_out = ulen;
	if (comp_len_out)
		*comp_len_out = zlen;
	if (comp_out && zlen > 0) {
		uint8_t *zbuf = xmalloc(zlen);
		if (fread(zbuf, 1, zlen, fp) != zlen) {
			free(zbuf);
			if (comment_out && *comment_out) {
				free(*comment_out);
				*comment_out = NULL;
			}
			if (comment_len_out)
				*comment_len_out = 0;
			fclose(fp);
			return false;
		}
		*comp_out = zbuf;
	}
	fclose(fp);
	return true;
}

static bool x4ss_write_container(const char *path,
		const uint8_t *comment, uint32_t comment_len,
		uint32_t uncomp_len, const uint8_t *comp, uint32_t comp_len)
{
	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	if (!fp)
		return false;
	uint8_t hdr[12];
	memcpy(hdr, X4SS_MAGIC, 8);
	hdr[8]  = (uint8_t)(comment_len & 0xff);
	hdr[9]  = (uint8_t)((comment_len >> 8) & 0xff);
	hdr[10] = (uint8_t)((comment_len >> 16) & 0xff);
	hdr[11] = (uint8_t)((comment_len >> 24) & 0xff);
	bool ok = fwrite(hdr, 1, 12, fp) == 12;
	if (ok && comment_len > 0)
		ok = fwrite(comment, 1, comment_len, fp) == comment_len;
	uint8_t phdr[8] = {
		(uint8_t)(uncomp_len & 0xff), (uint8_t)((uncomp_len >> 8) & 0xff),
		(uint8_t)((uncomp_len >> 16) & 0xff), (uint8_t)((uncomp_len >> 24) & 0xff),
		(uint8_t)(comp_len & 0xff), (uint8_t)((comp_len >> 8) & 0xff),
		(uint8_t)((comp_len >> 16) & 0xff), (uint8_t)((comp_len >> 24) & 0xff),
	};
	if (ok)
		ok = fwrite(phdr, 1, 8, fp) == 8;
	if (ok && comp_len > 0)
		ok = fwrite(comp, 1, comp_len, fp) == comp_len;
	if (!ok) {
		atomic_save_abort(fp, tmp_path);
		return false;
	}
	return atomic_save_commit(fp, path, tmp_path, is_critical_global_save(path));
}

struct gd_ss_bschema_field {
	int32_t type1;
	int32_t type2;
	char *name;
	size_t name_len;
};

struct gd_ss_bschema {
	char *name;
	size_t name_len;
	uint32_t nr_fields;
	struct gd_ss_bschema_field *fields;
};

struct gd_ss_bobj {
	int32_t struct_def;
	uint32_t nr_vals;
	uint32_t *val_idxs;
};

struct gd_ss_barray {
	int32_t rank;
	uint32_t total;
	int32_t elem_type;
	int32_t *elems;
};

struct gd_ss_builder {
	char **strings;
	size_t *string_lens;
	uint32_t nr_strings;
	uint32_t cap_strings;
	uint32_t *str_ht;
	uint32_t str_ht_cap;

	int32_t *vals;
	uint32_t nr_vals;
	uint32_t cap_vals;

	struct gd_ss_bschema *schemas;
	uint32_t nr_schemas;
	uint32_t cap_schemas;

	struct gd_ss_bobj *objs;
	uint32_t nr_objs;
	uint32_t cap_objs;

	int32_t *root_ids;
	uint32_t nr_roots;

	struct gd_ss_barray *arrays;
	uint32_t nr_arrays;
	uint32_t cap_arrays;

	uint8_t *comment;
	uint32_t comment_len;
	bool has_comment;
};

static uint32_t gd_ss_fnv1a(const char *s, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= (uint8_t)s[i];
		h *= 16777619u;
	}
	return h;
}

static void gd_ss_builder_init(struct gd_ss_builder *b)
{
	memset(b, 0, sizeof(*b));
	b->cap_vals = 256;
	b->vals = xmalloc(b->cap_vals * sizeof(int32_t));
	b->vals[0] = 0;
	b->nr_vals = 1;

	b->cap_objs = 64;
	b->objs = xcalloc(b->cap_objs, sizeof(*b->objs));
	b->objs[0].struct_def = -1;
	b->objs[0].nr_vals = 1;
	b->objs[0].val_idxs = xcalloc(1, sizeof(uint32_t));
	b->objs[0].val_idxs[0] = 0;
	b->nr_objs = 1;

	b->cap_arrays = 64;
	b->arrays = xcalloc(b->cap_arrays, sizeof(*b->arrays));
	b->arrays[0].rank = 1;
	b->arrays[0].total = 0;
	b->arrays[0].elem_type = AIN_INT;
	b->arrays[0].elems = NULL;
	b->nr_arrays = 1;
}

static void gd_ss_builder_free(struct gd_ss_builder *b)
{
	if (!b)
		return;
	for (uint32_t i = 0; i < b->nr_strings; i++)
		free(b->strings[i]);
	free(b->strings);
	free(b->string_lens);
	free(b->str_ht);
	free(b->vals);
	for (uint32_t si = 0; si < b->nr_schemas; si++) {
		free(b->schemas[si].name);
		for (uint32_t fi = 0; fi < b->schemas[si].nr_fields; fi++)
			free(b->schemas[si].fields[fi].name);
		free(b->schemas[si].fields);
	}
	free(b->schemas);
	for (uint32_t i = 0; i < b->nr_objs; i++)
		free(b->objs[i].val_idxs);
	free(b->objs);
	free(b->root_ids);
	for (uint32_t i = 0; i < b->nr_arrays; i++)
		free(b->arrays[i].elems);
	free(b->arrays);
	free(b->comment);
	memset(b, 0, sizeof(*b));
}

static void gd_ss_str_rehash(struct gd_ss_builder *b, uint32_t new_cap)
{
	uint32_t *ht = xcalloc(new_cap, sizeof(uint32_t));
	uint32_t mask = new_cap - 1;
	for (uint32_t i = 0; i < b->nr_strings; i++) {
		uint32_t slot = gd_ss_fnv1a(b->strings[i], b->string_lens[i]) & mask;
		while (ht[slot] != 0)
			slot = (slot + 1) & mask;
		ht[slot] = i + 1;
	}
	free(b->str_ht);
	b->str_ht = ht;
	b->str_ht_cap = new_cap;
}

static int32_t gd_ss_builder_add_string(struct gd_ss_builder *b, const char *s, size_t len)
{
	if (!s || len == 0)
		return INT32_MAX;
	const void *nul = memchr(s, '\0', len);
	if (nul)
		len = (size_t)((const char *)nul - s);
	if (len == 0)
		return INT32_MAX;

	if (!b->str_ht)
		gd_ss_str_rehash(b, 256);
	else if ((b->nr_strings + 1) * 2 >= b->str_ht_cap)
		gd_ss_str_rehash(b, b->str_ht_cap * 2);

	uint32_t mask = b->str_ht_cap - 1;
	uint32_t slot = gd_ss_fnv1a(s, len) & mask;
	while (b->str_ht[slot] != 0) {
		uint32_t idx = b->str_ht[slot] - 1;
		if (b->string_lens[idx] == len && memcmp(b->strings[idx], s, len) == 0)
			return (int32_t)idx;
		slot = (slot + 1) & mask;
	}

	if (b->nr_strings >= b->cap_strings) {
		b->cap_strings = b->cap_strings ? b->cap_strings * 2 : 256;
		b->strings = xrealloc(b->strings, b->cap_strings * sizeof(char *));
		b->string_lens = xrealloc(b->string_lens, b->cap_strings * sizeof(size_t));
	}
	uint32_t idx = b->nr_strings++;
	char *copy = xmalloc(len + 1);
	memcpy(copy, s, len);
	copy[len] = '\0';
	b->strings[idx] = copy;
	b->string_lens[idx] = len;
	b->str_ht[slot] = idx + 1;
	return (int32_t)idx;
}

static uint32_t gd_ss_builder_add_val(struct gd_ss_builder *b, int32_t v)
{
	if (b->nr_vals >= b->cap_vals) {
		b->cap_vals *= 2;
		b->vals = xrealloc(b->vals, b->cap_vals * sizeof(int32_t));
	}
	uint32_t idx = b->nr_vals++;
	b->vals[idx] = v;
	return idx;
}

static int32_t gd_ss_builder_intern_schema(struct gd_ss_builder *b, const struct gd_ss_bschema *cand)
{
	for (uint32_t si = 0; si < b->nr_schemas; si++) {
		const struct gd_ss_bschema *ex = &b->schemas[si];
		if (ex->nr_fields != cand->nr_fields || ex->name_len != cand->name_len)
			continue;
		if (cand->name_len && memcmp(ex->name, cand->name, cand->name_len) != 0)
			continue;
		bool match = true;
		for (uint32_t fi = 0; fi < cand->nr_fields; fi++) {
			const struct gd_ss_bschema_field *ef = &ex->fields[fi];
			const struct gd_ss_bschema_field *cf = &cand->fields[fi];
			if (ef->type1 != cf->type1 || ef->type2 != cf->type2 || ef->name_len != cf->name_len) {
				match = false;
				break;
			}
			if (cf->name_len && memcmp(ef->name, cf->name, cf->name_len) != 0) {
				match = false;
				break;
			}
		}
		if (match)
			return (int32_t)si;
	}

	if (b->nr_schemas >= b->cap_schemas) {
		b->cap_schemas = b->cap_schemas ? b->cap_schemas * 2 : 32;
		b->schemas = xrealloc(b->schemas, b->cap_schemas * sizeof(*b->schemas));
	}
	uint32_t si = b->nr_schemas++;
	struct gd_ss_bschema *dst = &b->schemas[si];
	dst->name_len = cand->name_len;
	dst->name = xmalloc(cand->name_len + 1);
	if (cand->name_len)
		memcpy(dst->name, cand->name, cand->name_len);
	dst->name[cand->name_len] = '\0';
	dst->nr_fields = cand->nr_fields;
	dst->fields = xcalloc(max(cand->nr_fields, 1u), sizeof(*dst->fields));
	for (uint32_t fi = 0; fi < cand->nr_fields; fi++) {
		const struct gd_ss_bschema_field *cf = &cand->fields[fi];
		dst->fields[fi].type1 = cf->type1;
		dst->fields[fi].type2 = cf->type2;
		dst->fields[fi].name_len = cf->name_len;
		dst->fields[fi].name = xmalloc(cf->name_len + 1);
		if (cf->name_len)
			memcpy(dst->fields[fi].name, cf->name, cf->name_len);
		dst->fields[fi].name[cf->name_len] = '\0';
	}
	return (int32_t)si;
}

static void gd_ss_builder_reserve_objs(struct gd_ss_builder *b, uint32_t total_objs)
{
	if (total_objs <= b->nr_objs)
		return;
	if (total_objs > b->cap_objs) {
		b->cap_objs = total_objs;
		b->objs = xrealloc(b->objs, b->cap_objs * sizeof(*b->objs));
	}
	memset(b->objs + b->nr_objs, 0, (total_objs - b->nr_objs) * sizeof(*b->objs));
	b->nr_objs = total_objs;
}

static void gd_ss_builder_reserve_arrays(struct gd_ss_builder *b, uint32_t total_arrays)
{
	if (total_arrays <= b->nr_arrays)
		return;
	if (total_arrays > b->cap_arrays) {
		b->cap_arrays = total_arrays;
		b->arrays = xrealloc(b->arrays, b->cap_arrays * sizeof(*b->arrays));
	}
	memset(b->arrays + b->nr_arrays, 0, (total_arrays - b->nr_arrays) * sizeof(*b->arrays));
	b->nr_arrays = total_arrays;
}

static void gd_ss_builder_set_roots(struct gd_ss_builder *b, const int32_t *roots, uint32_t nr_roots)
{
	free(b->root_ids);
	b->nr_roots = nr_roots;
	b->root_ids = xcalloc(max(nr_roots, 1u), sizeof(int32_t));
	if (nr_roots > 0)
		memcpy(b->root_ids, roots, nr_roots * sizeof(int32_t));

	b->objs[0].struct_def = -1;
	b->objs[0].nr_vals = nr_roots;
	free(b->objs[0].val_idxs);
	b->objs[0].val_idxs = xcalloc(max(nr_roots, 1u), sizeof(uint32_t));
	for (uint32_t r = 0; r < nr_roots; r++)
		b->objs[0].val_idxs[r] = r;
}

static void gd_ss_builder_set_comment(struct gd_ss_builder *b, const uint8_t *comment, uint32_t len, bool has_comment)
{
	free(b->comment);
	b->comment = NULL;
	b->comment_len = 0;
	b->has_comment = has_comment;
	if (has_comment && len > 0 && comment) {
		b->comment = xmalloc(len);
		memcpy(b->comment, comment, len);
		b->comment_len = len;
	}
}

static bool gd_ss_builder_write_file(const struct gd_ss_builder *b, const char *path, bool keep_backup)
{
	struct x4ss_buf raw = {0};
	/* Header layout (98 bytes): "serialize_struct\0", version=1000, unknown=9, t[0..6], "serialize_struct\0" */
	x4ss_put_bytes(&raw, GDSS_INNER_MAGIC, 17);
	x4ss_put_u32(&raw, 1000u);
	x4ss_put_u32(&raw, 9u);
	for (int i = 0; i < 7 * 2; i++)
		x4ss_put_u32(&raw, 0u);
	x4ss_put_bytes(&raw, GDSS_INNER_MAGIC, 17);

	uint32_t c1_off = (uint32_t)raw.len;
	for (uint32_t i = 0; i < b->nr_objs; i++) {
		const struct gd_ss_bobj *o = &b->objs[i];
		x4ss_put_i32(&raw, o->struct_def);
		x4ss_put_u32(&raw, o->nr_vals);
		for (uint32_t f = 0; f < o->nr_vals; f++)
			x4ss_put_u32(&raw, o->val_idxs[f]);
	}

	uint32_t c2_off = (uint32_t)raw.len;
	for (uint32_t r = 0; r < b->nr_roots; r++) {
		x4ss_put_i32(&raw, AIN_STRUCT);
		x4ss_put_i32(&raw, AIN_STRUCT);
		x4ss_put_i32(&raw, b->root_ids[r]);
		x4ss_put_i32(&raw, 0);
		x4ss_put_u8(&raw, 0);
	}
	/* Sentinel terminator */
	x4ss_put_i32(&raw, 0);
	x4ss_put_i32(&raw, 0);
	x4ss_put_i32(&raw, 0);
	x4ss_put_i32(&raw, 0);
	x4ss_put_u8(&raw, 0);

	uint32_t c3_off = (uint32_t)raw.len;
	for (uint32_t i = 0; i < b->nr_strings; i++) {
		if (b->string_lens[i] > 0)
			x4ss_put_bytes(&raw, b->strings[i], b->string_lens[i]);
		x4ss_put_u8(&raw, 0);
	}

	uint32_t c4_off = (uint32_t)raw.len;
	for (uint32_t i = 0; i < b->nr_arrays; i++) {
		const struct gd_ss_barray *a = &b->arrays[i];
		x4ss_put_i32(&raw, a->rank > 0 ? a->rank : 1);
		x4ss_put_u32(&raw, a->total);
		x4ss_put_u32(&raw, 1u);
		x4ss_put_u32(&raw, a->total);
		x4ss_put_i32(&raw, a->elem_type);
		for (uint32_t k = 0; k < a->total; k++)
			x4ss_put_i32(&raw, a->elems[k]);
	}

	uint32_t c5_off = (uint32_t)raw.len;
	for (uint32_t i = 0; i < b->nr_vals; i++)
		x4ss_put_i32(&raw, b->vals[i]);
	x4ss_put_u32(&raw, b->nr_schemas);
	for (uint32_t si = 0; si < b->nr_schemas; si++) {
		const struct gd_ss_bschema *s = &b->schemas[si];
		if (s->name_len > 0)
			x4ss_put_bytes(&raw, s->name, s->name_len);
		x4ss_put_u8(&raw, 0);
		x4ss_put_u32(&raw, s->nr_fields);
		for (uint32_t fi = 0; fi < s->nr_fields; fi++) {
			const struct gd_ss_bschema_field *f = &s->fields[fi];
			x4ss_put_i32(&raw, f->type1);
			x4ss_put_i32(&raw, f->type2);
			if (f->name_len > 0)
				x4ss_put_bytes(&raw, f->name, f->name_len);
			x4ss_put_u8(&raw, 0);
		}
	}

	uint32_t c6_off = (uint32_t)raw.len;
	if (b->has_comment) {
		x4ss_put_u32(&raw, b->comment_len);
		if (b->comment_len > 0 && b->comment)
			x4ss_put_bytes(&raw, b->comment, b->comment_len);
	}

	uint32_t sec_off[7] = { 64u, c1_off, c2_off, c3_off, c4_off, c5_off, c6_off };
	uint32_t sec_cnt[7] = {
		b->nr_roots,
		b->nr_objs,
		b->nr_roots + 1u,
		b->nr_strings,
		b->nr_arrays,
		b->nr_vals,
		b->has_comment ? 1u : 0u
	};
	for (int i = 0; i < 7; i++) {
		gd_write_u32_le(raw.data + 25 + i * 8, sec_off[i]);
		gd_write_u32_le(raw.data + 29 + i * 8, sec_cnt[i]);
	}

	uLongf comp_cap = compressBound((uLong)raw.len);
	uint8_t *comp = xmalloc(comp_cap);
	if (compress2(comp, &comp_cap, raw.data, (uLong)raw.len, Z_BEST_SPEED) != Z_OK) {
		free(comp);
		free(raw.data);
		return false;
	}
	uint32_t ulen = (uint32_t)raw.len;
	free(raw.data);

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	if (!fp) {
		free(comp);
		return false;
	}
	uint8_t hdr[8];
	memcpy(hdr, GDSS_OUTER_MAGIC, 4);
	gd_write_u32_le(hdr + 4, ulen);
	bool ok = fwrite(hdr, 1, 8, fp) == 8;
	if (ok && comp_cap > 0)
		ok = fwrite(comp, 1, (size_t)comp_cap, fp) == (size_t)comp_cap;
	free(comp);
	if (!ok) {
		atomic_save_abort(fp, tmp_path);
		return false;
	}
	return atomic_save_commit(fp, path, tmp_path, keep_backup);
}

static int32_t gd_ss_wrap_to_ref(int32_t inner)
{
	switch (inner) {
	case AIN_INT:        return AIN_REF_INT;        /* 10 -> 18 */
	case AIN_FLOAT:      return AIN_REF_FLOAT;      /* 11 -> 19 */
	case AIN_STRING:     return AIN_REF_STRING;     /* 12 -> 20 */
	case AIN_STRUCT:     return AIN_REF_STRUCT;     /* 13 -> 21 */
	case AIN_BOOL:       return AIN_REF_BOOL;       /* 47 -> 51 */
	case AIN_LONG_INT:   return AIN_REF_LONG_INT;   /* 55 -> 59 */
	case AIN_DELEGATE:   return AIN_REF_DELEGATE;   /* 63 -> 67 */
	case AIN_ARRAY:      return AIN_REF_ARRAY;      /* 79 -> 80 */
	case AIN_WRAP:       return AIN_WRAP;           /* 82 -> 82 */
	case AIN_OPTION:     return AIN_REF_OPTION;     /* 86 -> 87 */
	case AIN_ENUM:       return AIN_REF_ENUM;       /* 92 -> 93 */
	case AIN_IFACE_WRAP: return AIN_IFACE;          /* 100 -> 89 */
	case AIN_FUNC_TYPE:  return AIN_REF_FUNC_TYPE;  /* 27 -> 31 */
	default:             return inner;
	}
}

static int32_t gd_ss_compute_type2(const struct ain_type *t)
{
	if (!t)
		return AIN_VOID;
	if (t->data == AIN_WRAP)
		return t->array_type ? gd_ss_wrap_to_ref((int32_t)t->array_type->data) : AIN_WRAP;
	if (t->data == AIN_OPTION)
		return t->array_type ? gd_ss_compute_type2(t->array_type) : AIN_VOID;
	return (int32_t)t->data;
}

static int32_t gd_ss_compute_array_elem_type(const struct ain_type *t)
{
	if (!t)
		return AIN_INT;
	switch (t->data) {
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_ARRAY_BOOL:
	case AIN_ARRAY_LONG_INT:
	case AIN_ARRAY_DELEGATE:
	case AIN_REF_ARRAY_INT:
	case AIN_REF_ARRAY_FLOAT:
	case AIN_REF_ARRAY_STRING:
	case AIN_REF_ARRAY_STRUCT:
	case AIN_REF_ARRAY_FUNC_TYPE:
	case AIN_REF_ARRAY_BOOL:
	case AIN_REF_ARRAY_LONG_INT:
	case AIN_REF_ARRAY_DELEGATE:
		return AIN_ARRAY;
	default:
		return gd_ss_compute_type2(t);
	}
}

static bool gd_ss_type_is_struct_or_iface(int32_t type1, int32_t type2)
{
	int32_t eff = (type1 == AIN_WRAP || type1 == AIN_OPTION) ? type2 : type1;
	switch (eff) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		return true;
	default:
		return false;
	}
}

static const char *gd_ss_member_save_name(const struct ain *a, const struct ain_variable *v)
{
	if (a && a->version >= 12 && v->name2 != NULL)
		return v->name2;
	return v->name ? v->name : "";
}

static bool gd_ss_vm_is_page_var(const struct ain_type *t)
{
	if (!t)
		return false;
	switch (t->data) {
	case AIN_REF_INT:
	case AIN_REF_FLOAT:
	case AIN_REF_BOOL:
	case AIN_REF_LONG_INT:
	case AIN_REF_ENUM:
	case AIN_REF_OPTION:
		return true;
	case AIN_WRAP:
		if (!t->array_type)
			return false;
		if (t->array_type->data == AIN_STRING || t->array_type->data == AIN_DELEGATE
				|| t->array_type->data == AIN_IFACE || t->array_type->data == AIN_IFACE_WRAP)
			return false;
		return ain_type_slot_width(t) == 2;
	default:
		return false;
	}
}

struct gd_ss_vm_ctx {
	int *slot_to_c1;
	int *c1_to_slot;
	uint32_t nr_c1;
	uint32_t cap_c1;

	int *slot_to_c4;
	int *c4_to_slot;
	uint32_t nr_c4;
	uint32_t cap_c4;

	bool unsupported;
};

static int32_t gd_ss_vm_reg_struct(struct gd_ss_vm_ctx *ctx, int slot)
{
	if (slot < 0 || !page_index_valid(slot))
		return -1;
	struct page *p = heap[slot].page;
	if (!p || p->type != STRUCT_PAGE)
		return -1;
	if (ctx->slot_to_c1[slot] > 0)
		return (int32_t)ctx->slot_to_c1[slot];
	if (ctx->nr_c1 >= ctx->cap_c1) {
		ctx->cap_c1 = ctx->cap_c1 ? ctx->cap_c1 * 2 : 128;
		ctx->c1_to_slot = xrealloc(ctx->c1_to_slot, ctx->cap_c1 * sizeof(int));
	}
	uint32_t id = ctx->nr_c1++;
	ctx->slot_to_c1[slot] = (int)id;
	ctx->c1_to_slot[id] = slot;
	return (int32_t)id;
}

static int32_t gd_ss_vm_reg_array(struct gd_ss_vm_ctx *ctx, int slot)
{
	if (slot < 0 || !page_index_valid(slot))
		return -1;
	struct page *p = heap[slot].page;
	if (!p || p->type != ARRAY_PAGE)
		return -1;
	if (ctx->slot_to_c4[slot] > 0)
		return (int32_t)ctx->slot_to_c4[slot];
	if (p->array.rank > 1) {
		ctx->unsupported = true;
		return -1;
	}
	if (ctx->nr_c4 >= ctx->cap_c4) {
		ctx->cap_c4 = ctx->cap_c4 ? ctx->cap_c4 * 2 : 128;
		ctx->c4_to_slot = xrealloc(ctx->c4_to_slot, ctx->cap_c4 * sizeof(int));
	}
	uint32_t id = ctx->nr_c4++;
	ctx->slot_to_c4[slot] = (int)id;
	ctx->c4_to_slot[id] = slot;
	return (int32_t)id;
}

static void gd_ss_vm_collect_value(struct gd_ss_vm_ctx *ctx, const union vm_value *vals, const struct ain_type *t)
{
	if (!t || ctx->unsupported)
		return;
	if (gd_ss_vm_is_page_var(t)) {
		ctx->unsupported = true;
		return;
	}
	if (t->data == AIN_OPTION) {
		int w = ain_type_slot_width(t);
		if (vals[w - 1].i == 0 && t->array_type)
			gd_ss_vm_collect_value(ctx, vals, t->array_type);
		return;
	}
	const struct ain_type *eff = t;
	while (eff->data == AIN_WRAP && eff->array_type)
		eff = eff->array_type;
	switch (eff->data) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		gd_ss_vm_reg_struct(ctx, vals[0].i);
		break;
	case AIN_ARRAY:
	case AIN_REF_ARRAY:
	case AIN_ARRAY_TYPE:
	case AIN_REF_ARRAY_TYPE:
		gd_ss_vm_reg_array(ctx, vals[0].i);
		break;
	default:
		break;
	}
}

static int32_t gd_ss_vm_emit_value(struct gd_ss_vm_ctx *ctx, struct gd_ss_builder *b,
		const union vm_value *vals, const struct ain_type *t)
{
	if (!t)
		return 0;
	const struct ain_type *eff = t;
	while (eff->data == AIN_WRAP && eff->array_type)
		eff = eff->array_type;
	switch (eff->data) {
	case AIN_VOID:
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		return 0;
	case AIN_STRING:
	case AIN_REF_STRING:
		if (string_index_valid(vals[0].i)) {
			struct string *s = heap_get_string(vals[0].i);
			return gd_ss_builder_add_string(b, s->text, (size_t)s->size);
		}
		return INT32_MAX;
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		return (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_c1[vals[0].i] > 0)
			? (int32_t)ctx->slot_to_c1[vals[0].i] : -1;
	case AIN_ARRAY:
	case AIN_REF_ARRAY:
	case AIN_ARRAY_TYPE:
	case AIN_REF_ARRAY_TYPE:
		return (vals[0].i >= 0 && page_index_valid(vals[0].i) && ctx->slot_to_c4[vals[0].i] > 0)
			? (int32_t)ctx->slot_to_c4[vals[0].i] : -1;
	default:
		return vals[0].i;
	}
}

static void gd_ss_read_existing_comment(const char *path,
		uint8_t **comment_out, uint32_t *len_out, bool *has_comment_out)
{
	*comment_out = NULL;
	*len_out = 0;
	*has_comment_out = false;

	uint8_t *gd_raw = NULL;
	uint32_t gd_ulen = 0;
	if (gd_ss_decompress_file(path, &gd_raw, &gd_ulen)) {
		struct gd_ss_parsed parsed;
		if (gd_ss_parse(gd_raw, gd_ulen, &parsed)) {
			if (parsed.has_comment) {
				*has_comment_out = true;
				if (parsed.comment_len > 0) {
					bool src_is_gbk = gd_ss_detect_is_gbk(&parsed);
					size_t out_len = 0;
					char *conv = save_transcode_cstr(parsed.comment, parsed.comment_len,
							src_is_gbk, sys4_text_is_gbk, &out_len);
					*comment_out = (uint8_t *)conv;
					*len_out = (uint32_t)out_len;
				}
			}
			gd_ss_free_parsed(&parsed);
			return;
		}
		free(gd_raw);
	}

	uint8_t *cbuf = NULL;
	uint32_t clen = 0;
	if (x4ss_read_existing_parts(path, &cbuf, &clen, NULL, NULL, NULL)) {
		*comment_out = cbuf;
		*len_out = clen;
		*has_comment_out = (clen > 0);
	}
}

static int serialize_gd_structs(const char *filename, struct page *struct_page_list, bool save_folder, bool *fallback_out)
{
	*fallback_out = false;
	int nr_roots = v12_array_length(struct_page_list);
	struct gd_ss_vm_ctx ctx = {
		.slot_to_c1 = xcalloc((size_t)max(heap_size, 1), sizeof(int)),
		.c1_to_slot = xcalloc(128, sizeof(int)),
		.nr_c1 = 1,
		.cap_c1 = 128,
		.slot_to_c4 = xcalloc((size_t)max(heap_size, 1), sizeof(int)),
		.c4_to_slot = xcalloc(128, sizeof(int)),
		.nr_c4 = 1,
		.cap_c4 = 128,
		.unsupported = false,
	};

	int32_t *root_ids = xcalloc((size_t)max(nr_roots, 1), sizeof(int32_t));
	for (int i = 0; i < nr_roots; i++)
		root_ids[i] = gd_ss_vm_reg_struct(&ctx, struct_page_list->values[i].i);

	uint32_t next_c1 = 1;
	uint32_t next_c4 = 1;
	while ((next_c1 < ctx.nr_c1 || next_c4 < ctx.nr_c4) && !ctx.unsupported) {
		while (next_c1 < ctx.nr_c1 && !ctx.unsupported) {
			struct page *p = heap[ctx.c1_to_slot[next_c1++]].page;
			struct ain_struct *st = &ain->structures[p->index];
			for (int m = 0; m < st->nr_members && !ctx.unsupported; m++) {
				const struct ain_type *mt = &st->members[m].type;
				const char *mname = gd_ss_member_save_name(ain, &st->members[m]);
				if (mt->data == AIN_VOID || mt->data == AIN_DELEGATE || mt->data == AIN_REF_DELEGATE)
					continue;
				if (!mname[0] || !strcmp(mname, "<vtable>"))
					continue;
				if (m >= p->nr_vars)
					continue;
				if (mt->data == AIN_OPTION) {
					int w = ain_type_slot_width(mt);
					if (m + w <= p->nr_vars)
						gd_ss_vm_collect_value(&ctx, &p->values[m], mt);
					if (w > 1)
						m += w - 1;
				} else {
					gd_ss_vm_collect_value(&ctx, &p->values[m], mt);
				}
			}
		}
		while (next_c4 < ctx.nr_c4 && !ctx.unsupported) {
			struct page *p = heap[ctx.c4_to_slot[next_c4++]].page;
			struct ain_type et = p->a_type == AIN_ARRAY
				? p->array.element_type
				: (struct ain_type){ .data = array_type(p->a_type), .struc = p->array.struct_type };
			int32_t ec = gd_ss_compute_array_elem_type(&et);
			if (ec == AIN_IFACE || ec == AIN_IFACE_WRAP)
				continue; /* PC writes -1 placeholders for AIN_IFACE arrays */
			if (et.data == AIN_OPTION) {
				ctx.unsupported = true;
				break;
			}
			int w = ain_type_slot_width(&et);
			int len = p->nr_vars / w;
			for (int k = 0; k < len && !ctx.unsupported; k++)
				gd_ss_vm_collect_value(&ctx, &p->values[k * w], &et);
		}
	}

	if (ctx.unsupported) {
		free(root_ids);
		free(ctx.slot_to_c1);
		free(ctx.c1_to_slot);
		free(ctx.slot_to_c4);
		free(ctx.c4_to_slot);
		*fallback_out = true;
		return 0;
	}

	struct gd_ss_builder b;
	gd_ss_builder_init(&b);
	gd_ss_builder_reserve_objs(&b, ctx.nr_c1);
	gd_ss_builder_reserve_arrays(&b, ctx.nr_c4);
	gd_ss_builder_set_roots(&b, root_ids, (uint32_t)nr_roots);
	free(root_ids);

	int *sno_to_schema = xmalloc((size_t)max(ain->nr_structures, 1) * sizeof(int));
	for (int i = 0; i < ain->nr_structures; i++)
		sno_to_schema[i] = -1;

	for (uint32_t id = 1; id < ctx.nr_c1; id++) {
		struct page *p = heap[ctx.c1_to_slot[id]].page;
		struct ain_struct *st = &ain->structures[p->index];

		if (sno_to_schema[p->index] < 0) {
			uint32_t nf = (uint32_t)st->nr_members;
			struct gd_ss_bschema_field *fields = xcalloc((size_t)max(st->nr_members, 1), sizeof(*fields));
			for (int m = 0; m < st->nr_members; m++) {
				const char *mname = gd_ss_member_save_name(ain, &st->members[m]);
				const struct ain_type *mt = &st->members[m].type;
				fields[m].type1 = (int32_t)mt->data;
				fields[m].type2 = gd_ss_compute_type2(mt);
				fields[m].name = (char *)mname;
				fields[m].name_len = strlen(mname);
			}
			struct gd_ss_bschema cand = {
				.name = st->name ? st->name : "",
				.name_len = st->name ? strlen(st->name) : 0,
				.nr_fields = nf,
				.fields = fields,
			};
			sno_to_schema[p->index] = gd_ss_builder_intern_schema(&b, &cand);
			free(fields);
		}

		int32_t schema_id = sno_to_schema[p->index];
		uint32_t nf = b.schemas[schema_id].nr_fields;
		b.objs[id].struct_def = schema_id;
		b.objs[id].nr_vals = nf;
		b.objs[id].val_idxs = xcalloc(max(nf, 1u), sizeof(uint32_t));

		for (int m = 0; m < st->nr_members && (uint32_t)m < nf; m++) {
			const char *mname = gd_ss_member_save_name(ain, &st->members[m]);
			const struct ain_type *mt = &st->members[m].type;
			int32_t ft1 = (int32_t)mt->data;
			int32_t ft2 = gd_ss_compute_type2(mt);

			if (mt->data == AIN_VOID || mt->data == AIN_DELEGATE || mt->data == AIN_REF_DELEGATE
					|| !strcmp(mname, "<vtable>")) {
				b.objs[id].val_idxs[m] = 0; /* vals[0] = 0 */
				continue;
			}
			if (!mname[0]) {
				if (gd_ss_type_is_struct_or_iface(ft1, ft2))
					b.objs[id].val_idxs[m] = gd_ss_builder_add_val(&b, -1);
				else
					b.objs[id].val_idxs[m] = 0;
				continue;
			}
			if (mt->data == AIN_OPTION) {
				int w = ain_type_slot_width(mt);
				bool is_none = (m + w <= p->nr_vars) ? (p->values[m + w - 1].i != 0) : true;
				int32_t iv = (is_none || !mt->array_type || m >= p->nr_vars)
					? -1
					: gd_ss_vm_emit_value(&ctx, &b, &p->values[m], mt->array_type);
				b.objs[id].val_idxs[m] = gd_ss_builder_add_val(&b, iv);
				for (int k = 1; k < w && m + k < st->nr_members && (uint32_t)(m + k) < nf; k++) {
					int32_t sv = (k == w - 1)
						? (is_none ? 1 : 0)
						: ((is_none || m + k >= p->nr_vars) ? -1 : p->values[m + k].i);
					b.objs[id].val_idxs[m + k] = gd_ss_builder_add_val(&b, sv);
				}
				if (w > 1)
					m += w - 1;
				continue;
			}
			int32_t v = (m < p->nr_vars)
				? gd_ss_vm_emit_value(&ctx, &b, &p->values[m], mt)
				: (gd_ss_type_is_struct_or_iface(ft1, ft2) ? -1 : 0);
			b.objs[id].val_idxs[m] = gd_ss_builder_add_val(&b, v);
		}
	}
	free(sno_to_schema);

	for (uint32_t aid = 1; aid < ctx.nr_c4; aid++) {
		struct page *p = heap[ctx.c4_to_slot[aid]].page;
		struct ain_type et = p->a_type == AIN_ARRAY
			? p->array.element_type
			: (struct ain_type){ .data = array_type(p->a_type), .struc = p->array.struct_type };
		int32_t ec = gd_ss_compute_array_elem_type(&et);
		int w = ain_type_slot_width(&et);
		int len = p->nr_vars / w;

		b.arrays[aid].rank = 1;
		b.arrays[aid].total = (uint32_t)len;
		b.arrays[aid].elem_type = ec;
		b.arrays[aid].elems = xcalloc((size_t)max(len, 1), sizeof(int32_t));
		if (ec == AIN_IFACE || ec == AIN_IFACE_WRAP) {
			for (int k = 0; k < len; k++)
				b.arrays[aid].elems[k] = -1;
		} else {
			for (int k = 0; k < len; k++)
				b.arrays[aid].elems[k] = gd_ss_vm_emit_value(&ctx, &b, &p->values[k * w], &et);
		}
	}

	free(ctx.slot_to_c1);
	free(ctx.c1_to_slot);
	free(ctx.slot_to_c4);
	free(ctx.c4_to_slot);

	char *path = resolve_struct_save_path(filename, save_folder);
	uint8_t *existing_comment = NULL;
	uint32_t existing_comment_len = 0;
	bool has_comment = false;
	gd_ss_read_existing_comment(path, &existing_comment, &existing_comment_len, &has_comment);
	gd_ss_builder_set_comment(&b, existing_comment, existing_comment_len, has_comment);
	free(existing_comment);

	bool ok = gd_ss_builder_write_file(&b, path, is_critical_global_save(path));
	free(path);
	gd_ss_builder_free(&b);
	return ok ? 1 : 0;
}

static int serialize_x4ss_structs(const char *filename, struct page *struct_page_list, bool save_folder)
{
	int nr_roots = v12_array_length(struct_page_list);
	struct x4ss_save_ctx ctx = {
		.slot_to_id = xcalloc((size_t)max(heap_size, 1), sizeof(int)),
		.id_to_slot = xcalloc(256, sizeof(int)),
		.nr_objs = 0,
		.cap_objs = 256,
	};

	uint32_t *root_ids = xcalloc((size_t)max(nr_roots, 1), sizeof(uint32_t));
	for (int i = 0; i < nr_roots; i++)
		root_ids[i] = x4ss_register_page(&ctx, struct_page_list->values[i].i);

	/* BFS over all reachable STRUCT_PAGE and ARRAY_PAGE objects */
	for (int id = 1; id <= ctx.nr_objs; id++) {
		int slot = ctx.id_to_slot[id];
		struct page *p = heap[slot].page;
		if (!p)
			continue;
		if (p->type == STRUCT_PAGE) {
			struct ain_struct *st = &ain->structures[p->index];
			for (int m = 0; m < st->nr_members; m++) {
				if (st->members[m].type.data == AIN_VOID)
					continue;
				if (st->members[m].name && !strcmp(st->members[m].name, "<vtable>"))
					continue;
				x4ss_collect_value(&ctx, &p->values[m], &st->members[m].type);
			}
		} else if (p->type == ARRAY_PAGE) {
			struct ain_type et = p->a_type == AIN_ARRAY
				? p->array.element_type
				: (struct ain_type){ .data = array_type(p->a_type), .struc = p->array.struct_type };
			int w = ain_type_slot_width(&et);
			int len = p->nr_vars / w;
			for (int k = 0; k < len; k++)
				x4ss_collect_value(&ctx, &p->values[k * w], &et);
		}
	}

	struct x4ss_buf payload = {0};
	x4ss_put_u32(&payload, (uint32_t)nr_roots);
	for (int i = 0; i < nr_roots; i++)
		x4ss_put_u32(&payload, root_ids[i]);
	x4ss_put_u32(&payload, (uint32_t)ctx.nr_objs);

	/* Pass 1: object headers so cycles & forward refs allocate first on load */
	for (int id = 1; id <= ctx.nr_objs; id++) {
		int slot = ctx.id_to_slot[id];
		struct page *p = heap[slot].page;
		if (!p) {
			x4ss_put_u8(&payload, X4SS_OBJ_NULL);
		} else if (p->type == STRUCT_PAGE) {
			x4ss_put_u8(&payload, X4SS_OBJ_STRUCT);
			x4ss_put_cstr(&payload, ain->structures[p->index].name);
		} else {
			struct ain_type et = p->a_type == AIN_ARRAY
				? p->array.element_type
				: (struct ain_type){ .data = array_type(p->a_type), .struc = p->array.struct_type };
			int w = ain_type_slot_width(&et);
			int len = p->nr_vars / w;
			x4ss_put_u8(&payload, X4SS_OBJ_ARRAY);
			x4ss_put_i32(&payload, p->a_type);
			x4ss_put_i32(&payload, p->array.rank);
			x4ss_write_ain_type(&payload, &et);
			x4ss_put_u32(&payload, (uint32_t)len);
		}
	}

	/* Pass 2: object bodies */
	for (int id = 1; id <= ctx.nr_objs; id++) {
		int slot = ctx.id_to_slot[id];
		struct page *p = heap[slot].page;
		if (!p)
			continue;
		if (p->type == STRUCT_PAGE) {
			struct ain_struct *st = &ain->structures[p->index];
			uint32_t nr_fields = 0;
			for (int m = 0; m < st->nr_members; m++) {
				enum ain_data_type dt = st->members[m].type.data;
				if (dt == AIN_VOID || dt == AIN_DELEGATE || dt == AIN_REF_DELEGATE)
					continue;
				if (st->members[m].name && !strcmp(st->members[m].name, "<vtable>"))
					continue;
				nr_fields++;
			}
			x4ss_put_u32(&payload, nr_fields);
			for (int m = 0; m < st->nr_members; m++) {
				enum ain_data_type dt = st->members[m].type.data;
				if (dt == AIN_VOID || dt == AIN_DELEGATE || dt == AIN_REF_DELEGATE)
					continue;
				if (st->members[m].name && !strcmp(st->members[m].name, "<vtable>"))
					continue;
				x4ss_put_cstr(&payload, st->members[m].name);
				x4ss_write_value(&ctx, &payload, &p->values[m], &st->members[m].type);
			}
		} else if (p->type == ARRAY_PAGE) {
			struct ain_type et = p->a_type == AIN_ARRAY
				? p->array.element_type
				: (struct ain_type){ .data = array_type(p->a_type), .struc = p->array.struct_type };
			int w = ain_type_slot_width(&et);
			int len = p->nr_vars / w;
			for (int k = 0; k < len; k++)
				x4ss_write_value(&ctx, &payload, &p->values[k * w], &et);
		}
	}

	free(root_ids);
	free(ctx.slot_to_id);
	free(ctx.id_to_slot);

	uLongf comp_cap = compressBound((uLong)payload.len);
	uint8_t *comp = xmalloc(comp_cap);
	if (compress2(comp, &comp_cap, payload.data, (uLong)payload.len, Z_BEST_SPEED) != Z_OK) {
		free(comp);
		free(payload.data);
		return 0;
	}

	char *path = resolve_struct_save_path(filename, save_folder);
	uint8_t *existing_comment = NULL;
	uint32_t existing_comment_len = 0;
	bool has_comment = false;
	gd_ss_read_existing_comment(path, &existing_comment, &existing_comment_len, &has_comment);

	bool ok = x4ss_write_container(path, existing_comment, existing_comment_len,
			(uint32_t)payload.len, comp, (uint32_t)comp_cap);

	free(existing_comment);
	free(path);
	free(comp);
	free(payload.data);
	return ok ? 1 : 0;
}

int serialize_structs(const char *filename, struct page *struct_page_list, bool save_folder)
{
	if (!filename || !struct_page_list || struct_page_list->type != ARRAY_PAGE)
		return 0;

	int nr_roots = v12_array_length(struct_page_list);
	for (int i = 0; i < nr_roots; i++) {
		int s = struct_page_list->values[i].i;
		if (!page_index_valid(s) || !heap[s].page || heap[s].page->type != STRUCT_PAGE) {
			WARNING("SerializeStruct: invalid root page %d at index %d", s, i);
			return 0;
		}
	}

	bool fallback = false;
	int gd_ret = serialize_gd_structs(filename, struct_page_list, save_folder, &fallback);
	if (!fallback)
		return gd_ret;
	return serialize_x4ss_structs(filename, struct_page_list, save_folder);
}

struct x4ss_obj_meta {
	uint8_t kind;
	int struct_no;
	struct ain_type elem_type;
	uint32_t elem_count;
};

int deserialize_structs(const char *filename, struct page *struct_page_list, bool save_folder)
{
	if (!filename || !struct_page_list || struct_page_list->type != ARRAY_PAGE)
		return 0;

	int nr_caller_roots = v12_array_length(struct_page_list);
	for (int i = 0; i < nr_caller_roots; i++) {
		int s = struct_page_list->values[i].i;
		if (!page_index_valid(s) || !heap[s].page || heap[s].page->type != STRUCT_PAGE) {
			WARNING("DeserializeStruct: invalid root page %d at index %d", s, i);
			return 0;
		}
	}

	char *path = resolve_struct_save_path(filename, save_folder);
	uint8_t *gd_raw = NULL;
	uint32_t gd_ulen = 0;
	if (gd_ss_decompress_file(path, &gd_raw, &gd_ulen)) {
		bool gd_ok = deserialize_gd_structs(path, filename, save_folder, struct_page_list, gd_raw, gd_ulen);
		free(path);
		return gd_ok ? 1 : 0;
	}

	uint32_t uncomp_len = 0, comp_len = 0;
	uint8_t *comp = NULL;
	if (!x4ss_read_existing_parts(path, NULL, NULL, &uncomp_len, &comp, &comp_len) ||
	    uncomp_len == 0 || uncomp_len > 256u * 1024u * 1024u ||
	    comp_len == 0 || comp_len > 256u * 1024u * 1024u || !comp) {
		/* Try .bak recovery if critical save */
		char *bak_path = make_backup_path(path);
		free(comp);
		comp = NULL;
		if (file_exists(bak_path) && gd_ss_decompress_file(bak_path, &gd_raw, &gd_ulen)) {
			WARNING("Recovered %s from backup %s", display_utf0(path), display_utf0(bak_path));
			file_copy(bak_path, path);
			free(bak_path);
			bool gd_ok = deserialize_gd_structs(path, filename, save_folder, struct_page_list, gd_raw, gd_ulen);
			free(path);
			return gd_ok ? 1 : 0;
		}
		if (!file_exists(bak_path) ||
		    !x4ss_read_existing_parts(bak_path, NULL, NULL, &uncomp_len, &comp, &comp_len) ||
		    uncomp_len == 0 || uncomp_len > 256u * 1024u * 1024u ||
		    comp_len == 0 || comp_len > 256u * 1024u * 1024u || !comp) {
			free(comp);
			free(bak_path);
			free(path);
			return 0;
		}
		WARNING("Recovered %s from backup %s", display_utf0(path), display_utf0(bak_path));
		file_copy(bak_path, path);
		free(bak_path);
	}
	free(path);

	uint8_t *raw = xmalloc(uncomp_len);
	uLongf dst_len = (uLongf)uncomp_len;
	if (uncompress(raw, &dst_len, comp, (uLong)comp_len) != Z_OK || dst_len != uncomp_len) {
		free(raw);
		free(comp);
		WARNING("DeserializeStruct: decompression failed for %s", display_sjis0(filename));
		return 0;
	}
	free(comp);

	struct x4ss_reader r = { .data = raw, .len = uncomp_len, .pos = 0, .error = false };
	uint32_t nr_saved_roots = x4ss_get_u32(&r);
	if (r.error || nr_saved_roots > 1024u * 1024u ||
	    nr_saved_roots > (r.len - r.pos) / 4u || (int)nr_saved_roots != nr_caller_roots) {
		free(raw);
		WARNING("DeserializeStruct: root count mismatch (%u vs %d)", nr_saved_roots, nr_caller_roots);
		return 0;
	}
	uint32_t *root_ids = xcalloc(max(nr_saved_roots, 1u), sizeof(uint32_t));
	for (uint32_t i = 0; i < nr_saved_roots; i++)
		root_ids[i] = x4ss_get_u32(&r);

	uint32_t nr_objs = x4ss_get_u32(&r);
	if (r.error || nr_objs > 16u * 1024u * 1024u || nr_objs > r.len - r.pos) {
		free(root_ids);
		free(raw);
		return 0;
	}

	struct x4ss_load_ctx ctx = {
		.nr_objs = nr_objs,
		.id_to_slot = xcalloc((size_t)nr_objs + 1, sizeof(int)),
		.is_root = xcalloc((size_t)nr_objs + 1, sizeof(bool)),
	};
	for (uint32_t id = 0; id <= nr_objs; id++)
		ctx.id_to_slot[id] = -1;
	for (uint32_t i = 0; i < nr_saved_roots; i++) {
		uint32_t rid = root_ids[i];
		if (rid > 0 && rid <= nr_objs) {
			ctx.id_to_slot[rid] = struct_page_list->values[i].i;
			ctx.is_root[rid] = true;
		}
	}

	struct x4ss_obj_meta *metas = xcalloc((size_t)nr_objs + 1, sizeof(*metas));

	/* Pass 1: read headers and pre-allocate non-root pages */
	for (uint32_t id = 1; id <= nr_objs && !r.error; id++) {
		uint8_t kind = x4ss_get_u8(&r);
		metas[id].kind = kind;
		if (kind == X4SS_OBJ_NULL) {
			if (!ctx.is_root[id]) {
				int slot = heap_alloc_slot(VM_PAGE);
				heap_set_page(slot, NULL);
				ctx.id_to_slot[id] = slot;
			}
		} else if (kind == X4SS_OBJ_STRUCT) {
			char *sname = x4ss_get_cstr(&r, NULL);
			int sno = sname ? save_find_struct(sname) : -1;
			free(sname);
			metas[id].struct_no = sno;
			if (!ctx.is_root[id] && sno >= 0) {
				ctx.id_to_slot[id] = alloc_struct(sno);
			}
		} else if (kind == X4SS_OBJ_ARRAY) {
			int32_t a_type = x4ss_get_i32(&r);
			int32_t rank = x4ss_get_i32(&r);
			struct ain_type et = x4ss_read_ain_type(&r);
			uint32_t count = x4ss_get_u32(&r);
			int w = ain_type_slot_width(&et);
			if (r.error || w <= 0 || count > 16u * 1024u * 1024u ||
			    count > (uint32_t)(INT_MAX / w)) {
				r.error = true;
				break;
			}
			metas[id].elem_type = et;
			metas[id].elem_count = count;
			if (!ctx.is_root[id]) {
				struct page *ap = alloc_page(ARRAY_PAGE, a_type, (int)count * w);
				ap->array.rank = rank > 0 ? rank : 1;
				ap->array.struct_type = et.struc;
				ap->array.element_type = et;
				for (uint32_t k = 0; k < count; k++)
					variable_init_typed(&ap->values[k * w], &et, false, false);
				ctx.id_to_slot[id] = heap_alloc_page(ap);
			}
		} else {
			r.error = true;
		}
	}

	/* Pass 2: populate bodies */
	for (uint32_t id = 1; id <= nr_objs && !r.error; id++) {
		uint8_t kind = metas[id].kind;
		int slot = ctx.id_to_slot[id];
		if (kind == X4SS_OBJ_NULL) {
			continue;
		} else if (kind == X4SS_OBJ_STRUCT) {
			uint32_t nr_fields = x4ss_get_u32(&r);
			if (r.error || nr_fields > 1024u * 1024u || nr_fields > (r.len - r.pos) / 5u) {
				r.error = true;
				break;
			}
			int sno = metas[id].struct_no;
			struct page *sp = (slot >= 0 && page_index_valid(slot)) ? heap[slot].page : NULL;
			if (sp && sp->type == STRUCT_PAGE && ctx.is_root[id]) {
				/* Clear any existing delegate listeners on root struct */
				struct ain_struct *rst = &ain->structures[sp->index];
				for (int m = 0; m < rst->nr_members && m < sp->nr_vars; m++) {
					if (rst->members[m].type.data == AIN_DELEGATE) {
						int dslot = sp->values[m].i;
						if (page_index_valid(dslot) && heap[dslot].page)
							heap_set_page(dslot, delegate_clear(heap[dslot].page));
					}
				}
			}
			for (uint32_t f = 0; f < nr_fields && !r.error; f++) {
				char *fname = x4ss_get_cstr(&r, NULL);
				int m_idx = -1;
				if (sp && sp->type == STRUCT_PAGE && sno >= 0 && fname)
					m_idx = struct_member_index(&ain->structures[sp->index], fname);
				free(fname);
				if (m_idx >= 0) {
					struct ain_struct *st = &ain->structures[sp->index];
					int mw = ain_type_slot_width(&st->members[m_idx].type);
					if (mw > 0 && m_idx + mw <= sp->nr_vars)
						x4ss_read_value(&ctx, &r, &sp->values[m_idx], &st->members[m_idx].type, true);
					else
						x4ss_skip_value(&r);
				} else {
					x4ss_skip_value(&r);
				}
			}
		} else if (kind == X4SS_OBJ_ARRAY) {
			struct page *ap = (slot >= 0 && page_index_valid(slot)) ? heap[slot].page : NULL;
			int w = ain_type_slot_width(&metas[id].elem_type);
			for (uint32_t k = 0; k < metas[id].elem_count && !r.error; k++) {
				if (ap && ap->type == ARRAY_PAGE && w > 0 &&
				    (size_t)(k + 1) * (size_t)w <= (size_t)ap->nr_vars) {
					x4ss_read_value(&ctx, &r, &ap->values[k * w], &metas[id].elem_type, true);
				} else {
					x4ss_skip_value(&r);
				}
			}
		}
	}

	bool ok = !r.error;

	/* Release temporary table reference on all newly allocated non-root pages */
	for (uint32_t id = 1; id <= nr_objs; id++) {
		if (!ctx.is_root[id] && ctx.id_to_slot[id] >= 0)
			exit_unref(ctx.id_to_slot[id]);
	}

	free(metas);
	free(ctx.is_root);
	free(ctx.id_to_slot);
	free(root_ids);
	free(raw);

	if (ok && save_folder && is_game_config_save(filename))
		modifier_on_config_loaded();

	return ok ? 1 : 0;
}

static bool gd_ss_update_comment(const char *path, const struct string *comment)
{
	uint8_t *gd_raw = NULL;
	uint32_t gd_ulen = 0;
	if (!gd_ss_decompress_file(path, &gd_raw, &gd_ulen))
		return false;

	struct gd_ss_parsed parsed;
	if (!gd_ss_parse(gd_raw, gd_ulen, &parsed)) {
		free(gd_raw);
		return false;
	}
	uint32_t c6_off = gd_read_u32_le(gd_raw + 73);
	if (c6_off < 81 || c6_off > gd_ulen) {
		gd_ss_free_parsed(&parsed);
		return false;
	}

	bool dst_is_gbk = gd_ss_detect_is_gbk(&parsed);
	size_t c6_len = 0;
	char *c6_buf = save_transcode_cstr(comment ? comment->text : "",
			comment ? (size_t)comment->size : 0, sys4_text_is_gbk, dst_is_gbk, &c6_len);
	if (c6_len > UINT32_MAX - 4u - c6_off) {
		free(c6_buf);
		gd_ss_free_parsed(&parsed);
		return false;
	}

	uint32_t new_ulen = c6_off + 4u + (uint32_t)c6_len;
	uint8_t *new_raw = xmalloc(new_ulen);
	memcpy(new_raw, gd_raw, c6_off);
	gd_write_u32_le(new_raw + 77, 1); /* sec_cnt[6] = has_comment */
	gd_write_u32_le(new_raw + c6_off, (uint32_t)c6_len);
	if (c6_len > 0)
		memcpy(new_raw + c6_off + 4u, c6_buf, c6_len);

	free(c6_buf);
	gd_ss_free_parsed(&parsed);

	uLongf comp_cap = compressBound((uLong)new_ulen);
	uint8_t *comp = xmalloc(comp_cap);
	if (compress2(comp, &comp_cap, new_raw, (uLong)new_ulen, Z_BEST_SPEED) != Z_OK) {
		free(comp);
		free(new_raw);
		return false;
	}
	free(new_raw);

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	if (!fp) {
		free(comp);
		return false;
	}
	uint8_t hdr[8];
	memcpy(hdr, GDSS_OUTER_MAGIC, 4);
	gd_write_u32_le(hdr + 4, new_ulen);
	bool ok = fwrite(hdr, 1, 8, fp) == 8;
	if (ok && comp_cap > 0)
		ok = fwrite(comp, 1, (size_t)comp_cap, fp) == (size_t)comp_cap;
	free(comp);
	if (!ok) {
		atomic_save_abort(fp, tmp_path);
		return false;
	}
	return atomic_save_commit(fp, path, tmp_path, is_critical_global_save(path));
}

int write_serialize_struct_comment(const char *filename, const struct string *comment, bool save_folder)
{
	if (!filename)
		return 0;
	char *path = resolve_struct_save_path(filename, save_folder);
	uint32_t uncomp_len = 0, comp_len = 0;
	uint8_t *comp = NULL;
	bool has_x4ss = x4ss_read_existing_parts(path, NULL, NULL, &uncomp_len, &comp, &comp_len);
	if (!has_x4ss && gd_ss_update_comment(path, comment)) {
		free(path);
		return 1;
	}

	const uint8_t *cbuf = comment ? (const uint8_t *)comment->text : (const uint8_t *)"";
	uint32_t clen = comment ? (uint32_t)comment->size : 0;
	bool ok = x4ss_write_container(path, cbuf, clen, uncomp_len, comp, comp_len);

	free(comp);
	free(path);
	return ok ? 1 : 0;
}

int read_serialize_struct_comment(const char *filename, struct string **comment_out, bool save_folder)
{
	if (!filename || !comment_out)
		return 0;
	char *path = resolve_struct_save_path(filename, save_folder);
	uint8_t *gd_raw = NULL;
	uint32_t gd_ulen = 0;
	if (gd_ss_decompress_file(path, &gd_raw, &gd_ulen)) {
		free(path);
		struct gd_ss_parsed parsed;
		if (!gd_ss_parse(gd_raw, gd_ulen, &parsed)) {
			free(gd_raw);
			return 0;
		}
		if (!parsed.has_comment) {
			gd_ss_free_parsed(&parsed);
			return 0;
		}
		if (parsed.comment_len == 0) {
			*comment_out = string_ref(&EMPTY_STRING);
		} else {
			bool ascii = true;
			for (uint32_t i = 0; i < parsed.comment_len; i++) {
				if ((uint8_t)parsed.comment[i] >= 0x80) {
					ascii = false;
					break;
				}
			}
			if (ascii) {
				*comment_out = make_string(parsed.comment, parsed.comment_len);
			} else {
				bool src_is_gbk = gd_ss_detect_is_gbk(&parsed);
				*comment_out = save_decode_vm_string(parsed.comment, parsed.comment_len, src_is_gbk);
			}
		}
		gd_ss_free_parsed(&parsed);
		return 1;
	}
	uint8_t *cbuf = NULL;
	uint32_t clen = 0;
	bool ok = x4ss_read_existing_parts(path, &cbuf, &clen, NULL, NULL, NULL);
	free(path);
	if (!ok || !cbuf)
		return 0;
	*comment_out = make_string((const char *)cbuf, clen);
	free(cbuf);
	return 1;
}

static char *make_comment_sidecar_path(const char *base)
{
	size_t blen = strlen(base);
	char *cpath = xmalloc(blen + 9);
	memcpy(cpath, base, blen);
	memcpy(cpath + blen, ".comment", 9);
	return cpath;
}

static bool gsave_upgrade_and_set_comment(struct gsave *gs, const char *text, size_t len)
{
	if (!gs || gs->version < 7)
		return false;
	if (gs->version == 7) {
		gs->version = 8;
		gs->uk2 = 64;
		bool has_sentinel = (gs->nr_globals > 0
				&& gs->globals[gs->nr_globals - 1].type == AIN_VOID
				&& gs->globals[gs->nr_globals - 1].name
				&& gs->globals[gs->nr_globals - 1].name[0] == '\0');
		if (!has_sentinel) {
			gs->globals = xrealloc(gs->globals, (size_t)(gs->nr_globals + 1) * sizeof(*gs->globals));
			memset(&gs->globals[gs->nr_globals], 0, sizeof(*gs->globals));
			gs->globals[gs->nr_globals].type = AIN_VOID;
			gs->globals[gs->nr_globals].name = xstrdup("");
			gs->nr_globals++;
		}
	}
	if (gs->comments) {
		for (int32_t i = 0; i < gs->nr_comments; i++) {
			if (gs->comments[i])
				free_string(gs->comments[i]);
		}
		free(gs->comments);
	}
	gs->nr_comments = 1;
	gs->comments = xcalloc(1, sizeof(struct string *));
	gs->comments[0] = make_string(text ? text : "", (unsigned int)len);
	return true;
}

int write_group_save_comment(const char *keyname, const char *filename, const struct string *log)
{
	if (!filename || !log)
		return 0;
	char *path = savedir_path(filename);
	enum savefile_error err;
	struct gsave *gs = gsave_read(path, &err);
	if (!gs) {
		WARNING("%s: %s", display_utf0(path), savefile_strerror(err));
		free(path);
		return 0;
	}
	if (keyname && keyname[0] && gs->key && !save_names_equal(keyname, gs->key)) {
		WARNING("Attempted to write save comment with wrong key: expected '%s', got '%s'",
				display_sjis0(gs->key), display_sjis1(keyname));
		gsave_free(gs);
		free(path);
		return 0;
	}
	if (!gsave_upgrade_and_set_comment(gs, log->text, (size_t)log->size)) {
		gsave_free(gs);
		free(path);
		return 0;
	}

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(path), strerror(errno));
		gsave_free(gs);
		free(path);
		return 0;
	}

	bool encrypt = ain ? !AIN_VERSION_GTE(ain, 6, 0) : (gs->version <= 5);
	int compression_level = (ain ? AIN_VERSION_GTE(ain, 6, 0) : (gs->version >= 7)) ? 1 : 9;
	err = gsave_write(gs, fp, encrypt, compression_level);
	gsave_free(gs);
	if (err != SAVEFILE_SUCCESS) {
		WARNING("Failed to write save file: %s", savefile_strerror(err));
		atomic_save_abort(fp, tmp_path);
		free(path);
		return 0;
	}
	bool ok = atomic_save_commit(fp, path, tmp_path, false);
	if (ok) {
		char *cpath = make_comment_sidecar_path(path);
		if (file_exists(cpath))
			remove_utf8(cpath);
		free(cpath);
	}
	free(path);
	return ok ? 1 : 0;
}

int read_group_save_comment(const char *keyname, const char *filename, struct string **log_out)
{
	if (!filename || !log_out)
		return 0;
	char *path = savedir_path(filename);
	enum savefile_error err;
	struct gsave *gs = gsave_read(path, &err);
	if (gs) {
		if (keyname && keyname[0] && gs->key && !save_names_equal(keyname, gs->key)) {
			WARNING("Attempted to read save comment with wrong key: expected '%s', got '%s'",
					display_sjis0(gs->key), display_sjis1(keyname));
			gsave_free(gs);
			free(path);
			return 0;
		}
		if (gs->nr_comments > 0 && gs->comments && gs->comments[0]) {
			const struct string *c = gs->comments[0];
			if (c->size == 0 || cjk_is_ascii_only(c->text)) {
				*log_out = string_dup(c);
			} else {
				bool src_is_gbk = gsave_detect_is_gbk(gs, NULL, keyname, gs->group, sys4_text_is_gbk);
				if (gs->nr_strings == 0 && (!gs->key || cjk_is_ascii_only(gs->key))
						&& (!gs->group || cjk_is_ascii_only(gs->group))) {
					int bias = save_text_gbk_bias(c->text);
					if (bias > 15)
						src_is_gbk = true;
					else if (bias < -15)
						src_is_gbk = false;
				}
				*log_out = save_decode_vm_string(c->text, c->size, src_is_gbk);
			}
			gsave_free(gs);
			free(path);
			return 1;
		}
		gsave_free(gs);
	}

	/* Fallback for pre-existing AliceHub saves with legacy <file>.comment sidecars */
	char *cpath = make_comment_sidecar_path(path);
	free(path);
	size_t len = 0;
	uint8_t *data = file_read(cpath, &len);
	free(cpath);
	if (!data)
		return 0;
	if (len == 0 || cjk_is_ascii_only((const char *)data)) {
		*log_out = make_string((const char *)data, (unsigned int)len);
	} else {
		int bias = save_text_gbk_bias((const char *)data);
		bool src_is_gbk = bias > 15 ? true : (bias < -15 ? false : sys4_text_is_gbk);
		*log_out = save_decode_vm_string((const char *)data, len, src_is_gbk);
	}
	free(data);
	return 1;
}

/* =========================================================================
 * Standalone PC Export & X4SS / GD\x01\x01 Repair Helpers
 * (Must not dereference VM globals such as `ain` or `heap`.)
 * ========================================================================= */

static int ext_ain_type_slot_width(const struct ain *a, const struct ain_type *type)
{
	if (!type)
		return 1;
	switch (type->data) {
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
	case AIN_REF_OPTION:
	case AIN_REF_INT:
	case AIN_REF_FLOAT:
	case AIN_REF_BOOL:
	case AIN_REF_LONG_INT:
	case AIN_REF_ENUM:
	case AIN_REF_FUNC_TYPE:
		return 2;
	case AIN_WRAP:
		if (a && a->version < 14)
			return 2;
		if (!type->array_type)
			return 1;
		switch (type->array_type->data) {
		case AIN_STRING:
		case AIN_STRUCT:
		case AIN_ARRAY:
		case AIN_DELEGATE:
		case AIN_ARRAY_TYPE:
			return 1;
		default:
			return 2;
		}
	case AIN_OPTION:
		return (type->array_type ? ext_ain_type_slot_width(a, type->array_type) : 1) + 1;
	default:
		return 1;
	}
}

static int ext_ain_find_struct(const struct ain *a, const char *name, size_t len)
{
	if (!a || !name || len == 0)
		return -1;
	char stack_buf[256];
	char *tmp = (len < sizeof(stack_buf)) ? stack_buf : xmalloc(len + 1);
	memcpy(tmp, name, len);
	tmp[len] = '\0';
	int id = ain_get_struct((struct ain *)a, tmp);
	if (id < 0) {
		for (int i = 0; i < a->nr_structures; i++) {
			if (a->structures[i].name && save_symbols_equal(a->structures[i].name, tmp)) {
				id = i;
				break;
			}
		}
	}
	if (tmp != stack_buf)
		free(tmp);
	return id;
}

static bool ext_cstr_eq_slice(const char *s, const char *slice, size_t slen)
{
	return s && strlen(s) == slen && (slen == 0 || memcmp(s, slice, slen) == 0);
}

static bool ext_sym_eq_slice(const char *s, const char *slice, size_t slen)
{
	if (!s || slen == 0)
		return false;
	char stack_buf[256];
	char *tmp = (slen < sizeof(stack_buf)) ? stack_buf : xmalloc(slen + 1);
	memcpy(tmp, slice, slen);
	tmp[slen] = '\0';
	bool eq = save_symbols_equal(s, tmp);
	if (tmp != stack_buf)
		free(tmp);
	return eq;
}

static bool x4ss_get_cstr_slice(struct x4ss_reader *r, uint32_t *off_out, uint32_t *len_out)
{
	uint32_t n = x4ss_get_u32(r);
	if (r->error || n > r->len - r->pos) {
		r->error = true;
		*off_out = 0;
		*len_out = 0;
		return false;
	}
	*off_out = (uint32_t)r->pos;
	*len_out = n;
	r->pos += n;
	return true;
}

static int32_t x4ss_conv_norm_code(int32_t data)
{
	switch (data) {
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_ARRAY_BOOL:
	case AIN_ARRAY_LONG_INT:
	case AIN_ARRAY_DELEGATE:
	case AIN_REF_ARRAY_INT:
	case AIN_REF_ARRAY_FLOAT:
	case AIN_REF_ARRAY_STRING:
	case AIN_REF_ARRAY_STRUCT:
	case AIN_REF_ARRAY_FUNC_TYPE:
	case AIN_REF_ARRAY_BOOL:
	case AIN_REF_ARRAY_LONG_INT:
	case AIN_REF_ARRAY_DELEGATE:
		return AIN_ARRAY;
	default:
		return data;
	}
}

static bool x4ss_conv_read_type(struct x4ss_reader *r, int32_t *eff_data_out, int depth)
{
	if (depth > 8) {
		r->error = true;
		return false;
	}
	int32_t dt = x4ss_get_i32(r);
	(void)x4ss_get_i32(r); /* fallback_id */
	(void)x4ss_get_i32(r); /* rank */
	uint32_t sym_off = 0, sym_len = 0;
	if (!x4ss_get_cstr_slice(r, &sym_off, &sym_len))
		return false;
	uint8_t has_inner = x4ss_get_u8(r);
	if (r->error)
		return false;
	int32_t inner_dt = 0;
	if (has_inner) {
		if (!x4ss_conv_read_type(r, &inner_dt, depth + 1))
			return false;
	}
	if (dt == AIN_WRAP && has_inner)
		*eff_data_out = gd_ss_wrap_to_ref(inner_dt);
	else
		*eff_data_out = dt;
	return true;
}

struct x4ss_conv_val {
	uint8_t tag;
	int32_t i;
	uint32_t str_off;
	uint32_t str_len;
	bool opt_is_none;
	uint8_t opt_tag;
};

struct x4ss_conv_field {
	uint32_t name_off;
	uint32_t name_len;
	struct x4ss_conv_val val;
};

struct x4ss_conv_obj {
	uint8_t kind;
	uint32_t gd_id; /* C1 id for STRUCT, C4 id for ARRAY */
	uint32_t sname_off;
	uint32_t sname_len;
	int32_t elem_type;
	uint32_t count; /* nr_fields for STRUCT, nr_elems for ARRAY */
	uint32_t group_id;
	struct x4ss_conv_field *fields;
	struct x4ss_conv_val *elems;
};

struct x4ss_conv_ftype {
	int32_t t1;
	int32_t t2;
	bool resolved_nonnull;
};

struct x4ss_conv_group {
	uint32_t rep_oid;
	uint32_t hash;
	int32_t schema_id;
	int ain_sno;
	struct x4ss_conv_ftype *ftypes;
};

static bool x4ss_conv_read_val(struct x4ss_reader *r, struct x4ss_conv_val *v, bool in_option)
{
	memset(v, 0, sizeof(*v));
	v->tag = x4ss_get_u8(r);
	if (r->error)
		return false;
	switch (v->tag) {
	case X4SS_VAL_NONE:
		return true;
	case X4SS_VAL_SCALAR:
		v->i = x4ss_get_i32(r);
		return !r->error;
	case X4SS_VAL_STRING:
		return x4ss_get_cstr_slice(r, &v->str_off, &v->str_len);
	case X4SS_VAL_OBJ:
		v->i = (int32_t)x4ss_get_u32(r);
		return !r->error;
	case X4SS_VAL_IFACE:
		v->i = (int32_t)x4ss_get_u32(r);
		(void)x4ss_get_i32(r); /* saved vtable offset */
		return !r->error;
	case X4SS_VAL_OPTION: {
		if (in_option) {
			r->error = true;
			return false;
		}
		v->opt_is_none = (x4ss_get_u8(r) != 0);
		if (r->error)
			return false;
		if (!v->opt_is_none) {
			struct x4ss_conv_val inner;
			if (!x4ss_conv_read_val(r, &inner, true))
				return false;
			v->opt_tag = inner.tag;
			v->i = inner.i;
			v->str_off = inner.str_off;
			v->str_len = inner.str_len;
		}
		return true;
	}
	default:
		r->error = true;
		return false;
	}
}

static uint32_t x4ss_conv_group_hash(const uint8_t *raw, const struct x4ss_conv_obj *obj)
{
	uint32_t h = gd_ss_fnv1a((const char *)(raw + obj->sname_off), obj->sname_len);
	h ^= obj->count * 2654435761u;
	for (uint32_t f = 0; f < obj->count; f++) {
		h ^= gd_ss_fnv1a((const char *)(raw + obj->fields[f].name_off), obj->fields[f].name_len);
		h = (h << 5) | (h >> 27);
	}
	return h;
}

static bool x4ss_conv_group_equal(const uint8_t *raw, const struct x4ss_conv_obj *a, const struct x4ss_conv_obj *b)
{
	if (a->sname_len != b->sname_len || a->count != b->count)
		return false;
	if (a->sname_len && memcmp(raw + a->sname_off, raw + b->sname_off, a->sname_len) != 0)
		return false;
	for (uint32_t f = 0; f < a->count; f++) {
		if (a->fields[f].name_len != b->fields[f].name_len)
			return false;
		if (a->fields[f].name_len &&
		    memcmp(raw + a->fields[f].name_off, raw + b->fields[f].name_off, a->fields[f].name_len) != 0)
			return false;
	}
	return true;
}

static void x4ss_conv_infer_val_type(const struct x4ss_conv_obj *objs, uint32_t nr_objs,
		const struct x4ss_conv_val *v, struct x4ss_conv_ftype *out)
{
	memset(out, 0, sizeof(*out));
	switch (v->tag) {
	case X4SS_VAL_NONE:
		out->t1 = AIN_VOID;
		out->t2 = AIN_VOID;
		break;
	case X4SS_VAL_SCALAR:
		out->t1 = AIN_INT;
		out->t2 = AIN_INT;
		out->resolved_nonnull = true;
		break;
	case X4SS_VAL_STRING:
		out->t1 = AIN_STRING;
		out->t2 = AIN_STRING;
		out->resolved_nonnull = true;
		break;
	case X4SS_VAL_IFACE:
		out->t1 = AIN_IFACE;
		out->t2 = AIN_IFACE;
		out->resolved_nonnull = (v->i > 0);
		break;
	case X4SS_VAL_OBJ: {
		uint32_t oid = (v->i > 0) ? (uint32_t)v->i : 0;
		if (oid > 0 && oid <= nr_objs && objs[oid].kind == X4SS_OBJ_ARRAY) {
			out->t1 = AIN_ARRAY;
			out->t2 = AIN_ARRAY;
			out->resolved_nonnull = true;
		} else if (oid > 0 && oid <= nr_objs && objs[oid].kind == X4SS_OBJ_STRUCT) {
			out->t1 = AIN_STRUCT;
			out->t2 = AIN_STRUCT;
			out->resolved_nonnull = true;
		} else {
			out->t1 = AIN_STRUCT;
			out->t2 = AIN_STRUCT;
			out->resolved_nonnull = false;
		}
		break;
	}
	case X4SS_VAL_OPTION:
		out->t1 = AIN_OPTION;
		out->t2 = AIN_INT;
		if (!v->opt_is_none) {
			out->resolved_nonnull = true;
			switch (v->opt_tag) {
			case X4SS_VAL_STRING: out->t2 = AIN_STRING; break;
			case X4SS_VAL_IFACE:  out->t2 = AIN_IFACE;  break;
			case X4SS_VAL_OBJ: {
				uint32_t oid = (v->i > 0) ? (uint32_t)v->i : 0;
				out->t2 = (oid > 0 && oid <= nr_objs && objs[oid].kind == X4SS_OBJ_ARRAY)
					? AIN_ARRAY : AIN_STRUCT;
				break;
			}
			default: out->t2 = AIN_INT; break;
			}
		}
		break;
	default:
		out->t1 = AIN_INT;
		out->t2 = AIN_INT;
		break;
	}
}

static int32_t x4ss_conv_emit_scalar_or_ref(struct gd_ss_builder *b, const uint8_t *raw,
		const struct x4ss_conv_obj *objs, uint32_t nr_objs,
		uint8_t tag, int32_t raw_i, uint32_t str_off, uint32_t str_len, int32_t expected_type)
{
	switch (tag) {
	case X4SS_VAL_NONE:
		if (expected_type == AIN_ARRAY || expected_type == AIN_REF_ARRAY
				|| expected_type == AIN_STRUCT || expected_type == AIN_REF_STRUCT
				|| expected_type == AIN_IFACE || expected_type == AIN_IFACE_WRAP)
			return -1;
		if (expected_type == AIN_STRING || expected_type == AIN_REF_STRING)
			return INT32_MAX;
		return 0;
	case X4SS_VAL_SCALAR:
		return raw_i;
	case X4SS_VAL_STRING:
		return gd_ss_builder_add_string(b, (const char *)(raw + str_off), str_len);
	case X4SS_VAL_OBJ:
	case X4SS_VAL_IFACE: {
		uint32_t oid = (raw_i > 0) ? (uint32_t)raw_i : 0;
		if (oid > 0 && oid <= nr_objs && objs[oid].kind != X4SS_OBJ_NULL)
			return (int32_t)objs[oid].gd_id;
		return (expected_type == AIN_ARRAY || expected_type == AIN_REF_ARRAY
				|| expected_type == AIN_STRUCT || expected_type == AIN_REF_STRUCT
				|| expected_type == AIN_IFACE || expected_type == AIN_IFACE_WRAP) ? -1 : 0;
	}
	default:
		return 0;
	}
}

static int x4ss_conv_find_field_for_member(const uint8_t *raw, const struct x4ss_conv_obj *o,
		const struct ain_variable *v)
{
	if (v->name2 && v->name2[0]) {
		for (uint32_t f = 0; f < o->count; f++) {
			if (ext_cstr_eq_slice(v->name2, (const char *)(raw + o->fields[f].name_off), o->fields[f].name_len))
				return (int)f;
		}
	}
	if (v->name && v->name[0]) {
		for (uint32_t f = 0; f < o->count; f++) {
			if (ext_cstr_eq_slice(v->name, (const char *)(raw + o->fields[f].name_off), o->fields[f].name_len))
				return (int)f;
		}
	}
	if (v->name2 && v->name2[0]) {
		for (uint32_t f = 0; f < o->count; f++) {
			if (ext_sym_eq_slice(v->name2, (const char *)(raw + o->fields[f].name_off), o->fields[f].name_len))
				return (int)f;
		}
	}
	if (v->name && v->name[0]) {
		for (uint32_t f = 0; f < o->count; f++) {
			if (ext_sym_eq_slice(v->name, (const char *)(raw + o->fields[f].name_off), o->fields[f].name_len))
				return (int)f;
		}
	}
	return -1;
}

bool x4ss_is_container_file(const char *path)
{
	if (!path)
		return false;
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp)
		return false;
	uint8_t hdr[8];
	bool is_x4ss = (fread(hdr, 1, 8, fp) == 8 && memcmp(hdr, X4SS_MAGIC, 8) == 0);
	fclose(fp);
	return is_x4ss;
}

struct x4ss_conv_parsed {
	uint8_t *raw;
	uint32_t uncomp_len;
	uint8_t *comment;
	uint32_t clen;
	uint32_t *raw_roots;
	uint32_t nr_roots;
	struct x4ss_conv_obj *objs;
	uint32_t nr_objs;
	uint32_t nr_c1;
	uint32_t nr_c4;
	struct x4ss_conv_group *groups;
	uint32_t nr_groups;
};

static void x4ss_conv_free_parsed(struct x4ss_conv_parsed *p)
{
	if (!p)
		return;
	for (uint32_t gi = 0; gi < p->nr_groups; gi++)
		free(p->groups[gi].ftypes);
	free(p->groups);
	for (uint32_t oid = 1; oid <= p->nr_objs; oid++) {
		free(p->objs[oid].fields);
		free(p->objs[oid].elems);
	}
	free(p->objs);
	free(p->raw_roots);
	free(p->raw);
	free(p->comment);
	memset(p, 0, sizeof(*p));
}

static bool x4ss_conv_parse_file(const char *src_path, struct x4ss_conv_parsed *out)
{
	memset(out, 0, sizeof(*out));
	if (!src_path)
		return false;

	uint8_t *comment = NULL;
	uint32_t clen = 0, uncomp_len = 0, comp_len = 0;
	uint8_t *comp = NULL;
	if (!x4ss_read_existing_parts(src_path, &comment, &clen, &uncomp_len, &comp, &comp_len))
		return false;
	if (uncomp_len == 0 || uncomp_len > 256u * 1024u * 1024u || comp_len == 0 || !comp) {
		free(comment);
		free(comp);
		return false;
	}

	uint8_t *raw = xmalloc(uncomp_len);
	uLongf dst_len = (uLongf)uncomp_len;
	int zret = uncompress(raw, &dst_len, comp, (uLong)comp_len);
	free(comp);
	if (zret != Z_OK || dst_len != uncomp_len) {
		free(raw);
		free(comment);
		return false;
	}

	struct x4ss_reader r = { .data = raw, .len = uncomp_len, .pos = 0, .error = false };
	uint32_t nr_roots = x4ss_get_u32(&r);
	if (r.error || nr_roots > 1024u * 1024u || nr_roots * 4u > r.len - r.pos) {
		free(raw);
		free(comment);
		return false;
	}
	uint32_t *raw_roots = xcalloc(max(nr_roots, 1u), sizeof(uint32_t));
	for (uint32_t i = 0; i < nr_roots; i++)
		raw_roots[i] = x4ss_get_u32(&r);

	uint32_t nr_objs = x4ss_get_u32(&r);
	if (r.error || nr_objs > 16u * 1024u * 1024u || nr_objs > r.len - r.pos) {
		free(raw_roots);
		free(raw);
		free(comment);
		return false;
	}

	struct x4ss_conv_obj *objs = xcalloc((size_t)nr_objs + 1, sizeof(*objs));
	uint32_t nr_c1 = 1;
	uint32_t nr_c4 = 1;
	struct x4ss_conv_group *groups = NULL;
	uint32_t nr_groups = 0, cap_groups = 0;
	uint32_t *grp_ht = NULL;
	uint32_t grp_ht_cap = 0;

	/* Pass 1: X4SS object headers */
	for (uint32_t oid = 1; oid <= nr_objs && !r.error; oid++) {
		uint8_t kind = x4ss_get_u8(&r);
		objs[oid].kind = kind;
		if (kind == X4SS_OBJ_NULL) {
			objs[oid].gd_id = 0;
		} else if (kind == X4SS_OBJ_STRUCT) {
			if (!x4ss_get_cstr_slice(&r, &objs[oid].sname_off, &objs[oid].sname_len))
				break;
			objs[oid].gd_id = nr_c1++;
		} else if (kind == X4SS_OBJ_ARRAY) {
			(void)x4ss_get_i32(&r); /* a_type */
			int32_t rank = x4ss_get_i32(&r);
			int32_t eff_dt = 0;
			if (!x4ss_conv_read_type(&r, &eff_dt, 0))
				break;
			uint32_t cnt = x4ss_get_u32(&r);
			int32_t norm_et = x4ss_conv_norm_code(eff_dt);
			if (r.error || rank != 1 || norm_et == AIN_OPTION || cnt > r.len - r.pos) {
				r.error = true;
				break;
			}
			objs[oid].elem_type = norm_et;
			objs[oid].count = cnt;
			objs[oid].gd_id = nr_c4++;
		} else {
			r.error = true;
		}
	}
	if (r.error)
		goto fail;

	for (uint32_t i = 0; i < nr_roots; i++) {
		uint32_t rid = raw_roots[i];
		if (rid == 0 || rid > nr_objs || objs[rid].kind != X4SS_OBJ_STRUCT)
			goto fail;
	}

	/* Pass 2: X4SS object bodies */
	for (uint32_t oid = 1; oid <= nr_objs && !r.error; oid++) {
		struct x4ss_conv_obj *o = &objs[oid];
		if (o->kind == X4SS_OBJ_STRUCT) {
			uint32_t nf = x4ss_get_u32(&r);
			if (r.error || nf > 1024u * 1024u || nf > (r.len - r.pos) / 5u) {
				r.error = true;
				break;
			}
			o->count = nf;
			o->fields = xcalloc(max(nf, 1u), sizeof(*o->fields));
			for (uint32_t f = 0; f < nf && !r.error; f++) {
				if (!x4ss_get_cstr_slice(&r, &o->fields[f].name_off, &o->fields[f].name_len))
					break;
				x4ss_conv_read_val(&r, &o->fields[f].val, false);
			}
		} else if (o->kind == X4SS_OBJ_ARRAY) {
			o->elems = xcalloc(max(o->count, 1u), sizeof(*o->elems));
			for (uint32_t k = 0; k < o->count && !r.error; k++)
				x4ss_conv_read_val(&r, &o->elems[k], false);
		}
	}
	if (r.error)
		goto fail;

	/* Group structs with identical (sname, field names) and infer unified field types */
	for (uint32_t oid = 1; oid <= nr_objs; oid++) {
		struct x4ss_conv_obj *o = &objs[oid];
		if (o->kind != X4SS_OBJ_STRUCT)
			continue;
		if (!grp_ht) {
			grp_ht_cap = 256;
			grp_ht = xcalloc(grp_ht_cap, sizeof(uint32_t));
		} else if ((nr_groups + 1) * 2 >= grp_ht_cap) {
			uint32_t new_cap = grp_ht_cap * 2;
			uint32_t *nht = xcalloc(new_cap, sizeof(uint32_t));
			uint32_t nmask = new_cap - 1;
			for (uint32_t gi = 0; gi < nr_groups; gi++) {
				uint32_t s = groups[gi].hash & nmask;
				while (nht[s] != 0)
					s = (s + 1) & nmask;
				nht[s] = gi + 1;
			}
			free(grp_ht);
			grp_ht = nht;
			grp_ht_cap = new_cap;
		}
		uint32_t h = x4ss_conv_group_hash(raw, o);
		uint32_t mask = grp_ht_cap - 1;
		uint32_t slot = h & mask;
		uint32_t gid = UINT32_MAX;
		while (grp_ht[slot] != 0) {
			uint32_t cand = grp_ht[slot] - 1;
			if (groups[cand].hash == h && x4ss_conv_group_equal(raw, &objs[groups[cand].rep_oid], o)) {
				gid = cand;
				break;
			}
			slot = (slot + 1) & mask;
		}
		if (gid == UINT32_MAX) {
			if (nr_groups >= cap_groups) {
				cap_groups = cap_groups ? cap_groups * 2 : 64;
				groups = xrealloc(groups, cap_groups * sizeof(*groups));
			}
			gid = nr_groups++;
			groups[gid].rep_oid = oid;
			groups[gid].hash = h;
			groups[gid].schema_id = -1;
			groups[gid].ain_sno = -1;
			groups[gid].ftypes = xcalloc(max(o->count, 1u), sizeof(struct x4ss_conv_ftype));
			for (uint32_t f = 0; f < o->count; f++)
				groups[gid].ftypes[f].t1 = -1;
			grp_ht[slot] = gid + 1;
		}
		o->group_id = gid;

		struct x4ss_conv_ftype *gft = groups[gid].ftypes;
		for (uint32_t f = 0; f < o->count; f++) {
			struct x4ss_conv_ftype cand;
			x4ss_conv_infer_val_type(objs, nr_objs, &o->fields[f].val, &cand);
			if (gft[f].t1 < 0 || (gft[f].t1 == AIN_VOID && cand.t1 != AIN_VOID)) {
				gft[f] = cand;
			} else if (cand.t1 == AIN_OPTION && gft[f].t1 != AIN_OPTION) {
				if (!gft[f].resolved_nonnull)
					gft[f] = cand;
			} else if (gft[f].t1 == AIN_OPTION && cand.t1 == AIN_OPTION) {
				if (!gft[f].resolved_nonnull && cand.resolved_nonnull)
					gft[f] = cand;
			} else if (!gft[f].resolved_nonnull && cand.resolved_nonnull) {
				gft[f] = cand;
			}
		}
	}

	free(grp_ht);
	out->raw = raw;
	out->uncomp_len = uncomp_len;
	out->comment = comment;
	out->clen = clen;
	out->raw_roots = raw_roots;
	out->nr_roots = nr_roots;
	out->objs = objs;
	out->nr_objs = nr_objs;
	out->nr_c1 = nr_c1;
	out->nr_c4 = nr_c4;
	out->groups = groups;
	out->nr_groups = nr_groups;
	return true;

fail:
	free(grp_ht);
	for (uint32_t gi = 0; gi < nr_groups; gi++)
		free(groups[gi].ftypes);
	free(groups);
	for (uint32_t oid = 1; oid <= nr_objs; oid++) {
		free(objs[oid].fields);
		free(objs[oid].elems);
	}
	free(objs);
	free(raw_roots);
	free(raw);
	free(comment);
	return false;
}

static int32_t x4ss_conv_intern_inferred_schema(struct gd_ss_builder *b, const uint8_t *raw,
		const struct x4ss_conv_obj *rep, const struct x4ss_conv_ftype *gft)
{
	uint32_t schema_nf = 0;
	for (uint32_t f = 0; f < rep->count; f++)
		schema_nf += (gft[f].t1 == AIN_OPTION) ? 2u : 1u;

	struct gd_ss_bschema_field *fields = xcalloc(max(schema_nf, 1u), sizeof(*fields));
	uint32_t fi = 0;
	for (uint32_t f = 0; f < rep->count; f++) {
		int32_t ft1 = (gft[f].t1 < 0) ? AIN_VOID : gft[f].t1;
		int32_t ft2 = (gft[f].t1 < 0) ? AIN_VOID : gft[f].t2;
		fields[fi].type1 = ft1;
		fields[fi].type2 = ft2;
		fields[fi].name = (char *)(raw + rep->fields[f].name_off);
		fields[fi].name_len = rep->fields[f].name_len;
		fi++;
		if (ft1 == AIN_OPTION) {
			fields[fi].type1 = AIN_VOID;
			fields[fi].type2 = AIN_VOID;
			fields[fi].name = "";
			fields[fi].name_len = 0;
			fi++;
		}
	}
	struct gd_ss_bschema cand = {
		.name = (char *)(raw + rep->sname_off),
		.name_len = rep->sname_len,
		.nr_fields = schema_nf,
		.fields = fields,
	};
	int32_t sid = gd_ss_builder_intern_schema(b, &cand);
	free(fields);
	return sid;
}

static void x4ss_conv_emit_inferred_obj(struct gd_ss_builder *b, const uint8_t *raw,
		const struct x4ss_conv_obj *objs, uint32_t nr_objs,
		const struct x4ss_conv_obj *o, const struct x4ss_conv_group *g,
		struct gd_ss_bobj *dst, uint32_t schema_nf)
{
	const struct x4ss_conv_ftype *gft = g->ftypes;
	uint32_t fi = 0;
	for (uint32_t f = 0; f < o->count && fi < schema_nf; f++) {
		const struct x4ss_conv_val *v = &o->fields[f].val;
		int32_t ft1 = (gft[f].t1 < 0) ? AIN_VOID : gft[f].t1;
		int32_t ft2 = (gft[f].t1 < 0) ? AIN_VOID : gft[f].t2;
		if (ft1 == AIN_OPTION) {
			bool is_none = (v->tag != X4SS_VAL_OPTION) || v->opt_is_none;
			int32_t iv = is_none
				? -1
				: x4ss_conv_emit_scalar_or_ref(b, raw, objs, nr_objs,
						v->opt_tag, v->i, v->str_off, v->str_len, ft2);
			dst->val_idxs[fi++] = gd_ss_builder_add_val(b, iv);
			if (fi < schema_nf)
				dst->val_idxs[fi++] = gd_ss_builder_add_val(b, is_none ? 1 : 0);
		} else {
			int32_t eff_t = (ft1 == AIN_WRAP) ? ft2 : ft1;
			int32_t gv = x4ss_conv_emit_scalar_or_ref(b, raw, objs, nr_objs,
					v->tag, v->i, v->str_off, v->str_len, eff_t);
			dst->val_idxs[fi++] = gd_ss_builder_add_val(b, gv);
		}
	}
}

static void x4ss_conv_emit_array(struct gd_ss_builder *b, const uint8_t *raw,
		const struct x4ss_conv_obj *objs, uint32_t nr_objs, const struct x4ss_conv_obj *o)
{
	struct gd_ss_barray *dst = &b->arrays[o->gd_id];
	dst->rank = 1;
	dst->total = o->count;
	dst->elem_type = o->elem_type;
	dst->elems = xcalloc(max(o->count, 1u), sizeof(int32_t));
	for (uint32_t k = 0; k < o->count; k++) {
		const struct x4ss_conv_val *ev = &o->elems[k];
		dst->elems[k] = x4ss_conv_emit_scalar_or_ref(b, raw, objs, nr_objs,
				ev->tag, ev->i, ev->str_off, ev->str_len, o->elem_type);
	}
}

/* Self-contained .ain-free / utfsjis-free X4SS -> GD\x01\x01 conversion (used by Switch Hub). */
bool x4ss_convert_to_gdss_file(const char *src_path, const char *dst_path)
{
	if (!src_path || !dst_path)
		return false;

	struct x4ss_conv_parsed p;
	if (!x4ss_conv_parse_file(src_path, &p))
		return false;

	struct gd_ss_builder b;
	gd_ss_builder_init(&b);
	gd_ss_builder_reserve_objs(&b, p.nr_c1);
	gd_ss_builder_reserve_arrays(&b, p.nr_c4);

	int32_t *gd_roots = xcalloc(max(p.nr_roots, 1u), sizeof(int32_t));
	for (uint32_t i = 0; i < p.nr_roots; i++)
		gd_roots[i] = (int32_t)p.objs[p.raw_roots[i]].gd_id;
	gd_ss_builder_set_roots(&b, gd_roots, p.nr_roots);
	free(gd_roots);

	for (uint32_t gi = 0; gi < p.nr_groups; gi++) {
		const struct x4ss_conv_obj *rep = &p.objs[p.groups[gi].rep_oid];
		p.groups[gi].schema_id = x4ss_conv_intern_inferred_schema(&b, p.raw, rep, p.groups[gi].ftypes);
	}

	for (uint32_t oid = 1; oid <= p.nr_objs; oid++) {
		const struct x4ss_conv_obj *o = &p.objs[oid];
		if (o->kind == X4SS_OBJ_STRUCT) {
			const struct x4ss_conv_group *g = &p.groups[o->group_id];
			uint32_t schema_nf = b.schemas[g->schema_id].nr_fields;
			struct gd_ss_bobj *dst = &b.objs[o->gd_id];
			dst->struct_def = g->schema_id;
			dst->nr_vals = schema_nf;
			dst->val_idxs = xcalloc(max(schema_nf, 1u), sizeof(uint32_t));
			x4ss_conv_emit_inferred_obj(&b, p.raw, p.objs, p.nr_objs, o, g, dst, schema_nf);
		} else if (o->kind == X4SS_OBJ_ARRAY) {
			x4ss_conv_emit_array(&b, p.raw, p.objs, p.nr_objs, o);
		}
	}

	gd_ss_builder_set_comment(&b, p.comment, p.clen, p.clen > 0);
	bool ok = gd_ss_builder_write_file(&b, dst_path, false);
	gd_ss_builder_free(&b);
	x4ss_conv_free_parsed(&p);
	return ok;
}

static bool x4ss_convert_to_gdss_file_with_ain(const char *src_path, const char *dst_path, const struct ain *ext_ain)
{
	if (!src_path || !dst_path)
		return false;
	if (!ext_ain)
		return x4ss_convert_to_gdss_file(src_path, dst_path);

	struct x4ss_conv_parsed p;
	if (!x4ss_conv_parse_file(src_path, &p))
		return false;

	struct gd_ss_builder b;
	gd_ss_builder_init(&b);
	gd_ss_builder_reserve_objs(&b, p.nr_c1);
	gd_ss_builder_reserve_arrays(&b, p.nr_c4);

	int32_t *gd_roots = xcalloc(max(p.nr_roots, 1u), sizeof(int32_t));
	for (uint32_t i = 0; i < p.nr_roots; i++)
		gd_roots[i] = (int32_t)p.objs[p.raw_roots[i]].gd_id;
	gd_ss_builder_set_roots(&b, gd_roots, p.nr_roots);
	free(gd_roots);

	for (uint32_t gi = 0; gi < p.nr_groups; gi++) {
		const struct x4ss_conv_obj *rep = &p.objs[p.groups[gi].rep_oid];
		int sno = ext_ain_find_struct(ext_ain, (const char *)(p.raw + rep->sname_off), rep->sname_len);
		p.groups[gi].ain_sno = sno;
		if (sno >= 0 && sno < ext_ain->nr_structures) {
			const struct ain_struct *st = &ext_ain->structures[sno];
			uint32_t nf = (uint32_t)st->nr_members;
			struct gd_ss_bschema_field *fields = xcalloc(max(nf, 1u), sizeof(*fields));
			for (int m = 0; m < st->nr_members; m++) {
				const char *mname = gd_ss_member_save_name(ext_ain, &st->members[m]);
				fields[m].type1 = (int32_t)st->members[m].type.data;
				fields[m].type2 = gd_ss_compute_type2(&st->members[m].type);
				fields[m].name = (char *)mname;
				fields[m].name_len = strlen(mname);
			}
			struct gd_ss_bschema cand = {
				.name = st->name ? st->name : "",
				.name_len = st->name ? strlen(st->name) : 0,
				.nr_fields = nf,
				.fields = fields,
			};
			p.groups[gi].schema_id = gd_ss_builder_intern_schema(&b, &cand);
			free(fields);
		} else {
			p.groups[gi].schema_id = x4ss_conv_intern_inferred_schema(&b, p.raw, rep, p.groups[gi].ftypes);
		}
	}

	for (uint32_t oid = 1; oid <= p.nr_objs; oid++) {
		const struct x4ss_conv_obj *o = &p.objs[oid];
		if (o->kind == X4SS_OBJ_STRUCT) {
			const struct x4ss_conv_group *g = &p.groups[o->group_id];
			uint32_t schema_nf = b.schemas[g->schema_id].nr_fields;
			struct gd_ss_bobj *dst = &b.objs[o->gd_id];
			dst->struct_def = g->schema_id;
			dst->nr_vals = schema_nf;
			dst->val_idxs = xcalloc(max(schema_nf, 1u), sizeof(uint32_t));

			if (g->ain_sno >= 0 && g->ain_sno < ext_ain->nr_structures) {
				const struct ain_struct *st = &ext_ain->structures[g->ain_sno];
				for (int m = 0; m < st->nr_members && (uint32_t)m < schema_nf; m++) {
					const struct ain_type *mt = &st->members[m].type;
					const char *mname = gd_ss_member_save_name(ext_ain, &st->members[m]);
					int32_t ft1 = (int32_t)mt->data;
					int32_t ft2 = gd_ss_compute_type2(mt);
					if (mt->data == AIN_VOID || mt->data == AIN_DELEGATE || mt->data == AIN_REF_DELEGATE
							|| !strcmp(mname, "<vtable>")) {
						dst->val_idxs[m] = 0;
						continue;
					}
					if (!mname[0]) {
						dst->val_idxs[m] = gd_ss_type_is_struct_or_iface(ft1, ft2)
							? gd_ss_builder_add_val(&b, -1) : 0;
						continue;
					}
					int xf = x4ss_conv_find_field_for_member(p.raw, o, &st->members[m]);
					if (mt->data == AIN_OPTION) {
						int w = ext_ain_type_slot_width(ext_ain, mt);
						bool is_none = true;
						int32_t iv = -1;
						if (xf >= 0 && o->fields[xf].val.tag == X4SS_VAL_OPTION
								&& !o->fields[xf].val.opt_is_none) {
							const struct x4ss_conv_val *v = &o->fields[xf].val;
							is_none = false;
							iv = x4ss_conv_emit_scalar_or_ref(&b, p.raw, p.objs, p.nr_objs,
									v->opt_tag, v->i, v->str_off, v->str_len, ft2);
						}
						dst->val_idxs[m] = gd_ss_builder_add_val(&b, iv);
						for (int k = 1; k < w && m + k < st->nr_members && (uint32_t)(m + k) < schema_nf; k++) {
							int32_t sv = (k == w - 1) ? (is_none ? 1 : 0) : (is_none ? -1 : 0);
							dst->val_idxs[m + k] = gd_ss_builder_add_val(&b, sv);
						}
						if (w > 1)
							m += w - 1;
						continue;
					}
					int32_t eff_t = (ft1 == AIN_WRAP) ? ft2 : ft1;
					int32_t gv;
					if (xf >= 0) {
						const struct x4ss_conv_val *v = &o->fields[xf].val;
						gv = x4ss_conv_emit_scalar_or_ref(&b, p.raw, p.objs, p.nr_objs,
								v->tag, v->i, v->str_off, v->str_len, eff_t);
					} else if (gd_ss_type_is_struct_or_iface(ft1, ft2)
							|| eff_t == AIN_ARRAY || eff_t == AIN_REF_ARRAY) {
						gv = -1;
					} else if (eff_t == AIN_STRING || eff_t == AIN_REF_STRING) {
						gv = INT32_MAX;
					} else {
						gv = 0;
					}
					dst->val_idxs[m] = gd_ss_builder_add_val(&b, gv);
				}
			} else {
				x4ss_conv_emit_inferred_obj(&b, p.raw, p.objs, p.nr_objs, o, g, dst, schema_nf);
			}
		} else if (o->kind == X4SS_OBJ_ARRAY) {
			x4ss_conv_emit_array(&b, p.raw, p.objs, p.nr_objs, o);
		}
	}

	gd_ss_builder_set_comment(&b, p.comment, p.clen, p.clen > 0);
	bool ok = gd_ss_builder_write_file(&b, dst_path, false);
	gd_ss_builder_free(&b);
	x4ss_conv_free_parsed(&p);
	return ok;
}

static int gd_ss_find_old_field_for_member(const struct gd_ss_struct_def *sd, const struct ain_variable *v)
{
	if (!sd || !v)
		return -1;
	if (v->name2 && v->name2[0]) {
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name;
			if (sd->fields[fi].type1 != AIN_VOID && fn && fn[0] && !strcmp(v->name2, fn))
				return (int)fi;
		}
	}
	if (v->name && v->name[0]) {
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name;
			if (sd->fields[fi].type1 != AIN_VOID && fn && fn[0] && !strcmp(v->name, fn))
				return (int)fi;
		}
	}
	if (v->name2 && v->name2[0]) {
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name;
			if (sd->fields[fi].type1 != AIN_VOID && fn && fn[0] && save_symbols_equal(v->name2, fn))
				return (int)fi;
		}
	}
	if (v->name && v->name[0]) {
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name;
			if (sd->fields[fi].type1 != AIN_VOID && fn && fn[0] && save_symbols_equal(v->name, fn))
				return (int)fi;
		}
	}
	return -1;
}

/* Self-contained .ain-free / utfsjis-free GD\x01\x01 repair: fixes 0 -> -1 null structs & multi-root objs[0]. */
static bool gd_ss_repair_raw_nulls(uint8_t *gd_raw, uint32_t gd_ulen, const char *dst_path)
{
	struct gd_ss_parsed parsed;
	if (!gd_ss_parse(gd_raw, gd_ulen, &parsed)) {
		free(gd_raw);
		return false;
	}

	struct gd_ss_builder b;
	gd_ss_builder_init(&b);

	/* Preserve C3 string pool 1-to-1 by index */
	if (parsed.nr_strings > 0) {
		b.cap_strings = parsed.nr_strings;
		b.nr_strings = parsed.nr_strings;
		b.strings = xcalloc(parsed.nr_strings, sizeof(char *));
		b.string_lens = xcalloc(parsed.nr_strings, sizeof(size_t));
		for (uint32_t i = 0; i < parsed.nr_strings; i++) {
			size_t slen = parsed.strings[i].len;
			b.strings[i] = xmalloc(slen + 1);
			if (slen > 0)
				memcpy(b.strings[i], parsed.strings[i].ptr, slen);
			b.strings[i][slen] = '\0';
			b.string_lens[i] = slen;
		}
	}

	gd_ss_builder_reserve_objs(&b, max(parsed.nr_objs, 1u));
	gd_ss_builder_reserve_arrays(&b, max(parsed.nr_arrays, 1u));
	gd_ss_builder_set_roots(&b, parsed.root_ids, parsed.nr_roots);

	uint32_t nr_defs = parsed.nr_struct_defs;
	int32_t *new_schema_ids = xcalloc(max(nr_defs, 1u), sizeof(int32_t));
	for (uint32_t si = 0; si < nr_defs; si++) {
		const struct gd_ss_struct_def *sd = &parsed.struct_defs[si];
		struct gd_ss_bschema_field *fields = xcalloc(max(sd->nr_fields, 1u), sizeof(*fields));
		for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
			const char *fn = sd->fields[fi].name ? sd->fields[fi].name : "";
			fields[fi].type1 = sd->fields[fi].type1;
			fields[fi].type2 = sd->fields[fi].type2;
			fields[fi].name = (char *)fn;
			fields[fi].name_len = strlen(fn);
		}
		struct gd_ss_bschema cand = {
			.name = (char *)(sd->name ? sd->name : ""),
			.name_len = sd->name ? strlen(sd->name) : 0,
			.nr_fields = sd->nr_fields,
			.fields = fields,
		};
		new_schema_ids[si] = gd_ss_builder_intern_schema(&b, &cand);
		free(fields);
	}

	for (uint32_t id = 1; id < parsed.nr_objs; id++) {
		const struct gd_ss_obj *old_obj = &parsed.objs[id];
		int32_t sd_idx = old_obj->struct_def;
		if (sd_idx < 0 || (uint32_t)sd_idx >= nr_defs) {
			b.objs[id].struct_def = sd_idx;
			b.objs[id].nr_vals = 0;
			continue;
		}
		const struct gd_ss_struct_def *old_sd = &parsed.struct_defs[sd_idx];
		uint32_t nf = old_obj->nr_vals;
		b.objs[id].struct_def = new_schema_ids[sd_idx];
		b.objs[id].nr_vals = nf;
		b.objs[id].val_idxs = xcalloc(max(nf, 1u), sizeof(uint32_t));
		for (uint32_t fi = 0; fi < nf; fi++) {
			uint32_t v_idx = gd_read_u32_le(old_obj->val_idxs_le + fi * 4u);
			int32_t raw_val = (v_idx < parsed.nr_vals)
				? gd_read_i32_le(parsed.vals_le + v_idx * 4u) : 0;
			if (fi < old_sd->nr_fields
					&& gd_ss_type_is_struct_or_iface(old_sd->fields[fi].type1, old_sd->fields[fi].type2)
					&& raw_val == 0) {
				raw_val = -1;
			}
			b.objs[id].val_idxs[fi] = gd_ss_builder_add_val(&b, raw_val);
		}
	}

	for (uint32_t ai = 0; ai < parsed.nr_arrays; ai++) {
		const struct gd_ss_array *sa = &parsed.arrays[ai];
		int32_t et = sa->elem_type;
		bool is_struct_elem = gd_ss_type_is_struct_or_iface(et, et);
		b.arrays[ai].rank = sa->rank > 0 ? sa->rank : 1;
		b.arrays[ai].total = sa->count;
		b.arrays[ai].elem_type = et;
		free(b.arrays[ai].elems);
		b.arrays[ai].elems = sa->count > 0 ? xcalloc(sa->count, sizeof(int32_t)) : NULL;
		for (uint32_t k = 0; k < sa->count; k++) {
			int32_t ev = gd_read_i32_le(sa->elems_le + k * 4u);
			if (is_struct_elem && ev == 0)
				ev = -1;
			b.arrays[ai].elems[k] = ev;
		}
	}

	gd_ss_builder_set_comment(&b, (const uint8_t *)parsed.comment, parsed.comment_len, parsed.has_comment);
	bool ok = gd_ss_builder_write_file(&b, dst_path, false);

	free(new_schema_ids);
	gd_ss_builder_free(&b);
	gd_ss_free_parsed(&parsed);
	return ok;
}

static bool gd_ss_repair_raw_with_ain(uint8_t *gd_raw, uint32_t gd_ulen, const char *dst_path,
		const struct ain *ext_ain)
{
	if (!ext_ain)
		return gd_ss_repair_raw_nulls(gd_raw, gd_ulen, dst_path);

	struct gd_ss_parsed parsed;
	if (!gd_ss_parse(gd_raw, gd_ulen, &parsed)) {
		free(gd_raw);
		return false;
	}

	struct gd_ss_builder b;
	gd_ss_builder_init(&b);

	/* Preserve C3 string pool 1-to-1 by index */
	if (parsed.nr_strings > 0) {
		b.cap_strings = parsed.nr_strings;
		b.nr_strings = parsed.nr_strings;
		b.strings = xcalloc(parsed.nr_strings, sizeof(char *));
		b.string_lens = xcalloc(parsed.nr_strings, sizeof(size_t));
		for (uint32_t i = 0; i < parsed.nr_strings; i++) {
			size_t slen = parsed.strings[i].len;
			b.strings[i] = xmalloc(slen + 1);
			if (slen > 0)
				memcpy(b.strings[i], parsed.strings[i].ptr, slen);
			b.strings[i][slen] = '\0';
			b.string_lens[i] = slen;
		}
	}

	gd_ss_builder_reserve_objs(&b, max(parsed.nr_objs, 1u));
	gd_ss_builder_reserve_arrays(&b, max(parsed.nr_arrays, 1u));
	gd_ss_builder_set_roots(&b, parsed.root_ids, parsed.nr_roots);

	uint32_t nr_defs = parsed.nr_struct_defs;
	int *schema_snos = xcalloc(max(nr_defs, 1u), sizeof(int));
	int32_t *new_schema_ids = xcalloc(max(nr_defs, 1u), sizeof(int32_t));
	int **schema_old_fi = xcalloc(max(nr_defs, 1u), sizeof(int *));

	for (uint32_t si = 0; si < nr_defs; si++) {
		const struct gd_ss_struct_def *sd = &parsed.struct_defs[si];
		int sno = sd->name ? ext_ain_find_struct(ext_ain, sd->name, strlen(sd->name)) : -1;
		schema_snos[si] = sno;

		if (sno >= 0 && sno < ext_ain->nr_structures) {
			const struct ain_struct *st = &ext_ain->structures[sno];
			uint32_t nf = (uint32_t)st->nr_members;
			struct gd_ss_bschema_field *fields = xcalloc(max(nf, 1u), sizeof(*fields));
			schema_old_fi[si] = xcalloc(max(nf, 1u), sizeof(int));
			for (int m = 0; m < st->nr_members; m++) {
				const char *mname = gd_ss_member_save_name(ext_ain, &st->members[m]);
				fields[m].type1 = (int32_t)st->members[m].type.data;
				fields[m].type2 = gd_ss_compute_type2(&st->members[m].type);
				fields[m].name = (char *)mname;
				fields[m].name_len = strlen(mname);
				if (st->members[m].type.data == AIN_VOID || !mname[0] || !strcmp(mname, "<vtable>"))
					schema_old_fi[si][m] = -1;
				else
					schema_old_fi[si][m] = gd_ss_find_old_field_for_member(sd, &st->members[m]);
			}
			struct gd_ss_bschema cand = {
				.name = st->name ? st->name : "",
				.name_len = st->name ? strlen(st->name) : 0,
				.nr_fields = nf,
				.fields = fields,
			};
			new_schema_ids[si] = gd_ss_builder_intern_schema(&b, &cand);
			free(fields);
		} else {
			struct gd_ss_bschema_field *fields = xcalloc(max(sd->nr_fields, 1u), sizeof(*fields));
			for (uint32_t fi = 0; fi < sd->nr_fields; fi++) {
				const char *fn = sd->fields[fi].name ? sd->fields[fi].name : "";
				fields[fi].type1 = sd->fields[fi].type1;
				fields[fi].type2 = sd->fields[fi].type2;
				fields[fi].name = (char *)fn;
				fields[fi].name_len = strlen(fn);
			}
			struct gd_ss_bschema cand = {
				.name = (char *)(sd->name ? sd->name : ""),
				.name_len = sd->name ? strlen(sd->name) : 0,
				.nr_fields = sd->nr_fields,
				.fields = fields,
			};
			new_schema_ids[si] = gd_ss_builder_intern_schema(&b, &cand);
			free(fields);
		}
	}

	int32_t *arr_repaired_elem_type = xmalloc(max(parsed.nr_arrays, 1u) * sizeof(int32_t));
	for (uint32_t ai = 0; ai < max(parsed.nr_arrays, 1u); ai++)
		arr_repaired_elem_type[ai] = -1;

	for (uint32_t id = 1; id < parsed.nr_objs; id++) {
		const struct gd_ss_obj *old_obj = &parsed.objs[id];
		int32_t sd_idx = old_obj->struct_def;
		if (sd_idx < 0 || (uint32_t)sd_idx >= nr_defs) {
			b.objs[id].struct_def = sd_idx;
			b.objs[id].nr_vals = 0;
			continue;
		}
		const struct gd_ss_struct_def *old_sd = &parsed.struct_defs[sd_idx];
		int32_t new_sd = new_schema_ids[sd_idx];
		int sno = schema_snos[sd_idx];

		if (sno >= 0 && sno < ext_ain->nr_structures) {
			const struct ain_struct *st = &ext_ain->structures[sno];
			uint32_t nf = (uint32_t)st->nr_members;
			b.objs[id].struct_def = new_sd;
			b.objs[id].nr_vals = nf;
			b.objs[id].val_idxs = xcalloc(max(nf, 1u), sizeof(uint32_t));

			for (int m = 0; m < st->nr_members; m++) {
				const char *mname = gd_ss_member_save_name(ext_ain, &st->members[m]);
				const struct ain_type *mt = &st->members[m].type;
				int32_t ft1 = (int32_t)mt->data;
				int32_t ft2 = gd_ss_compute_type2(mt);

				if (mt->data == AIN_VOID || mt->data == AIN_DELEGATE || mt->data == AIN_REF_DELEGATE
						|| !strcmp(mname, "<vtable>")) {
					b.objs[id].val_idxs[m] = 0;
					continue;
				}
				if (!mname[0]) {
					b.objs[id].val_idxs[m] = gd_ss_type_is_struct_or_iface(ft1, ft2)
						? gd_ss_builder_add_val(&b, -1) : 0;
					continue;
				}

				int old_fi = schema_old_fi[sd_idx][m];
				if (mt->data == AIN_OPTION) {
					int w = ext_ain_type_slot_width(ext_ain, mt);
					bool is_none = true;
					int32_t raw_val = -1;
					int32_t mid_val = -1;
					if (old_fi >= 0 && (uint32_t)old_fi < old_obj->nr_vals) {
						uint32_t v_idx = gd_read_u32_le(old_obj->val_idxs_le + (uint32_t)old_fi * 4u);
						if (v_idx < parsed.nr_vals)
							raw_val = gd_read_i32_le(parsed.vals_le + v_idx * 4u);
						uint32_t none_off = (w > 1) ? (uint32_t)(w - 1) : 1u;
						uint32_t old_none_fi = UINT32_MAX;
						if ((uint32_t)old_fi + none_off < old_sd->nr_fields
								&& (uint32_t)old_fi + none_off < old_obj->nr_vals
								&& old_sd->fields[(uint32_t)old_fi + none_off].type1 == AIN_VOID) {
							old_none_fi = (uint32_t)old_fi + none_off;
						} else if ((uint32_t)old_fi + 1u < old_sd->nr_fields
								&& (uint32_t)old_fi + 1u < old_obj->nr_vals
								&& old_sd->fields[(uint32_t)old_fi + 1u].type1 == AIN_VOID) {
							old_none_fi = (uint32_t)old_fi + 1u;
						}
						if (old_none_fi != UINT32_MAX) {
							uint32_t nv_idx = gd_read_u32_le(old_obj->val_idxs_le + old_none_fi * 4u);
							if (nv_idx < parsed.nr_vals)
								is_none = (gd_read_i32_le(parsed.vals_le + nv_idx * 4u) != 0);
						}
						if (w >= 3 && old_none_fi == (uint32_t)old_fi + (uint32_t)(w - 1)) {
							uint32_t mv_idx = gd_read_u32_le(old_obj->val_idxs_le + ((uint32_t)old_fi + 1u) * 4u);
							if (mv_idx < parsed.nr_vals)
								mid_val = gd_read_i32_le(parsed.vals_le + mv_idx * 4u);
						} else if (!is_none) {
							mid_val = 0;
						}
					}
					if (is_none) {
						b.objs[id].val_idxs[m] = gd_ss_builder_add_val(&b, -1);
						for (int k = 1; k < w && m + k < st->nr_members; k++) {
							int32_t sv = (k == w - 1) ? 1 : -1;
							b.objs[id].val_idxs[m + k] = gd_ss_builder_add_val(&b, sv);
						}
					} else {
						if (gd_ss_type_is_struct_or_iface(ft1, ft2) && raw_val == 0)
							raw_val = -1;
						if ((ft2 == AIN_ARRAY || ft2 == AIN_REF_ARRAY) && mt->array_type) {
							const struct ain_type *arr_t = mt->array_type;
							while (arr_t->data == AIN_WRAP && arr_t->array_type)
								arr_t = arr_t->array_type;
							if (arr_t->array_type && raw_val > 0 && (uint32_t)raw_val < parsed.nr_arrays)
								arr_repaired_elem_type[raw_val] = gd_ss_compute_array_elem_type(arr_t->array_type);
						}
						b.objs[id].val_idxs[m] = gd_ss_builder_add_val(&b, raw_val);
						for (int k = 1; k < w && m + k < st->nr_members; k++) {
							int32_t sv = (k == w - 1) ? 0 : (k == 1 ? mid_val : 0);
							b.objs[id].val_idxs[m + k] = gd_ss_builder_add_val(&b, sv);
						}
					}
					if (w > 1)
						m += w - 1;
					continue;
				}

				int32_t raw_val = 0;
				bool found = false;
				if (old_fi >= 0 && (uint32_t)old_fi < old_obj->nr_vals) {
					uint32_t v_idx = gd_read_u32_le(old_obj->val_idxs_le + (uint32_t)old_fi * 4u);
					if (v_idx < parsed.nr_vals) {
						raw_val = gd_read_i32_le(parsed.vals_le + v_idx * 4u);
						found = true;
					}
				}
				int32_t eff_t = (ft1 == AIN_WRAP) ? ft2 : ft1;
				if (!found) {
					if (gd_ss_type_is_struct_or_iface(ft1, ft2)
							|| eff_t == AIN_ARRAY || eff_t == AIN_REF_ARRAY)
						raw_val = -1;
					else if (eff_t == AIN_STRING || eff_t == AIN_REF_STRING)
						raw_val = INT32_MAX;
					else
						raw_val = 0;
				} else {
					if (gd_ss_type_is_struct_or_iface(ft1, ft2) && raw_val == 0)
						raw_val = -1;
					if ((eff_t == AIN_ARRAY || eff_t == AIN_REF_ARRAY) && raw_val > 0
							&& (uint32_t)raw_val < parsed.nr_arrays) {
						const struct ain_type *arr_t = mt;
						while (arr_t->data == AIN_WRAP && arr_t->array_type)
							arr_t = arr_t->array_type;
						if (arr_t->array_type)
							arr_repaired_elem_type[raw_val] = gd_ss_compute_array_elem_type(arr_t->array_type);
					}
				}
				b.objs[id].val_idxs[m] = gd_ss_builder_add_val(&b, raw_val);
			}
		} else {
			uint32_t nf = old_obj->nr_vals;
			b.objs[id].struct_def = new_sd;
			b.objs[id].nr_vals = nf;
			b.objs[id].val_idxs = xcalloc(max(nf, 1u), sizeof(uint32_t));
			for (uint32_t fi = 0; fi < nf; fi++) {
				uint32_t v_idx = gd_read_u32_le(old_obj->val_idxs_le + fi * 4u);
				int32_t raw_val = (v_idx < parsed.nr_vals)
					? gd_read_i32_le(parsed.vals_le + v_idx * 4u) : 0;
				if (fi < old_sd->nr_fields
						&& gd_ss_type_is_struct_or_iface(old_sd->fields[fi].type1, old_sd->fields[fi].type2)
						&& raw_val == 0) {
					raw_val = -1;
				}
				b.objs[id].val_idxs[fi] = gd_ss_builder_add_val(&b, raw_val);
			}
		}
	}

	for (uint32_t ai = 0; ai < parsed.nr_arrays; ai++) {
		const struct gd_ss_array *sa = &parsed.arrays[ai];
		int32_t et = (arr_repaired_elem_type[ai] >= 0) ? arr_repaired_elem_type[ai] : sa->elem_type;
		bool is_struct_elem = gd_ss_type_is_struct_or_iface(et, et);
		b.arrays[ai].rank = sa->rank > 0 ? sa->rank : 1;
		b.arrays[ai].total = sa->count;
		b.arrays[ai].elem_type = et;
		free(b.arrays[ai].elems);
		b.arrays[ai].elems = sa->count > 0 ? xcalloc(sa->count, sizeof(int32_t)) : NULL;
		for (uint32_t k = 0; k < sa->count; k++) {
			int32_t ev = gd_read_i32_le(sa->elems_le + k * 4u);
			if (is_struct_elem && ev == 0)
				ev = -1;
			b.arrays[ai].elems[k] = ev;
		}
	}

	gd_ss_builder_set_comment(&b, (const uint8_t *)parsed.comment, parsed.comment_len, parsed.has_comment);

	bool ok = gd_ss_builder_write_file(&b, dst_path, false);

	free(arr_repaired_elem_type);
	for (uint32_t si = 0; si < nr_defs; si++)
		free(schema_old_fi[si]);
	free(schema_old_fi);
	free(new_schema_ids);
	free(schema_snos);
	gd_ss_builder_free(&b);
	gd_ss_free_parsed(&parsed);
	return ok;
}

static bool savedata_copy_file_atomic(const char *src_path, const char *dst_path)
{
	FILE *in = file_open_utf8(src_path, "rb");
	if (!in)
		return false;
	char *tmp_path = NULL;
	FILE *out = atomic_save_open(dst_path, "wb", &tmp_path);
	if (!out) {
		fclose(in);
		return false;
	}
	uint8_t buf[16384];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			ok = false;
			break;
		}
	}
	if (ferror(in))
		ok = false;
	fclose(in);
	if (!ok) {
		atomic_save_abort(out, tmp_path);
		return false;
	}
	return atomic_save_commit(out, dst_path, tmp_path, false);
}

bool savedata_export_pc_file_with_ain(const char *src_path, const char *dst_path, const char *ain_path)
{
	if (!src_path || !dst_path)
		return false;

	/* (b) Legacy X4SS container -> convert to Windows GD\x01\x01 SerializeStruct */
	if (x4ss_is_container_file(src_path)) {
		struct ain *ext_ain = NULL;
		if (ain_path && ain_path[0]) {
			int ain_err = 0;
			ext_ain = ain_open(ain_path, &ain_err);
		}
		bool ok = x4ss_convert_to_gdss_file_with_ain(src_path, dst_path, ext_ain);
		if (ext_ain)
			ain_free(ext_ain);
		return ok;
	}

	/* (a) Existing GD\x01\x01 SerializeStruct v9 -> repair null structs, multi-root objs[0], and .ain schemas */
	uint8_t *gd_raw = NULL;
	uint32_t gd_ulen = 0;
	if (gd_ss_decompress_file(src_path, &gd_raw, &gd_ulen)) {
		struct ain *ext_ain = NULL;
		if (ain_path && ain_path[0]) {
			int ain_err = 0;
			ext_ain = ain_open(ain_path, &ain_err);
		}
		bool ok = gd_ss_repair_raw_with_ain(gd_raw, gd_ulen, dst_path, ext_ain);
		if (ext_ain)
			ain_free(ext_ain);
		return ok;
	}

	/* Check GD\x01\x01 RSM resume saves & gsave files */
	enum savefile_error sf_err = SAVEFILE_SUCCESS;
	struct savefile *sf = savefile_read(src_path, &sf_err);
	if (sf) {
		/* (d) Encrypted RSM resume saves -> decrypt to plain zlib for AIN v6+ (or RSM v7+ without .ain) */
		if (sf->len >= 8 && memcmp(sf->buf, "RSM\0", 4) == 0 && gd_read_i32_le(sf->buf + 4) != 1000) {
			int32_t rsm_ver = gd_read_i32_le(sf->buf + 4);
			if (sf->encrypted) {
				bool should_decrypt = (rsm_ver >= 7);
				if (ain_path && ain_path[0]) {
					int ain_err = 0;
					struct ain *ext_ain = ain_open(ain_path, &ain_err);
					if (ext_ain) {
						should_decrypt = AIN_VERSION_GTE(ext_ain, 6, 0);
						ain_free(ext_ain);
					}
				}
				if (should_decrypt) {
					char *tmp_path = NULL;
					FILE *out = atomic_save_open(dst_path, "wb", &tmp_path);
					if (!out) {
						savefile_free(sf);
						return false;
					}
					struct savefile out_sf = {
						.buf = sf->buf,
						.len = sf->len,
						.encrypted = false,
						.compression_level = 1,
					};
					enum savefile_error werr = savefile_write(&out_sf, out);
					savefile_free(sf);
					if (werr != SAVEFILE_SUCCESS) {
						atomic_save_abort(out, tmp_path);
						return false;
					}
					return atomic_save_commit(out, dst_path, tmp_path, false);
				}
			}
			savefile_free(sf);
			return savedata_copy_file_atomic(src_path, dst_path);
		}

		/* (c) gsave with <src_path>.comment sidecar -> fold into gs->comments[0] (gsave v8)
		 *     only when the gsave does not already have a non-empty embedded comment. */
		char *cpath = make_comment_sidecar_path(src_path);
		if (file_exists(cpath)) {
			struct gsave *gs = xcalloc(1, sizeof(struct gsave));
			if (gsave_parse(sf->buf, sf->len, gs) == SAVEFILE_SUCCESS) {
				bool has_embedded_comment = (gs->nr_comments > 0 && gs->comments
						&& gs->comments[0] && gs->comments[0]->size > 0
						&& gs->comments[0]->text[0] != '\0');
				if (!has_embedded_comment) {
					size_t clen = 0;
					uint8_t *cdata = file_read(cpath, &clen);
					free(cpath);
					if (cdata && gsave_upgrade_and_set_comment(gs, (const char *)cdata, clen)) {
						free(cdata);
						char *tmp_path = NULL;
						FILE *out = atomic_save_open(dst_path, "wb", &tmp_path);
						if (!out) {
							gsave_free(gs);
							savefile_free(sf);
							return false;
						}
						bool encrypt = (gs->version <= 5) ? sf->encrypted : false;
						int comp_level = (gs->version >= 7) ? 1 : 9;
						enum savefile_error werr = gsave_write(gs, out, encrypt, comp_level);
						gsave_free(gs);
						savefile_free(sf);
						if (werr != SAVEFILE_SUCCESS) {
							atomic_save_abort(out, tmp_path);
							return false;
						}
						return atomic_save_commit(out, dst_path, tmp_path, false);
					}
					free(cdata);
					gsave_free(gs);
					savefile_free(sf);
					return savedata_copy_file_atomic(src_path, dst_path);
				}
			}
			gsave_free(gs);
		}
		free(cpath);
		savefile_free(sf);
	}

	/* (e) All other files (PSR\0, CSD\0, QNT, MsgSkip.asd, plain gsave, unencrypted RSM, etc.) */
	return savedata_copy_file_atomic(src_path, dst_path);
}

/* Self-contained .ain-free / utfsjis-free / savefile-free PC export path (used by Switch Hub). */
bool savedata_export_pc_file(const char *src_path, const char *dst_path)
{
	if (!src_path || !dst_path)
		return false;
	if (x4ss_is_container_file(src_path))
		return x4ss_convert_to_gdss_file(src_path, dst_path);
	uint8_t *gd_raw = NULL;
	uint32_t gd_ulen = 0;
	if (gd_ss_decompress_file(src_path, &gd_raw, &gd_ulen))
		return gd_ss_repair_raw_nulls(gd_raw, gd_ulen, dst_path);
	return savedata_copy_file_atomic(src_path, dst_path);
}

static bool buf_contains_bytes(const uint8_t *haystack, size_t hlen, const uint8_t *needle, size_t nlen)
{
	if (!haystack || !needle || nlen == 0 || hlen < nlen)
		return false;
	for (size_t i = 0; i <= hlen - nlen; i++) {
		if (haystack[i] == needle[0] && memcmp(haystack + i, needle, nlen) == 0)
			return true;
	}
	return false;
}

bool savedata_file_has_xsystem4_only_state(const char *src_path)
{
	if (!src_path)
		return false;
	enum savefile_error err = SAVEFILE_SUCCESS;
	struct savefile *sf = savefile_read(src_path, &err);
	if (!sf)
		return false;
	if (sf->len < 8 || memcmp(sf->buf, "RSM\0", 4) != 0) {
		savefile_free(sf);
		return false;
	}
	int32_t rsm_ver = gd_read_i32_le(sf->buf + 4);
	if (rsm_ver == 1000) {
		savefile_free(sf);
		return false;
	}

	bool has_x4_only = false;
	struct rsave *rs = xcalloc(1, sizeof(struct rsave));
	enum savefile_error perr = rsave_parse(sf->buf, sf->len, RSAVE_READ_ALL, rs);
	if (perr == SAVEFILE_SUCCESS) {
		if (!rs->comments_only) {
			for (int32_t i = 0; i < rs->nr_heap_objs; i++) {
				enum rsave_heap_tag *tag = rs->heap[i];
				if (!tag || *tag != RSAVE_ARRAY)
					continue;
				const struct rsave_heap_array *a = rs->heap[i];
				if (a->data_type != AIN_INT && a->data_type != AIN_ARRAY_INT && a->data_type != AIN_ARRAY)
					continue;
				/* PartsEngine.Save writes iarray header "XPE\0" as 4 int32 elements */
				if (a->nr_slots >= 4
						&& a->slots[0] == 'X' && a->slots[1] == 'P'
						&& a->slots[2] == 'E' && a->slots[3] == 0) {
					has_x4_only = true;
					break;
				}
				/* PartsEngine.SaveParts writes iarray header "XPP2\0" as 5 int32 elements */
				if (a->nr_slots >= 5
						&& a->slots[0] == 'X' && a->slots[1] == 'P'
						&& a->slots[2] == 'P' && a->slots[3] == '2' && a->slots[4] == 0) {
					has_x4_only = true;
					break;
				}
				/* Packed 4-byte int32 magic ("XPE\0" = 0x00455058, "XPP2" = 0x32505058) */
				if (a->nr_slots >= 1
						&& (a->slots[0] == 0x00455058 || a->slots[0] == 0x32505058)) {
					has_x4_only = true;
					break;
				}
			}
		}
	} else if (perr == SAVEFILE_UNSUPPORTED_FORMAT && rsm_ver >= 1 && rsm_ver <= 64) {
		static const uint8_t xpe_iarray[16] = {
			'X', 0, 0, 0,  'P', 0, 0, 0,  'E', 0, 0, 0,  0, 0, 0, 0
		};
		static const uint8_t xpp2_iarray[20] = {
			'X', 0, 0, 0,  'P', 0, 0, 0,  'P', 0, 0, 0,  '2', 0, 0, 0,  0, 0, 0, 0
		};
		if (buf_contains_bytes(sf->buf, sf->len, xpe_iarray, sizeof(xpe_iarray))
				|| buf_contains_bytes(sf->buf, sf->len, xpp2_iarray, sizeof(xpp2_iarray))) {
			has_x4_only = true;
		}
	}

	rsave_free(rs);
	savefile_free(sf);
	return has_x4_only;
}
