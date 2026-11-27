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
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "system4.h"
#include "system4/ald.h"
#include "system4/afa.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "savedata.h"
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
	case ASSET_FLASH: return "Flash";
	}
	return "Invalid";
}

struct asset_manager {
	bool (*load_archive)(struct asset_manager *manager, const char *name);
	bool (*exists_by_id)(struct asset_manager *manager, int id);
	bool (*exists_by_name)(struct asset_manager *manager, const char *name, int *id_out);
	struct archive_data *(*get_by_id)(struct asset_manager *manager, int id);
	struct archive_data *(*get_by_name)(struct asset_manager *manager, const char *name, int *id_out);
};

// XXX: also used for AFAv1 in Daiteikoku, Shaman's Sanctuary
struct asset_manager_ald {
	struct asset_manager manager;
	struct archive *archive;
	struct afa_archive **cg_overlays;
	size_t nr_cg_overlays;
	struct archive *loose_cg_override;
};

enum afa_archive_priority {
	AFA_PRIO_BACKUP     = 0,
	AFA_PRIO_BASE       = 1,
	AFA_PRIO_PATCH      = 2,
	AFA_PRIO_MOD        = 3,
	AFA_PRIO_UNCENSORED = 4,
};

struct afa_canon_entry {
	struct afa_archive *ar;
	struct afa_entry *entry;
	int prio;
};

struct asset_manager_afa {
	struct asset_manager manager;
	struct afa_archive **archives;
	uint8_t *archive_priorities;
	size_t nr_archives;
	struct afa_archive **id_archives;
	size_t nr_id_archives;
	struct hash_table *canon_basename_table;
	struct afa_canon_entry *canon_entries;
	size_t nr_canon_entries;
	size_t cap_canon_entries;
	bool canon_index_valid;
	struct hash_table *miss_table;
	size_t nr_miss_entries;
};

static struct asset_manager *assets[ASSET_TYPE_MAX] = {0};
static struct asset_manager_afa *afa_create_manager(void);
static _Atomic uint64_t cg_archive_gen = 1;

uint64_t asset_cg_archive_gen(void)
{
	return atomic_load_explicit(&cg_archive_gen, memory_order_acquire);
}

static inline void asset_cg_bump_archive_gen(void)
{
	atomic_fetch_add_explicit(&cg_archive_gen, 1, memory_order_release);
}

// Serializes ASSET_CG archive index lookup and raw byte reads across the main
// thread and PartsEngine CG decode worker threads. Never held during CPU decode
// (cg_load_data) so workers decompress in parallel and recursive base CG loads
// (DCF / WebP OVER) cannot deadlock.
#ifdef _WIN32
static SRWLOCK asset_cg_lock = SRWLOCK_INIT;
static inline void asset_cg_archive_lock(enum asset_type type)
{
	if (type == ASSET_CG)
		AcquireSRWLockExclusive(&asset_cg_lock);
}
static inline void asset_cg_archive_unlock(enum asset_type type)
{
	if (type == ASSET_CG)
		ReleaseSRWLockExclusive(&asset_cg_lock);
}
#else
static pthread_mutex_t asset_cg_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void asset_cg_archive_lock(enum asset_type type)
{
	if (type == ASSET_CG)
		pthread_mutex_lock(&asset_cg_lock);
}
static inline void asset_cg_archive_unlock(enum asset_type type)
{
	if (type == ASSET_CG)
		pthread_mutex_unlock(&asset_cg_lock);
}
#endif

bool asset_manager_load_archive(enum asset_type type, const char *archive_name)
{
	if (type < 0 || type >= ASSET_TYPE_MAX) return false;
	char rel_name[PATH_MAX];
	snprintf(rel_name, sizeof(rel_name), "%s.afa", archive_name);
	char *path = gamedir_path_icase(rel_name);
	if (!path || !file_exists(path)) {
		free(path);
		path = gamedir_path(archive_name);
	}
	asset_cg_archive_lock(type);
	if (!assets[type]) assets[type] = &afa_create_manager()->manager;
	if (!assets[type]->load_archive) {
		asset_cg_archive_unlock(type);
		free(path);
		ERROR("load_archive not supported on this archive type");
	}
	bool r = assets[type]->load_archive(assets[type], path);
	free(path);
	if (r && type == ASSET_CG) {
		size_t len = strlen(archive_name);
		if (len >= 3 && !strcasecmp(archive_name + len - 3, "CG5")) {
			char cg6_rel[PATH_MAX];
			snprintf(cg6_rel, sizeof(cg6_rel), "%.*s6.afa", (int)(len - 1), archive_name);
			char *cg6_path = gamedir_path_icase(cg6_rel);
			if (cg6_path && file_exists(cg6_path))
				assets[type]->load_archive(assets[type], cg6_path);
			free(cg6_path);
		}
	}
	asset_cg_archive_unlock(type);
	return r;
}

#define SAVE_CG_ID_TAG 0x7f000000u

static char **save_cg_names = NULL;
static size_t nr_save_cg_names = 0;
static size_t cap_save_cg_names = 0;

static char *asset_cg_save_path(const char *name)
{
	if (!name || strncmp(name, "<save>", 6) != 0)
		return NULL;
	const char *rel = name + 6;
	while (*rel == '/' || *rel == '\\')
		rel++;
	char *resolved = savedir_path(rel);
	if (resolved && !file_exists(resolved)) {
		char *icase = path_get_icase(resolved);
		if (icase) {
			free(resolved);
			resolved = icase;
		}
	}
	return resolved;
}

static bool is_save_cg_id(int id)
{
	return id > 0 && (((uint32_t)id & SAVE_CG_ID_TAG) == SAVE_CG_ID_TAG) && ((uint32_t)id & 0x00ffffffu) != 0;
}

static int save_cg_intern_name(const char *name)
{
	for (size_t i = 0; i < nr_save_cg_names; i++) {
		if (!strcmp(save_cg_names[i], name))
			return (int)(SAVE_CG_ID_TAG | (uint32_t)(i + 1));
	}
	if (nr_save_cg_names >= 0x00fffffeu)
		return 0;
	if (nr_save_cg_names == cap_save_cg_names) {
		size_t new_cap = cap_save_cg_names ? cap_save_cg_names * 2 : 32;
		save_cg_names = xrealloc(save_cg_names, new_cap * sizeof(*save_cg_names));
		cap_save_cg_names = new_cap;
	}
	save_cg_names[nr_save_cg_names] = xstrdup(name);
	nr_save_cg_names++;
	return (int)(SAVE_CG_ID_TAG | (uint32_t)nr_save_cg_names);
}

static char *save_cg_resolve_id(int id)
{
	if (!is_save_cg_id(id))
		return NULL;
	size_t idx = (size_t)((uint32_t)id & 0x00ffffffu) - 1;
	if (idx >= nr_save_cg_names)
		return NULL;
	return asset_cg_save_path(save_cg_names[idx]);
}

bool asset_exists(enum asset_type type, int id)
{
	if (type < 0 || type >= ASSET_TYPE_MAX)
		return false;
	if (type == ASSET_CG && is_save_cg_id(id)) {
		asset_cg_archive_lock(type);
		char *save_path = save_cg_resolve_id(id);
		asset_cg_archive_unlock(type);
		bool exists = save_path && file_exists(save_path);
		free(save_path);
		return exists;
	}
	asset_cg_archive_lock(type);
	bool exists = assets[type] ? assets[type]->exists_by_id(assets[type], id) : false;
	asset_cg_archive_unlock(type);
	return exists;
}

static bool asset_cg_format_special_org_fallback(const char *name, char *buf, size_t buf_size)
{
	const char *needle = sys4_text_is_gbk
		? "\xcc\xd8\xca\xe2\xcb\xf9\xca\xf4"  /* 特殊所属 (GBK) */
		: "\x93\xc1\x8e\xea\x8f\x8a\x91\xae"; /* 特殊所属 (SJIS) */
	const char *repl = sys4_text_is_gbk
		? "\xc6\xe4\xcb\xfb\xcb\xf9\xca\xf4"          /* 其他所属 (GBK) */
		: "\x82\xbb\x82\xcc\x91\xbc\x8f\x8a\x91\xae"; /* その他所属 (SJIS) */
	size_t needle_len = 8;
	size_t repl_len = sys4_text_is_gbk ? 8 : 10;

	for (const char *p = name; *p; p = sjis_skip_char(p)) {
		if (!memcmp(p, needle, needle_len)) {
			size_t prefix_len = (size_t)(p - name);
			const char *suffix = p + needle_len;
			size_t suffix_len = strlen(suffix);
			if (prefix_len + repl_len + suffix_len + 1 > buf_size)
				return false;
			memcpy(buf, name, prefix_len);
			memcpy(buf + prefix_len, repl, repl_len);
			memcpy(buf + prefix_len + repl_len, suffix, suffix_len + 1);
			return true;
		}
	}
	return false;
}

bool asset_exists_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (type < 0 || type >= ASSET_TYPE_MAX || !name || !name[0])
		return false;
	if (type == ASSET_CG) {
		char *save_path = asset_cg_save_path(name);
		if (save_path) {
			bool exists = file_exists(save_path);
			free(save_path);
			if (exists && id_out) {
				asset_cg_archive_lock(type);
				*id_out = save_cg_intern_name(name);
				asset_cg_archive_unlock(type);
			}
			return exists;
		}
	}
	asset_cg_archive_lock(type);
	if (!assets[type]) {
		asset_cg_archive_unlock(type);
		return false;
	}
	if (!assets[type]->exists_by_name) {
		asset_cg_archive_unlock(type);
		ERROR("exists_by_name not supported on this archive type");
	}
	bool r = assets[type]->exists_by_name(assets[type], name, id_out);
	if (!r && type == ASSET_CG) {
		char fallback[512];
		if (asset_cg_format_special_org_fallback(name, fallback, sizeof(fallback)))
			r = assets[type]->exists_by_name(assets[type], fallback, id_out);
	}
	asset_cg_archive_unlock(type);
	return r;
}

struct archive_data *asset_get(enum asset_type type, int id)
{
	if (type < 0 || type >= ASSET_TYPE_MAX)
		return NULL;
	asset_cg_archive_lock(type);
	struct archive_data *data = assets[type] ? assets[type]->get_by_id(assets[type], id) : NULL;
	asset_cg_archive_unlock(type);
	return data;
}

struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (type < 0 || type >= ASSET_TYPE_MAX || !name || !name[0])
		return NULL;
	asset_cg_archive_lock(type);
	if (!assets[type]) {
		asset_cg_archive_unlock(type);
		return NULL;
	}
	if (!assets[type]->get_by_name) {
		asset_cg_archive_unlock(type);
		ERROR("get_by_name not supported on this archive type");
	}
	struct archive_data *data = assets[type]->get_by_name(assets[type], name, id_out);
	if (!data && type == ASSET_CG) {
		char fallback[512];
		if (asset_cg_format_special_org_fallback(name, fallback, sizeof(fallback)))
			data = assets[type]->get_by_name(assets[type], fallback, id_out);
	}
	asset_cg_archive_unlock(type);
	return data;
}

static void localize_sengoku_skip_cg(int id, const struct archive_data *data, struct cg *cg)
{
	// CG 10005 contains three 36x20 frames of the Japanese skip button.
	// Match the original graphic, not just its ID, so translated and modded art wins.
	if (id != 10005 || !sys4_text_is_gbk
			|| data->size != 3451 || crc32(0, data->data, data->size) != 0xdd729ed3
			|| !cg || !cg->pixels || cg->type != ALCG_QNT
			|| cg->metrics.w != 108 || cg->metrics.h != 20)
		return;

	// 快进 rasterized from HanaMinA, with two bits of coverage per pixel.
	static const uint64_t label[] = {
		0x06180100028060, 0x071c0a00028070, 0x1b1d2c00679470, 0x2fae9400b7d5f8,
		0x071c000072877d, 0x071c1a40b2857a, 0x7b6d6d42fbe976, 0x5bae6800078470,
		0x070c280007c070, 0x070a28001dd070, 0x07027d00747470, 0x6aabd782d01d70,
		0x7ffe02814002a0,
	};
	static const uint8_t background[] = { 226, 194, 180 };
	static const uint8_t ink[][3] = { { 177, 103, 68 }, { 206, 158, 135 }, { 177, 103, 68 } };
	uint8_t *pixels = cg->pixels;
	for (int frame = 0; frame < 3; frame++) {
		int left = 36 * frame + 2 + (frame == 2);
		int top = 3 + (frame == 2);
		for (int y = 0; y < 13; y++) {
			for (int x = 0; x < 30; x++) {
				unsigned coverage = x > 0 && x < 29 ? (label[y] >> (2 * (x - 1))) & 3 : 0;
				uint8_t *pixel = pixels + 4 * ((top + y) * 108 + left + x);
				for (int c = 0; c < 3; c++) {
					pixel[c] = (ink[frame][c] * coverage + background[c] * (3 - coverage) + 1) / 3;
				}
			}
		}
	}
}

struct cg *asset_cg_load(int id)
{
	if (is_save_cg_id(id)) {
		asset_cg_archive_lock(ASSET_CG);
		char *save_path = save_cg_resolve_id(id);
		asset_cg_archive_unlock(ASSET_CG);
		struct cg *cg = (save_path && file_exists(save_path)) ? cg_load_file(save_path) : NULL;
		free(save_path);
		return cg;
	}
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	localize_sengoku_skip_cg(id, data, cg);
	archive_free_data(data);
	return cg;
}

bool asset_cg_supports_name(void)
{
	asset_cg_archive_lock(ASSET_CG);
	bool supported = assets[ASSET_CG] && assets[ASSET_CG]->get_by_name != NULL;
	asset_cg_archive_unlock(ASSET_CG);
	return supported;
}

struct cg *asset_cg_load_by_name(const char *name, int *id_out)
{
	char *save_path = asset_cg_save_path(name);
	if (save_path) {
		struct cg *cg = file_exists(save_path) ? cg_load_file(save_path) : NULL;
		free(save_path);
		if (cg && id_out) {
			asset_cg_archive_lock(ASSET_CG);
			*id_out = save_cg_intern_name(name);
			asset_cg_archive_unlock(ASSET_CG);
		}
		return cg;
	}
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, id_out);
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	return cg;
}

bool asset_cg_get_metrics(int id, struct cg_metrics *metrics)
{
	if (is_save_cg_id(id)) {
		asset_cg_archive_lock(ASSET_CG);
		char *save_path = save_cg_resolve_id(id);
		asset_cg_archive_unlock(ASSET_CG);
		bool r = (save_path && file_exists(save_path)) ? cg_get_metrics_file(save_path, metrics) : false;
		free(save_path);
		return r;
	}
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

bool asset_cg_get_metrics_by_name(const char *name, struct cg_metrics *metrics)
{
	char *save_path = asset_cg_save_path(name);
	if (save_path) {
		bool r = file_exists(save_path) ? cg_get_metrics_file(save_path, metrics) : false;
		free(save_path);
		return r;
	}
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, NULL);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

struct loose_cg_entry {
	char *path;
	char *name;
};

struct loose_cg_archive {
	struct archive ar;
	struct hash_table *by_id;
};

static int compare_cstr_ptr(const void *a, const void *b)
{
	const char *sa = *(const char * const *)a;
	const char *sb = *(const char * const *)b;
	return strcmp(sa, sb);
}

static void loose_cg_entry_free(void *p)
{
	struct loose_cg_entry *entry = p;
	if (!entry)
		return;
	free(entry->path);
	free(entry->name);
	free(entry);
}

static bool is_supported_loose_cg_ext(const char *ext)
{
	static const char * const exts[] = {
		"qnt", "pms", "ajp", "dcf", "webp", "png", "jpg", "jpeg"
	};
	for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
		if (!strcasecmp(ext, exts[i]))
			return true;
	}
	return false;
}

static bool parse_loose_cg_id(const char *filename, int *id_out)
{
	if (!filename)
		return false;
	if ((filename[0] != 'c' && filename[0] != 'C') ||
	    (filename[1] != 'g' && filename[1] != 'G'))
		return false;
	const char *p = filename + 2;
	if (*p < '0' || *p > '9')
		return false;
	int id = 0;
	while (*p >= '0' && *p <= '9') {
		int digit = *p - '0';
		if (id > (INT_MAX - digit) / 10)
			return false;
		id = id * 10 + digit;
		p++;
	}
	if (id <= 0 || *p != '.')
		return false;
	if (!is_supported_loose_cg_ext(p + 1))
		return false;
	*id_out = id;
	return true;
}

static bool loose_cg_exists(struct archive *_ar, int no)
{
	struct loose_cg_archive *ar = (struct loose_cg_archive *)_ar;
	return no >= 0 && no < INT_MAX && ht_get_int(ar->by_id, no + 1, NULL) != NULL;
}

static struct archive_data *loose_cg_get(struct archive *_ar, int no)
{
	if (no < 0 || no >= INT_MAX)
		return NULL;
	struct loose_cg_archive *ar = (struct loose_cg_archive *)_ar;
	struct loose_cg_entry *entry = ht_get_int(ar->by_id, no + 1, NULL);
	if (!entry)
		return NULL;
	size_t size = 0;
	uint8_t *buf = file_read(entry->path, &size);
	if (!buf || size == 0) {
		free(buf);
		return NULL;
	}
	struct archive_data *d = xcalloc(1, sizeof(*d));
	d->size = size;
	d->data = buf;
	d->name = xstrdup(entry->name);
	d->no = no;
	d->archive = _ar;
	return d;
}

static void loose_cg_free_data(struct archive_data *data)
{
	free(data->data);
	free(data->name);
	free(data);
}

static void loose_cg_free(struct archive *_ar)
{
	struct loose_cg_archive *ar = (struct loose_cg_archive *)_ar;
	if (ar->by_id) {
		ht_foreach_value(ar->by_id, loose_cg_entry_free);
		ht_free_int(ar->by_id);
	}
	free(ar);
}

static struct archive_ops loose_cg_archive_ops = {
	.exists = loose_cg_exists,
	.get = loose_cg_get,
	.free_data = loose_cg_free_data,
	.free = loose_cg_free,
};

static struct archive *loose_cg_open_dir(const char *dir_path)
{
	UDIR *dir = opendir_utf8(dir_path);
	if (!dir)
		return NULL;

	char **names = NULL;
	size_t nr_names = 0;
	size_t cap_names = 0;
	char *d_name;
	while ((d_name = readdir_utf8(dir))) {
		int id = 0;
		if (d_name[0] == '.' || !parse_loose_cg_id(d_name, &id)) {
			free(d_name);
			continue;
		}
		char *full_path = path_join(dir_path, d_name);
		ustat st;
		bool is_reg = stat_utf8(full_path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
		free(full_path);
		if (!is_reg) {
			free(d_name);
			continue;
		}
		if (nr_names == cap_names) {
			size_t new_cap = cap_names ? cap_names * 2 : 64;
			names = xrealloc(names, new_cap * sizeof(*names));
			cap_names = new_cap;
		}
		names[nr_names++] = d_name;
	}
	closedir_utf8(dir);

	if (nr_names == 0) {
		free(names);
		return NULL;
	}

	if (nr_names > 1)
		qsort(names, nr_names, sizeof(*names), compare_cstr_ptr);

	struct loose_cg_archive *ar = xcalloc(1, sizeof(*ar));
	ar->ar.ops = &loose_cg_archive_ops;
	ar->by_id = ht_create(max((size_t)64, nr_names * 2));

	for (size_t i = 0; i < nr_names; i++) {
		int id = 0;
		if (!parse_loose_cg_id(names[i], &id)) {
			free(names[i]);
			continue;
		}
		struct loose_cg_entry *entry = xmalloc(sizeof(*entry));
		entry->path = path_join(dir_path, names[i]);
		entry->name = names[i];
		struct ht_slot *slot = ht_put_int(ar->by_id, id, NULL);
		if (slot->value)
			loose_cg_entry_free(slot->value);
		slot->value = entry;
	}
	free(names);
	return &ar->ar;
}

static bool ald_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	if (id > 0) {
		for (size_t i = 0; i < manager->nr_cg_overlays; i++) {
			if (!manager->cg_overlays[i]->has_number)
				continue;
			if (archive_exists(&manager->cg_overlays[i]->ar, id - 1))
				return true;
		}
		if (manager->loose_cg_override && archive_exists(manager->loose_cg_override, id - 1))
			return true;
	}
	return archive_exists(manager->archive, id - 1);
}

static struct archive_data *ald_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	if (id > 0) {
		for (size_t i = 0; i < manager->nr_cg_overlays; i++) {
			if (!manager->cg_overlays[i]->has_number)
				continue;
			struct archive_data *data = archive_get(&manager->cg_overlays[i]->ar, id - 1);
			if (data)
				return data;
		}
		if (manager->loose_cg_override) {
			struct archive_data *data = archive_get(manager->loose_cg_override, id - 1);
			if (data)
				return data;
		}
	}
	return archive_get(manager->archive, id - 1);
}

static bool ald_attach_cg_overlay(struct asset_manager_ald *manager, const char *path)
{
	for (size_t i = 0; i < manager->nr_cg_overlays; i++) {
		if (!strcasecmp(manager->cg_overlays[i]->filename, path))
			return true;
	}
	int error;
	struct afa_archive *ar = afa_open(path, MMAP_IF_64BIT, &error);
	if (!ar) {
		WARNING("Failed to open CG overlay archive: %s", display_utf0(path));
		return false;
	}
	manager->cg_overlays = xrealloc(manager->cg_overlays,
		(manager->nr_cg_overlays + 1) * sizeof(*manager->cg_overlays));
	manager->cg_overlays[manager->nr_cg_overlays++] = ar;
	return true;
}

static bool afa_is_backup_archive(const char *filepath)
{
	size_t len = strlen(filepath);
	return len >= 12 && !strcasecmp(filepath + len - 12, ".vanilla.bak");
}

static bool afa_is_uncensored_archive(const char *filepath)
{
	const char *base = strrchr(filepath, '/');
	base = base ? base + 1 : filepath;
	if (!base[0] || base[0] == '.' || afa_is_backup_archive(base))
		return false;
	size_t len = strlen(base);
	if (len < 14 || strcasecmp(base + len - 4, ".afa") != 0)
		return false;
	static const char needle[] = "uncensored";
	for (const char *p = base; *p; p++) {
		size_t k = 0;
		while (needle[k] && p[k] && tolower((unsigned char)p[k]) == needle[k])
			k++;
		if (needle[k] == '\0')
			return true;
	}
	return false;
}

static bool afa_is_mod_archive(const char *filepath)
{
	const char *base = strrchr(filepath, '/');
	base = base ? base + 1 : filepath;
	for (const char *p = base; p[0] && p[1] && p[2]; p++) {
		if (tolower((unsigned char)p[0]) == 'm' &&
		    tolower((unsigned char)p[1]) == 'o' &&
		    tolower((unsigned char)p[2]) == 'd')
			return true;
		if (p[3] &&
		    tolower((unsigned char)p[0]) == 'b' &&
		    tolower((unsigned char)p[1]) == 'f' &&
		    tolower((unsigned char)p[2]) == 'y' &&
		    tolower((unsigned char)p[3]) == 'y')
			return true;
	}
	return false;
}

static int afa_default_priority(const char *filepath)
{
	if (afa_is_backup_archive(filepath))
		return AFA_PRIO_BACKUP;
	if (afa_is_uncensored_archive(filepath))
		return AFA_PRIO_UNCENSORED;
	if (afa_is_mod_archive(filepath))
		return AFA_PRIO_MOD;
	return AFA_PRIO_BASE;
}

static size_t afa_find_insert_pos(struct asset_manager_afa *manager, int prio)
{
	size_t pos = 0;
	while (pos < manager->nr_archives && manager->archive_priorities[pos] > prio)
		pos++;
	return pos;
}

static void afa_invalidate_canon_index(struct asset_manager_afa *manager)
{
	if (manager->canon_basename_table) {
		ht_free(manager->canon_basename_table);
		manager->canon_basename_table = NULL;
	}
	if (manager->miss_table) {
		ht_free(manager->miss_table);
		manager->miss_table = NULL;
	}
	manager->nr_miss_entries = 0;
	free(manager->canon_entries);
	manager->canon_entries = NULL;
	manager->nr_canon_entries = 0;
	manager->cap_canon_entries = 0;
	manager->canon_index_valid = false;
}

static size_t afa_register_id_archive(struct asset_manager_afa *manager, struct afa_archive *ar)
{
	for (size_t i = 0; i < manager->nr_id_archives; i++) {
		if (manager->id_archives[i] == ar)
			return i;
	}
	manager->id_archives = xrealloc(manager->id_archives,
		(manager->nr_id_archives + 1) * sizeof(*manager->id_archives));
	manager->id_archives[manager->nr_id_archives] = ar;
	return manager->nr_id_archives++;
}

static bool afa_load_archive_prio(struct asset_manager_afa *manager, const char *name, int prio)
{
	char path[PATH_MAX];
	size_t name_len = strlen(name);
	if (name_len >= 4 && !strcasecmp(name + name_len - 4, ".afa"))
		snprintf(path, PATH_MAX, "%s", name);
	else
		snprintf(path, PATH_MAX, "%s.afa", name);
	for (size_t i = 0; i < manager->nr_archives; i++) {
		if (!strcasecmp(manager->archives[i]->filename, path))
			return true;
	}

	int error;
	struct afa_archive *ar = afa_open(path, MMAP_IF_64BIT, &error);
	if (!ar) {
		snprintf(path, PATH_MAX, "%s.AFA", name);
		ar = afa_open(path, MMAP_IF_64BIT, &error);
		if (!ar) {
			WARNING("Failed to open archive: %s", display_utf0(path));
			return false;
		}
	}

	if (prio < 0)
		prio = afa_default_priority(path);
	size_t pos = afa_find_insert_pos(manager, prio);
	manager->archives = xrealloc(manager->archives,
		(manager->nr_archives + 1) * sizeof(*manager->archives));
	manager->archive_priorities = xrealloc(manager->archive_priorities,
		(manager->nr_archives + 1) * sizeof(*manager->archive_priorities));
	memmove(manager->archives + pos + 1, manager->archives + pos,
		(manager->nr_archives - pos) * sizeof(*manager->archives));
	memmove(manager->archive_priorities + pos + 1, manager->archive_priorities + pos,
		(manager->nr_archives - pos) * sizeof(*manager->archive_priorities));
	manager->archives[pos] = ar;
	manager->archive_priorities[pos] = (uint8_t)prio;
	manager->nr_archives++;
	afa_register_id_archive(manager, ar);
	afa_invalidate_canon_index(manager);
	if (&manager->manager == assets[ASSET_CG])
		asset_cg_bump_archive_gen();
	return true;
}

static bool afa_load_archive(struct asset_manager *_manager, const char *name)
{
	return afa_load_archive_prio((struct asset_manager_afa*)_manager, name, -1);
}

static int afa_encode_id(struct asset_manager_afa *manager, struct afa_archive *ar, int local_no)
{
	if (local_no < 0 || (uint32_t)local_no >= 0x00fffffeu)
		return 0;
	size_t idx = afa_register_id_archive(manager, ar);
	// 1..126 identify id_archives[ar_tag - 1]; 0 means priority-order scan; 127 is reserved for <save> CGs.
	uint32_t ar_tag = (idx < 126) ? (uint32_t)(idx + 1) : 0u;
	return (int)((ar_tag << 24) | ((uint32_t)(local_no + 1) & 0x00ffffffu));
}

static bool afa_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	if (id <= 0)
		return false;
	uint32_t uid = (uint32_t)id;
	size_t ar_tag = (uid >> 24) & 0x7fu;
	int local_id = (int)(uid & 0x00ffffffu);
	if (local_id <= 0)
		return false;
	if (ar_tag == 0) {
		for (size_t i = 0; i < manager->nr_archives; i++) {
			if (archive_exists(&manager->archives[i]->ar, local_id - 1))
				return true;
		}
		return false;
	}
	size_t idx = ar_tag - 1;
	if (idx >= manager->nr_id_archives)
		return false;
	return archive_exists(&manager->id_archives[idx]->ar, local_id - 1);
}

static struct archive_data *afa_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	if (id <= 0)
		return NULL;
	uint32_t uid = (uint32_t)id;
	size_t ar_tag = (uid >> 24) & 0x7fu;
	int local_id = (int)(uid & 0x00ffffffu);
	if (local_id <= 0)
		return NULL;
	if (ar_tag == 0) {
		for (size_t i = 0; i < manager->nr_archives; i++) {
			struct archive_data *data = archive_get(&manager->archives[i]->ar, local_id - 1);
			if (data)
				return data;
		}
		return NULL;
	}
	size_t idx = ar_tag - 1;
	if (idx >= manager->nr_id_archives)
		return NULL;
	return archive_get(&manager->id_archives[idx]->ar, local_id - 1);
}

static uint64_t afa_entry_timestamp(const struct afa_entry *e)
{
	if (!e->unknown0 && !e->unknown1)
		return 0;
	return ((uint64_t)(e->unknown1 & 0x7fffffffu) << 32) | (uint64_t)e->unknown0;
}

static bool name_has_non_ascii(const char *s)
{
	for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
		if (*p >= 0x80)
			return true;
	}
	return false;
}

static bool afa_candidate_beats(int cur_prio, struct afa_entry *cur_e,
		int cand_prio, struct afa_entry *cand_e)
{
	if (!cur_e)
		return true;
	if (cur_prio != cand_prio)
		return cand_prio > cur_prio;
	if (cur_prio >= AFA_PRIO_MOD)
		return false;

	uint64_t cur_ts = afa_entry_timestamp(cur_e);
	uint64_t cand_ts = afa_entry_timestamp(cand_e);
	if (cur_ts == 0)
		return false;
	if (cand_ts == 0)
		return true;
	return cand_ts > cur_ts;
}

static struct afa_entry *afa_find_best_raw(struct asset_manager_afa *manager,
		const char *name, struct afa_archive **ar_out)
{
	struct afa_archive *best_ar = NULL;
	struct afa_entry *best_entry = NULL;
	int best_prio = -1;

	for (size_t i = 0; i < manager->nr_archives; i++) {
		struct afa_archive *ar = manager->archives[i];
		int prio = manager->archive_priorities[i];
		if (best_entry && prio < best_prio)
			break;

		struct afa_entry *e = afa_lookup_by_basename(ar, name);
		if (!e)
			continue;

		if (prio >= AFA_PRIO_MOD) {
			*ar_out = ar;
			return e;
		}

		if (afa_candidate_beats(best_prio, best_entry, prio, e)) {
			best_ar = ar;
			best_entry = e;
			best_prio = prio;
			if (afa_entry_timestamp(e) == 0) {
				*ar_out = ar;
				return e;
			}
		}
	}

	if (best_entry)
		*ar_out = best_ar;
	return best_entry;
}

static void afa_canon_index_insert(struct asset_manager_afa *manager, const char *key,
		struct afa_archive *ar, struct afa_entry *entry, int prio)
{
	struct ht_slot *slot = ht_put(manager->canon_basename_table, key, NULL);
	if (!slot->value) {
		if (manager->nr_canon_entries == manager->cap_canon_entries) {
			size_t new_cap = manager->cap_canon_entries ? manager->cap_canon_entries * 2 : 256;
			manager->canon_entries = xrealloc(manager->canon_entries,
					new_cap * sizeof(*manager->canon_entries));
			manager->cap_canon_entries = new_cap;
		}
		size_t idx = manager->nr_canon_entries++;
		manager->canon_entries[idx].ar = ar;
		manager->canon_entries[idx].entry = entry;
		manager->canon_entries[idx].prio = prio;
		slot->value = (void *)(uintptr_t)(idx + 1);
		return;
	}
	size_t idx = (size_t)(uintptr_t)slot->value - 1;
	if (afa_candidate_beats(manager->canon_entries[idx].prio, manager->canon_entries[idx].entry, prio, entry)) {
		manager->canon_entries[idx].ar = ar;
		manager->canon_entries[idx].entry = entry;
		manager->canon_entries[idx].prio = prio;
	}
}

static void afa_ensure_canon_index(struct asset_manager_afa *manager)
{
	if (manager->canon_index_valid)
		return;
	afa_invalidate_canon_index(manager);

	size_t total_files = 0;
	for (size_t i = 0; i < manager->nr_archives; i++)
		total_files += manager->archives[i]->nr_files;
	manager->canon_basename_table = ht_create(max((size_t)64, total_files * 2));

	for (size_t i = 0; i < manager->nr_archives; i++) {
		struct afa_archive *ar = manager->archives[i];
		int prio = manager->archive_priorities[i];
		for (uint32_t j = 0; j < ar->nr_files; j++) {
			const char *ename = ar->files[j].name->text;
			if (!name_has_non_ascii(ename))
				continue;
			char base_buf[512];
			char sjis_buf[1536];
			char gbk_buf[1536];
			char *ebase = archive_basename_buf(ename, base_buf, sizeof(base_buf));
			char *sjis_form = NULL;
			char *gbk_form = NULL;
			save_cjk_canon_forms_buf(ebase, sjis_buf, sizeof(sjis_buf), &sjis_form,
					gbk_buf, sizeof(gbk_buf), &gbk_form);
			if (ebase != base_buf)
				free(ebase);
			if (sjis_form)
				afa_canon_index_insert(manager, sjis_form, ar, &ar->files[j], prio);
			if (gbk_form && (!sjis_form || strcmp(sjis_form, gbk_form) != 0))
				afa_canon_index_insert(manager, gbk_form, ar, &ar->files[j], prio);
			if (sjis_form != sjis_buf)
				free(sjis_form);
			if (gbk_form != gbk_buf)
				free(gbk_form);
		}
	}
	manager->canon_index_valid = true;
}

static struct afa_entry *afa_find_best_by_basename(struct asset_manager_afa *manager,
		const char *name, struct afa_archive **ar_out)
{
	struct afa_entry *best_entry = afa_find_best_raw(manager, name, ar_out);
	if (best_entry || !name || !name_has_non_ascii(name))
		return best_entry;

	if (manager->miss_table && ht_get(manager->miss_table, name, NULL))
		return NULL;

	// Normalize GBK fullwidth digits/punctuation (e.g. %D/%F spliced into SJIS paths)
	// back to Shift-JIS before canonical/opposite-encoding fallback.
	char norm_buf[512];
	char *norm_alloc = NULL;
	const char *lookup_name = name;
	if (sys4_text_is_gbk) {
		bool norm_changed = false;
		norm_alloc = cjk_gbk_zenkaku_to_sjis_buf(name, strlen(name), norm_buf, sizeof(norm_buf), &norm_changed);
		if (norm_changed) {
			best_entry = afa_find_best_raw(manager, norm_alloc, ar_out);
			if (best_entry) {
				if (norm_alloc != norm_buf)
					free(norm_alloc);
				return best_entry;
			}
			lookup_name = norm_alloc;
		} else {
			if (norm_alloc != norm_buf)
				free(norm_alloc);
			norm_alloc = NULL;
		}
	}

	// Lazily indexed O(1) canonical Shinjitai/Simplified fallback across non-ASCII entries.
	afa_ensure_canon_index(manager);

	char base_buf[512];
	char *target_base = archive_basename_buf(lookup_name, base_buf, sizeof(base_buf));
	char q_sjis_buf[1536];
	char q_gbk_buf[1536];
	char *q_sjis = NULL;
	char *q_gbk = NULL;
	save_cjk_canon_forms_buf(target_base, q_sjis_buf, sizeof(q_sjis_buf), &q_sjis,
			q_gbk_buf, sizeof(q_gbk_buf), &q_gbk);
	if (target_base != base_buf)
		free(target_base);

	struct afa_archive *best_ar = NULL;
	int best_prio = -1;
	best_entry = NULL;
	if (manager->nr_canon_entries > 0) {
		const char *queries[2] = { q_sjis, q_gbk };
		for (int k = 0; k < 2; k++) {
			if (!queries[k])
				continue;
			if (k == 1 && q_sjis && !strcmp(q_sjis, q_gbk))
				continue;
			void *val = ht_get(manager->canon_basename_table, queries[k], NULL);
			if (!val)
				continue;
			size_t idx = (size_t)(uintptr_t)val - 1;
			struct afa_canon_entry *ce = &manager->canon_entries[idx];
			if (afa_candidate_beats(best_prio, best_entry, ce->prio, ce->entry)) {
				best_ar = ce->ar;
				best_entry = ce->entry;
				best_prio = ce->prio;
			}
		}
	}
	if (best_entry) {
		*ar_out = best_ar;
		goto out_free;
	}

	// Strict opposite-encoding fallback for halfwidth kana / middot / wave dash pairs
	// not normalized into canon_basename_table; skip directions that failed strict decode.
	size_t lookup_len = strlen(lookup_name);
	bool can_decode[2] = { q_sjis != NULL, q_gbk != NULL };
	for (int dir = 0; dir < 2; dir++) {
		bool src_gbk = (dir == 0) ? !sys4_text_is_gbk : sys4_text_is_gbk;
		if (!can_decode[src_gbk ? 1 : 0])
			continue;
		char *alt = save_transcode_cstr_strict(lookup_name, lookup_len, src_gbk, !src_gbk, NULL);
		if (alt) {
			best_entry = afa_find_best_raw(manager, alt, ar_out);
			free(alt);
			if (best_entry)
				goto out_free;
		}
	}

out_free:
	if (norm_alloc && norm_alloc != norm_buf)
		free(norm_alloc);
	if (q_sjis != q_sjis_buf)
		free(q_sjis);
	if (q_gbk != q_gbk_buf)
		free(q_gbk);
	if (!best_entry) {
		if (!manager->miss_table)
			manager->miss_table = ht_create(1024);
		if (manager->nr_miss_entries < 65536) {
			struct ht_slot *ms = ht_put(manager->miss_table, name, NULL);
			if (!ms->value) {
				ms->value = (void *)1;
				manager->nr_miss_entries++;
			}
		}
	}
	return best_entry;
}

static struct archive_data *afa_get_by_name(struct asset_manager *_manager, const char *name,
		int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	struct afa_archive *ar = NULL;
	struct afa_entry *entry = afa_find_best_by_basename(manager, name, &ar);
	if (!entry)
		return NULL;
	struct archive_data *data = afa_load_entry(ar, entry);
	if (data && id_out)
		*id_out = afa_encode_id(manager, ar, data->no);
	return data;
}

static bool afa_exists_by_name(struct asset_manager *_manager, const char *name, int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	struct afa_archive *ar = NULL;
	struct afa_entry *entry = afa_find_best_by_basename(manager, name, &ar);
	if (!entry)
		return false;
	if (id_out)
		*id_out = afa_encode_id(manager, ar, entry->no);
	return true;
}

struct asset_foreach_snapshot {
	struct archive_data **items;
	size_t count;
	size_t cap;
};

static void asset_foreach_collect_cb(struct archive_data *data, void *user)
{
	struct asset_foreach_snapshot *snap = user;
	if (snap->count == snap->cap) {
		size_t new_cap = snap->cap ? snap->cap * 2 : 256;
		snap->items = xrealloc(snap->items, new_cap * sizeof(*snap->items));
		snap->cap = new_cap;
	}
	snap->items[snap->count++] = archive_copy_descriptor(data);
}

void asset_foreach_file(enum asset_type type,
		void (*callback)(struct archive_data *data, void *user), void *user)
{
	if (type < 0 || type >= ASSET_TYPE_MAX)
		return;
	if (type != ASSET_CG) {
		if (!assets[type])
			return;
		if (assets[type]->get_by_name == afa_get_by_name) {
			struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[type];
			for (size_t i = 0; i < manager->nr_archives; i++)
				archive_for_each(&manager->archives[i]->ar, callback, user);
		} else {
			struct asset_manager_ald *manager = (struct asset_manager_ald *)assets[type];
			archive_for_each(manager->archive, callback, user);
		}
		return;
	}

	// Snapshot descriptors under ASSET_CG lock so concurrent CG workers cannot race
	// with archive traversal and callbacks may safely re-enter asset_manager.
	struct asset_foreach_snapshot snap = {0};
	asset_cg_archive_lock(ASSET_CG);
	if (assets[ASSET_CG]) {
		if (assets[ASSET_CG]->get_by_name == afa_get_by_name) {
			struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[ASSET_CG];
			for (size_t i = 0; i < manager->nr_archives; i++)
				archive_for_each(&manager->archives[i]->ar, asset_foreach_collect_cb, &snap);
		} else {
			struct asset_manager_ald *manager = (struct asset_manager_ald *)assets[ASSET_CG];
			archive_for_each(manager->archive, asset_foreach_collect_cb, &snap);
		}
	}
	asset_cg_archive_unlock(ASSET_CG);

	for (size_t i = 0; i < snap.count; i++) {
		callback(snap.items[i], user);
		archive_free_data(snap.items[i]);
	}
	free(snap.items);
}

static struct asset_manager_afa *afa_create_manager(void)
{
	struct asset_manager_afa *manager = xcalloc(1, sizeof(*manager));
	manager->manager.load_archive = afa_load_archive;
	manager->manager.exists_by_id = afa_exists_by_id;
	manager->manager.get_by_id = afa_get_by_id;
	manager->manager.exists_by_name = afa_exists_by_name;
	manager->manager.get_by_name = afa_get_by_name;
	return manager;
}

static void asset_manager_free_slot(enum asset_type type)
{
	if (!assets[type])
		return;
	if (assets[type]->get_by_name == afa_get_by_name) {
		struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[type];
		afa_invalidate_canon_index(manager);
		for (size_t i = 0; i < manager->nr_archives; i++)
			archive_free(&manager->archives[i]->ar);
		free(manager->archives);
		free(manager->archive_priorities);
		free(manager->id_archives);
	} else {
		struct asset_manager_ald *manager = (struct asset_manager_ald *)assets[type];
		for (size_t i = 0; i < manager->nr_cg_overlays; i++)
			archive_free(&manager->cg_overlays[i]->ar);
		free(manager->cg_overlays);
		if (manager->loose_cg_override)
			archive_free(manager->loose_cg_override);
		archive_free(manager->archive);
	}
	free(assets[type]);
	assets[type] = NULL;
}

static void _ald_init(enum asset_type type, struct archive *ar)
{
	if (assets[type])
		asset_manager_free_slot(type);
	struct asset_manager_ald *manager = xcalloc(1, sizeof(struct asset_manager_ald));
	manager->manager.exists_by_id = ald_exists_by_id;
	manager->manager.get_by_id = ald_get_by_id;
	manager->manager.load_archive = NULL;
	manager->manager.exists_by_name = NULL;
	manager->manager.get_by_name = NULL;
	manager->archive = ar;
	assets[type] = &manager->manager;
}

#if defined(ALICEHUB_IOS)
void asset_manager_fini(void)
{
	asset_cg_archive_lock(ASSET_CG);
	for (int type = 0; type < ASSET_TYPE_MAX; type++) {
		asset_manager_free_slot(type);
	}
	for (size_t i = 0; i < nr_save_cg_names; i++)
		free(save_cg_names[i]);
	free(save_cg_names);
	save_cg_names = NULL;
	nr_save_cg_names = cap_save_cg_names = 0;
	asset_cg_bump_archive_gen();
	asset_cg_archive_unlock(ASSET_CG);
}
#endif

static void ald_init(enum asset_type type, char **files, int count)
{
	if (count < 1)
		return;
	if (assets[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));

	int error;
	struct archive *ar = ald_open(files, count, MMAP_IF_64BIT, &error);
	if (!ar)
		ERROR("Failed to open ALD file: %s", archive_strerror(error));

	_ald_init(type, ar);

	for (int i = 0; i < count; i++) {
		free(files[i]);
	}
}

static void afa_init(enum asset_type type, char *file)
{
	if (!file)
		return;
	if (assets[type]) {
		WARNING("Multiple asset archives for type %s", asset_strtype(type));
		asset_manager_free_slot(type);
	}

	int error;
	struct afa_archive *ar = afa_open(file, MMAP_IF_64BIT, &error);
	if (!ar)
		ERROR("Failed to open AFA file: %s", archive_strerror(error));

	struct asset_manager_afa *manager = afa_create_manager();
	manager->archives = xmalloc(sizeof(*manager->archives));
	manager->archive_priorities = xmalloc(sizeof(*manager->archive_priorities));
	manager->archives[0] = ar;
	manager->archive_priorities[0] = AFA_PRIO_BASE;
	manager->nr_archives = 1;
	afa_register_id_archive(manager, ar);
	assets[type] = &manager->manager;

	char bak_path[PATH_MAX];
	snprintf(bak_path, sizeof(bak_path), "%s.vanilla.bak", file);
	if (file_exists(bak_path)) {
		struct afa_archive *bak_ar = afa_open(bak_path, MMAP_IF_64BIT, &error);
		if (bak_ar) {
			manager->archives = xrealloc(manager->archives, 2 * sizeof(*manager->archives));
			manager->archive_priorities = xrealloc(manager->archive_priorities, 2 * sizeof(*manager->archive_priorities));
			manager->archives[1] = bak_ar;
			manager->archive_priorities[1] = AFA_PRIO_BACKUP;
			manager->nr_archives = 2;
			afa_register_id_archive(manager, bak_ar);
		}
	}

	free(file);
}

static char *get_base_name(const char *ain_filename)
{
	char *path = (config.ain_basename_utf8 && *config.ain_basename_utf8)
		? xstrdup(config.ain_basename_utf8)
		: sjis_filename2utf(ain_filename, 0);
	char *dot = strrchr(path, '.');
	if (dot)
		*dot = '\0';
	return path;
}

static size_t archive_base_length(const char *name)
{
	size_t len = strlen(name);
	if (len > 6 && !strcasecmp(name + len - 4, ".ald") &&
			strchr("bBgGwWdD", name[len - 6]) &&
			toupper((unsigned char)name[len - 5]) >= 'A' &&
			toupper((unsigned char)name[len - 5]) <= 'Z')
		return len - 6;
	const char *suffixes[] = {
		"BA.afa", "WA.afa", "Sound.afa", "Voice.afa",
		"CG.afa", "Flat.afa", "Pact.afa", "AFF.afa"
	};
	for (size_t i = 0; i < sizeof(suffixes) / sizeof(*suffixes); i++) {
		size_t suffix_len = strlen(suffixes[i]);
		if (len > suffix_len && !strcasecmp(name + len - suffix_len, suffixes[i]))
			return len - suffix_len;
	}
	return 0;
}

static char *get_asset_base_name(void)
{
	char *base = get_base_name(config.ain_filename);
	UDIR *dir = opendir_utf8(config.game_dir);
	if (!dir)
		return base;

	// Translations can rename the AIN while retaining the original archives.
	// Keep an exact script match; otherwise accept only one archive family.
	char *candidate = NULL;
	bool matched = false, ambiguous = false;
	char *name;
	while ((name = readdir_utf8(dir))) {
		size_t len = archive_base_length(name);
		if (len) {
			char *path = path_join(config.game_dir, name);
			ustat st;
			bool regular = !stat_utf8(path, &st) && S_ISREG(st.st_mode);
			free(path);
			if (regular) {
				matched |= strlen(base) == len && !strncasecmp(base, name, len);
				if (!candidate)
					candidate = strndup(name, len);
				else if (strlen(candidate) != len || strncasecmp(candidate, name, len))
					ambiguous = true;
			}
		}
		free(name);
	}
	closedir_utf8(dir);
	if (!matched && candidate && !ambiguous) {
		WARNING("No archives match script %s; using resource family %s",
				display_sjis0(config.ain_filename), display_utf0(candidate));
		free(base);
		return candidate;
	}
	if (!matched && ambiguous)
		WARNING("No archives match script %s and multiple resource families exist",
				display_sjis0(config.ain_filename));
	free(candidate);
	return base;
}

static bool register_ald_file(char *ald_filenames[ASSET_TYPE_MAX][ALD_FILEMAX],
		int ald_count[ASSET_TYPE_MAX], const char *d_name, size_t prefix_len)
{
	int dno = toupper((unsigned char)d_name[prefix_len + 1]) - 'A';
	if (dno < 0 || dno >= ALD_FILEMAX) {
		WARNING("Invalid ALD index: %s", display_utf0(d_name));
		return false;
	}

	switch (d_name[prefix_len]) {
	case 'b':
	case 'B':
		ald_filenames[ASSET_BGM][dno] = path_join(config.game_dir, d_name);
		ald_count[ASSET_BGM] = max(ald_count[ASSET_BGM], dno + 1);
		return true;
	case 'g':
	case 'G':
		ald_filenames[ASSET_CG][dno] = path_join(config.game_dir, d_name);
		ald_count[ASSET_CG] = max(ald_count[ASSET_CG], dno + 1);
		return true;
	case 'w':
	case 'W':
		ald_filenames[ASSET_SOUND][dno] = path_join(config.game_dir, d_name);
		ald_count[ASSET_SOUND] = max(ald_count[ASSET_SOUND], dno + 1);
		return true;
	case 'd':
	case 'D':
		ald_filenames[ASSET_DATA][dno] = path_join(config.game_dir, d_name);
		ald_count[ASSET_DATA] = max(ald_count[ASSET_DATA], dno + 1);
		return true;
	default:
		WARNING("Unhandled ALD file: %s", display_utf0(d_name));
		return false;
	}
}

static size_t fallback_ald_prefix_length(const char *name, const char *kinds)
{
	size_t len = strlen(name);
	if (len <= 6)
		return 0;
	size_t plen = len - 6;
	int disk = toupper((unsigned char)name[plen + 1]);
	if (!strchr(kinds, toupper((unsigned char)name[plen])) || disk < 'A' || disk > 'Z')
		return 0;
	return plen;
}

static void register_fallback_alds(char *ald_filenames[ASSET_TYPE_MAX][ALD_FILEMAX],
		int ald_count[ASSET_TYPE_MAX], char **names, size_t count, const char *kinds,
		bool require_unique)
{
	const char *prefix = NULL;
	size_t prefix_len = 0;
	for (size_t i = 0; i < count; i++) {
		size_t len = fallback_ald_prefix_length(names[i], kinds);
		if (!len)
			continue;
		if (!prefix) {
			prefix = names[i];
			prefix_len = len;
			if (!require_unique)
				break;
		} else if (len != prefix_len || strncasecmp(prefix, names[i], len)) {
			return;
		}
	}
	if (!prefix)
		return;

	if (require_unique && !strchr(kinds, 'G')) {
		// Audio-only translation packs can accompany matching graphics. A second
		// complete game family in the same directory must stay independent.
		for (size_t i = 0; i < count; ++i) {
			if (fallback_ald_prefix_length(names[i], "G") == prefix_len &&
					!strncasecmp(names[i], prefix, prefix_len))
				return;
		}
	}
	for (size_t i = 0; i < count; i++) {
		if (fallback_ald_prefix_length(names[i], kinds) == prefix_len
				&& !strncasecmp(names[i], prefix, prefix_len))
			register_ald_file(ald_filenames, ald_count, names[i], prefix_len);
	}
}

static char *find_ald_audio_metadata(char **volumes, int count, const char *extension)
{
	for (int i = 0; i < count; i++) {
		if (!volumes[i])
			continue;
		char *path = xstrdup(volumes[i]);
		// Metadata is usually named after its volume (BA.bgi or WA.wai).
		char *dot = strrchr(path, '.');
		strcpy(dot + 1, extension);
		char *found = path_get_icase(path);
		if (!found) {
			// Also accept metadata named after the archive family.
			memmove(dot - 2, dot, strlen(dot) + 1);
			found = path_get_icase(path);
		}
		free(path);
		if (found)
			return found;
	}
	return NULL;
}

static bool is_valid_fnl_file(const char *path)
{
	if (!path || is_directory(path))
		return false;
	FILE *f = file_open_utf8(path, "rb");
	if (!f)
		return false;
	uint8_t magic[4];
	bool ok = fread(magic, sizeof(magic), 1, f) == 1 && !memcmp(magic, "FNA\0", 4);
	fclose(f);
	return ok;
}

static struct cg *asset_cg_load_base_by_name(struct archive *src_ar, const char *name)
{
	if (!name || !name[0])
		return NULL;
	struct archive_data *data = NULL;
	bool try_afa_by_name = false;
	bool src_in_afa = false;

	asset_cg_archive_lock(ASSET_CG);
	if (assets[ASSET_CG]) {
		if (assets[ASSET_CG]->get_by_name != NULL) {
			try_afa_by_name = true;
			if (src_ar && assets[ASSET_CG]->get_by_name == afa_get_by_name) {
				struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[ASSET_CG];
				for (size_t i = 0; i < manager->nr_archives; i++) {
					if (&manager->archives[i]->ar == src_ar) {
						src_in_afa = true;
						break;
					}
				}
			}
		} else if (assets[ASSET_CG]->get_by_id == ald_get_by_id) {
			struct asset_manager_ald *manager = (struct asset_manager_ald *)assets[ASSET_CG];
			char base_buf[512];
			char *basename = archive_basename_buf(name, base_buf, sizeof(base_buf));
			for (size_t i = 0; i < manager->nr_cg_overlays && !data; i++)
				data = archive_get_by_basename(&manager->cg_overlays[i]->ar, basename);
			if (!data && src_ar)
				data = archive_get_by_basename(src_ar, basename);
			if (!data && manager->archive && manager->archive != src_ar)
				data = archive_get_by_basename(manager->archive, basename);
			if (basename != base_buf)
				free(basename);
		}
	} else if (src_ar) {
		char base_buf[512];
		char *basename = archive_basename_buf(name, base_buf, sizeof(base_buf));
		data = archive_get_by_basename(src_ar, basename);
		if (basename != base_buf)
			free(basename);
	}
	asset_cg_archive_unlock(ASSET_CG);

	if (try_afa_by_name) {
		struct cg *cg = asset_cg_load_by_name(name, NULL);
		if (cg || !src_ar || src_in_afa)
			return cg;
		asset_cg_archive_lock(ASSET_CG);
		char base_buf[512];
		char *basename = archive_basename_buf(name, base_buf, sizeof(base_buf));
		data = archive_get_by_basename(src_ar, basename);
		if (basename != base_buf)
			free(basename);
		asset_cg_archive_unlock(ASSET_CG);
	}
	if (data) {
		struct cg *cg = cg_load_data(data);
		archive_free_data(data);
		return cg;
	}
	return NULL;
}

static struct cg *asset_cg_load_base_by_id(struct archive *src_ar, int id)
{
	if (!src_ar || id <= 0)
		return NULL;
	struct archive_data *data = NULL;
	char *base_name = NULL;
	bool handled = false;
	asset_cg_archive_lock(ASSET_CG);
	if (assets[ASSET_CG]) {
		if (assets[ASSET_CG]->get_by_id == ald_get_by_id) {
			struct asset_manager_ald *manager = (struct asset_manager_ald *)assets[ASSET_CG];
			for (size_t i = 0; i < manager->nr_cg_overlays && !data; i++) {
				if (!manager->cg_overlays[i]->has_number)
					continue;
				data = archive_get(&manager->cg_overlays[i]->ar, id - 1);
			}
			if (!data && manager->loose_cg_override) {
				if (src_ar != manager->loose_cg_override) {
					data = archive_get(manager->loose_cg_override, id - 1);
				} else if (!archive_exists(manager->loose_cg_override, id - 1) && manager->archive) {
					data = archive_get(manager->archive, id - 1);
				}
			}
		} else if (assets[ASSET_CG]->get_by_id == afa_get_by_id) {
			struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[ASSET_CG];
			struct afa_archive *src_afa = NULL;
			for (size_t i = 0; i < manager->nr_archives; i++) {
				if (&manager->archives[i]->ar == src_ar) {
					src_afa = manager->archives[i];
					break;
				}
			}
			if (src_afa) {
				if (!src_afa->has_number) {
					uint32_t idx = (uint32_t)(id - 1);
					if (idx < src_afa->nr_files && src_afa->files[idx].name) {
						base_name = xstrdup(src_afa->files[idx].name->text);
						handled = true;
					}
				} else {
					for (size_t i = 0; i < manager->nr_archives && !data; i++) {
						if (!manager->archives[i]->has_number)
							continue;
						data = archive_get(&manager->archives[i]->ar, id - 1);
					}
					handled = true;
				}
			}
		}
	}
	if (!data && !handled)
		data = archive_get(src_ar, id - 1);
	asset_cg_archive_unlock(ASSET_CG);

	if (base_name) {
		struct cg *cg = asset_cg_load_base_by_name(src_ar, base_name);
		free(base_name);
		return cg;
	}
	if (data) {
		struct cg *cg = cg_load_data(data);
		archive_free_data(data);
		return cg;
	}
	return NULL;
}

static int compare_uncensored_paths(const void *a, const void *b)
{
	const char *pa = *(const char * const *)a;
	const char *pb = *(const char * const *)b;
	const char *ba = strrchr(pa, '/');
	const char *bb = strrchr(pb, '/');
	ba = ba ? ba + 1 : pa;
	bb = bb ? bb + 1 : pb;
	bool ca = !strcasecmp(ba, "AliceHubUncensoredCG.afa");
	bool cb = !strcasecmp(bb, "AliceHubUncensoredCG.afa");
	if (ca != cb)
		return ca ? -1 : 1;
	return strcasecmp(ba, bb);
}

void asset_manager_init(void)
{
	char *ald_filenames[ASSET_TYPE_MAX][ALD_FILEMAX] = {0};
	int ald_count[ASSET_TYPE_MAX] = {0};
	char *afa_filenames[ASSET_TYPE_MAX] = {0};
	char *afa_chs_cg = NULL;
	char *afa_chs_flat = NULL;
	char **afa_uncensored_cgs = NULL;
	size_t nr_afa_uncensored_cgs = 0;

	UDIR *dir;
	char *d_name;

	if (!(dir = opendir_utf8(config.game_dir))) {
		ERROR("Failed to open directory: %s", display_utf0(config.game_dir));
	}

	char *base = get_asset_base_name();
	size_t base_len = strlen(base);
	char *base_repaired = cjk_repair_sjis_as_gbk_utf8(base);
	size_t base_repaired_len = base_repaired ? strlen(base_repaired) : 0;

	bool ex_is_canonical = false;
	bool has_base_ald = false;
	char **fallback_alds = NULL;
	size_t nr_fallback_alds = 0;
	char *fallback_fnl_name = NULL;

	// get ALD filenames
	while ((d_name = readdir_utf8(dir))) {
		char *repaired = cjk_repair_sjis_as_gbk_utf8(d_name);
		const char *match_name = repaired ? repaired : d_name;
		const char *ext = file_extension(match_name);
		if (!ext)
			goto loop_next;
		if (afa_is_uncensored_archive(d_name)) {
			afa_uncensored_cgs = xrealloc(afa_uncensored_cgs,
				(nr_afa_uncensored_cgs + 1) * sizeof(*afa_uncensored_cgs));
			afa_uncensored_cgs[nr_afa_uncensored_cgs++] = path_join(config.game_dir, d_name);
			goto loop_next;
		}
		// archive filename must normally begin with ain filename; retain *.ald candidates for renamed CN AINs
		// and standalone CHS.afa overlays used by Longinus CN translations (e.g. Oyakorankan).
		const char *matched_name = NULL;
		size_t matched_prefix_len = 0;
		if (base_repaired && !strncasecmp(base_repaired, match_name, base_repaired_len)) {
			matched_name = match_name;
			matched_prefix_len = base_repaired_len;
		} else if (!strncasecmp(base, match_name, base_len)) {
			matched_name = match_name;
			matched_prefix_len = base_len;
		} else if (!strncasecmp(base, d_name, base_len)) {
			matched_name = d_name;
			matched_prefix_len = base_len;
		}
		if (!matched_name) {
			if (!strcasecmp(ext, "ald")) {
				fallback_alds = xrealloc(fallback_alds, (nr_fallback_alds + 1) * sizeof(*fallback_alds));
				fallback_alds[nr_fallback_alds++] = xstrdup(d_name);
			} else if (!strcasecmp(match_name, "CHS.afa") && !afa_chs_cg) {
				afa_chs_cg = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(ext, "fnl") && d_name[0] != '.') {
				if (!fallback_fnl_name || strcmp(d_name, fallback_fnl_name) < 0) {
					char *cand_path = path_join(config.game_dir, d_name);
					if (is_valid_fnl_file(cand_path)) {
						free(fallback_fnl_name);
						fallback_fnl_name = xstrdup(d_name);
					}
					free(cand_path);
				}
			}
			goto loop_next;
		}
		if (!strcasecmp(ext, "ald")) {
			size_t mlen = strlen(matched_name);
			size_t dlen = strlen(d_name);
			if (mlen == matched_prefix_len + 6 && dlen >= mlen - matched_prefix_len) {
				size_t d_prefix_len = dlen - (mlen - matched_prefix_len);
				if (register_ald_file(ald_filenames, ald_count, d_name, d_prefix_len))
					has_base_ald = true;
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
			const char *suffix = matched_name + matched_prefix_len;
			bool canonical = !strcasecmp(suffix, "EX.ex") || !strcasecmp(suffix, ".ex");
			if (config.ex_path) {
				WARNING("Multiple ex files");
				if (!canonical || ex_is_canonical)
					goto loop_next;
				free(config.ex_path);
			}
			config.ex_path = path_join(config.game_dir, d_name);
			ex_is_canonical = canonical;
		} else if (!strcasecmp(ext, "afa")) {
			const char *type = matched_name + matched_prefix_len;
			if (!strcasecmp(type, "BA.afa")) {
				afa_filenames[ASSET_BGM] = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "WA.afa") || !strcasecmp(type, "Sound.afa")) {
				afa_filenames[ASSET_SOUND] = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "Voice.afa")) {
				afa_filenames[ASSET_VOICE] = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "CG.afa")) {
				afa_filenames[ASSET_CG] = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "Flat.afa")) {
				afa_filenames[ASSET_FLAT] = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "Pact.afa")) {
				afa_filenames[ASSET_PACT] = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "AFF.afa")) {
				afa_filenames[ASSET_FLASH] = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "CHS.afa")) {
				free(afa_chs_cg);
				afa_chs_cg = path_join(config.game_dir, d_name);
			} else if (!strcasecmp(type, "CHS2.afa")) {
				free(afa_chs_flat);
				afa_chs_flat = path_join(config.game_dir, d_name);
			}
		} else if (!strcasecmp(ext, "fnl")) {
			if (!config.fnl_path)
				config.fnl_path = path_join(config.game_dir, d_name);
		}
	loop_next:
		free(repaired);
		free(d_name);
	}
	closedir_utf8(dir);
	free(base_repaired);
	free(base);

	if (!config.fnl_path && fallback_fnl_name) {
		config.fnl_path = path_join(config.game_dir, fallback_fnl_name);
	}
	free(fallback_fnl_name);

	if (nr_fallback_alds > 1)
		qsort(fallback_alds, nr_fallback_alds, sizeof(*fallback_alds), compare_cstr_ptr);

	if (!has_base_ald) {
		register_fallback_alds(ald_filenames, ald_count, fallback_alds, nr_fallback_alds, "BGWD", true);
	} else {
		// A translated script can have matching graphics but keep the original audio.
		if (!ald_count[ASSET_BGM] && !afa_filenames[ASSET_BGM])
			register_fallback_alds(ald_filenames, ald_count, fallback_alds, nr_fallback_alds, "B", true);
		if (!ald_count[ASSET_SOUND] && !afa_filenames[ASSET_SOUND])
			register_fallback_alds(ald_filenames, ald_count, fallback_alds, nr_fallback_alds, "W", true);
	}
	for (size_t i = 0; i < nr_fallback_alds; i++)
		free(fallback_alds[i]);
	free(fallback_alds);

	if (!config.bgi_path && !afa_filenames[ASSET_BGM])
		config.bgi_path = find_ald_audio_metadata(ald_filenames[ASSET_BGM], ald_count[ASSET_BGM], "bgi");
	if (!config.wai_path && !afa_filenames[ASSET_SOUND])
		config.wai_path = find_ald_audio_metadata(ald_filenames[ASSET_SOUND], ald_count[ASSET_SOUND], "wai");

	// open ALD archives
	ald_init(ASSET_BGM, ald_filenames[ASSET_BGM], ald_count[ASSET_BGM]);
	ald_init(ASSET_SOUND, ald_filenames[ASSET_SOUND], ald_count[ASSET_SOUND]);
	ald_init(ASSET_VOICE, ald_filenames[ASSET_VOICE], ald_count[ASSET_VOICE]);
	ald_init(ASSET_CG, ald_filenames[ASSET_CG], ald_count[ASSET_CG]);
	ald_init(ASSET_FLAT, ald_filenames[ASSET_FLAT], ald_count[ASSET_FLAT]);
	ald_init(ASSET_PACT, ald_filenames[ASSET_PACT], ald_count[ASSET_PACT]);
	ald_init(ASSET_DATA, ald_filenames[ASSET_DATA], ald_count[ASSET_DATA]);
	ald_init(ASSET_FLASH, ald_filenames[ASSET_FLASH], ald_count[ASSET_FLASH]);

	// open AFA archives
	afa_init(ASSET_BGM, afa_filenames[ASSET_BGM]);
	afa_init(ASSET_SOUND, afa_filenames[ASSET_SOUND]);
	afa_init(ASSET_VOICE, afa_filenames[ASSET_VOICE]);
	afa_init(ASSET_CG, afa_filenames[ASSET_CG]);
	afa_init(ASSET_FLAT, afa_filenames[ASSET_FLAT]);
	afa_init(ASSET_PACT, afa_filenames[ASSET_PACT]);
	afa_init(ASSET_DATA, afa_filenames[ASSET_DATA]);
	afa_init(ASSET_FLASH, afa_filenames[ASSET_FLASH]);

	// Mount companion CN CG/Flat patch archives (e.g. Evenicle2RanceCHS.afa / CHS2.afa)
	// with patch overlay priority (above base archives, below user mods) so translated
	// UI skins and flats take precedence even when base entries have newer timestamps.
	if (afa_chs_cg) {
		if (!assets[ASSET_CG])
			assets[ASSET_CG] = &afa_create_manager()->manager;
		if (assets[ASSET_CG]->load_archive == afa_load_archive)
			afa_load_archive_prio((struct asset_manager_afa *)assets[ASSET_CG], afa_chs_cg, AFA_PRIO_PATCH);
		free(afa_chs_cg);
	}
	if (afa_chs_flat) {
		if (!assets[ASSET_FLAT])
			assets[ASSET_FLAT] = &afa_create_manager()->manager;
		if (assets[ASSET_FLAT]->load_archive == afa_load_archive)
			afa_load_archive_prio((struct asset_manager_afa *)assets[ASSET_FLAT], afa_chs_flat, AFA_PRIO_PATCH);
		free(afa_chs_flat);
	}

	// Mount standalone uncensored CG overlay archives (e.g. AliceHubUncensoredCG.afa,
	// Aiuncensored.afa) above base/patch/mod archives so uncensored H-CGs work across
	// both AFA and ALD games without touching game scripts or conflicting with MODs.
	if (nr_afa_uncensored_cgs > 0) {
		qsort(afa_uncensored_cgs, nr_afa_uncensored_cgs, sizeof(*afa_uncensored_cgs),
			compare_uncensored_paths);
		if (!assets[ASSET_CG])
			assets[ASSET_CG] = &afa_create_manager()->manager;
		if (assets[ASSET_CG]->load_archive == afa_load_archive) {
			// afa_load_archive_prio prepends equal-priority archives, so load in reverse
			// to keep index 0 (AliceHubUncensoredCG.afa) at highest precedence.
			for (size_t i = nr_afa_uncensored_cgs; i-- > 0; ) {
				afa_load_archive_prio((struct asset_manager_afa *)assets[ASSET_CG],
					afa_uncensored_cgs[i], AFA_PRIO_UNCENSORED);
			}
		} else if (assets[ASSET_CG]->get_by_id == ald_get_by_id) {
			for (size_t i = 0; i < nr_afa_uncensored_cgs; i++) {
				ald_attach_cg_overlay((struct asset_manager_ald *)assets[ASSET_CG],
					afa_uncensored_cgs[i]);
			}
		}
		for (size_t i = 0; i < nr_afa_uncensored_cgs; i++)
			free(afa_uncensored_cgs[i]);
		free(afa_uncensored_cgs);
	}

	if (assets[ASSET_CG] && assets[ASSET_CG]->get_by_id == ald_get_by_id
			&& (sys4_text_is_gbk || sys4_source_is_utf8)) {
		char *nspic_dir = gamedir_path_icase("NSPic");
		if (nspic_dir && is_directory(nspic_dir)) {
			((struct asset_manager_ald *)assets[ASSET_CG])->loose_cg_override =
				loose_cg_open_dir(nspic_dir);
		}
		free(nspic_dir);
	}

	asset_cg_bump_archive_gen();
	cg_set_base_loader_by_name(asset_cg_load_base_by_name);
	cg_set_base_loader_by_id(asset_cg_load_base_by_id);
}
