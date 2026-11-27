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

#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#ifndef __SWITCH__
#include <signal.h>
#include <unistd.h>
#endif
#endif

#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "debugger.h"

// In a Windows environment, console output should be SJIS-encoded
// (assuming the user is using Japanese non-unicode locale).
#if (defined(_WIN32) || defined(__WIN32))
#define NATIVE_SJIS
#endif

const char *display_sjis0(const char *sjis)
{
	if (!sjis)
		return "";
#ifdef NATIVE_SJIS
	return sjis;
#else
	static _Thread_local char *utf = NULL;
	free(utf);
	utf = mai_sjis2utf(sjis, 0);
	return utf;
#endif
}

const char *display_sjis1(const char *sjis)
{
	if (!sjis)
		return "";
#ifdef NATIVE_SJIS
	return sjis;
#else
	static _Thread_local char *utf = NULL;
	free(utf);
	utf = mai_sjis2utf(sjis, 0);
	return utf;
#endif
}

const char *display_sjis2(const char *sjis)
{
	if (!sjis)
		return "";
#ifdef NATIVE_SJIS
	return sjis;
#else
	static _Thread_local char *utf = NULL;
	free(utf);
	utf = mai_sjis2utf(sjis, 0);
	return utf;
#endif
}

const char *display_utf0(const char *utf)
{
	if (!utf)
		return "";
#ifdef NATIVE_SJIS
	static _Thread_local char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

const char *display_utf1(const char *utf)
{
	if (!utf)
		return "";
#ifdef NATIVE_SJIS
	static _Thread_local char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

const char *display_utf2(const char *utf)
{
	if (!utf)
		return "";
#ifdef NATIVE_SJIS
	static _Thread_local char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

static char *unix_path_with(const char *path, bool legacy_gbk)
{
	if (!path)
		return xstrdup("");
	char *utf = legacy_gbk ? sjis2utf(path, strlen(path)) : sjis_filename2utf(path, strlen(path));
	for (int i = 0; utf[i]; i++) {
		if (utf[i] == '\\')
			utf[i] = '/';
	}
	return utf;
}

char *unix_path(const char *path)
{
	return unix_path_with(path, false);
}

bool is_absolute_path(const char *path)
{
	if (!path || !path[0])
		return false;
#if (defined(_WIN32) || defined(__WIN32__))
	int i = (isalpha((unsigned char)path[0]) && path[1] == ':') ? 2 : 0;
	return path[i] == '/' || path[i] == '\\';
#elif defined(__SWITCH__)
	const char *slash = strchr(path, '/');
	return slash && (slash == path || slash[-1] == ':');
#else
	return path[0] == '/';
#endif
}

static bool strip_cjk_dir_prefix(const char *dir, const char *path, const char **rel_out)
{
	if (!dir || !*dir || !path || !*path)
		return false;
	for (int mode = 0; mode < 2; mode++) {
		if (mode == 1 && !sys4_text_is_gbk)
			break;
		char *cjk_dir = (mode == 0) ? utf2sjis(dir, strlen(dir)) : utf2cjk(dir, strlen(dir), false);
		size_t pfx = strlen(cjk_dir);
		if (pfx > 0 && !strncmp(path, cjk_dir, pfx) &&
		    (path[pfx] == '/' || path[pfx] == '\\' || path[pfx] == '\0')) {
			const char *rel = path + pfx;
			while (*rel == '/' || *rel == '\\')
				rel++;
			free(cjk_dir);
			*rel_out = rel;
			return true;
		}
		free(cjk_dir);
	}
	return false;
}

static char *join_dir_and_rel(const char *dir, const char *utf_rel)
{
	if (is_absolute_path(utf_rel))
		return xstrdup(utf_rel);
	char *resolved = xmalloc(strlen(dir) + strlen(utf_rel) + 2);
	strcpy(resolved, dir);
	if (*utf_rel) {
		strcat(resolved, "/");
		strcat(resolved, utf_rel);
	}
	return resolved;
}

static bool path_component_has_non_ascii(const char *s)
{
	for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
		if (*p >= 0x80)
			return true;
	}
	return false;
}

static char *dir_find_mojibake_icase(const char *dir, const char *component)
{
	if (!path_component_has_non_ascii(component))
		return NULL;
	UDIR *d = opendir_utf8(dir);
	if (!d)
		return NULL;
	char *target_repaired = cjk_repair_sjis_as_gbk_utf8(component);
	const char *targets[2] = { component, target_repaired };
	char *found = NULL;
	char *d_name;
	while ((d_name = readdir_utf8(d))) {
		char *entry_repaired = cjk_repair_sjis_as_gbk_utf8(d_name);
		const char *candidates[2] = { d_name, entry_repaired };
		bool exact = false;
		bool icase = false;
		for (int ti = 0; ti < 2 && !exact; ti++) {
			if (!targets[ti])
				continue;
			for (int ci = 0; ci < 2; ci++) {
				if (!candidates[ci])
					continue;
				if (!strcmp(candidates[ci], targets[ti])) {
					exact = true;
					break;
				}
				if (!icase && !strcasecmp(candidates[ci], targets[ti]))
					icase = true;
			}
		}
		if (exact) {
			free(found);
			found = path_join(dir, d_name);
			free(entry_repaired);
			free(d_name);
			break;
		}
		if (!found && icase) {
			found = path_join(dir, d_name);
		}
		free(entry_repaired);
		free(d_name);
	}
	closedir_utf8(d);
	free(target_repaired);
	return found;
}

static char *relative_path_get_icase(const char *dir, char *relative)
{
	char *resolved = xstrdup(dir);
	char *component = relative;
	for (;;) {
		char *slash = strchr(component, '/');
		if (slash)
			*slash = '\0';
		if (*component) {
			char *next = path_join(resolved, component);
			if (!file_exists(next)) {
				char *matched = path_get_icase(next);
				if (!matched)
					matched = dir_find_mojibake_icase(resolved, component);
				free(next);
				next = matched;
			}
			free(resolved);
			resolved = next;
			if (!resolved) {
				if (slash)
					*slash = '/';
				return NULL;
			}
		}
		if (!slash) {
			if (*component)
				return resolved;
			char *with_separator = path_join(resolved, "");
			free(resolved);
			return with_separator;
		}
		*slash = '/';
		if (!is_directory(resolved)) {
			free(resolved);
			return NULL;
		}
		component = slash + 1;
	}
}

static char *find_path_icase(const char *resolved, const char *dir, char *relative, bool nested_icase)
{
	char *match = path_get_icase(resolved);
	if (!match && nested_icase && !is_absolute_path(relative))
		return relative_path_get_icase(dir, relative);
	return match;
}

static char *resolve_path(const char *dir, const char *path, bool nested_icase)
{
	const char *target = path;
	const char *stripped_rel = NULL;
	if (strip_cjk_dir_prefix(dir, path, &stripped_rel)) {
		target = stripped_rel;
	}

	char *utf_primary = unix_path_with(target, false);
	char *resolved_primary = join_dir_and_rel(dir, utf_primary);

	if (file_exists(resolved_primary)) {
		free(utf_primary);
		return resolved_primary;
	}

	char *icase_primary = find_path_icase(resolved_primary, dir, utf_primary, nested_icase);
	free(utf_primary);
	if (icase_primary) {
		free(resolved_primary);
		return icase_primary;
	}

	if (sys4_text_is_gbk) {
		char *utf_legacy = unix_path_with(target, true);
		char *resolved_legacy = join_dir_and_rel(dir, utf_legacy);
		if (strcmp(resolved_legacy, resolved_primary) != 0) {
			if (file_exists(resolved_legacy)) {
				free(utf_legacy);
				free(resolved_primary);
				return resolved_legacy;
			}
			char *icase_legacy = find_path_icase(resolved_legacy, dir, utf_legacy, nested_icase);
			if (icase_legacy) {
				free(utf_legacy);
				free(resolved_primary);
				free(resolved_legacy);
				return icase_legacy;
			}
		}
		free(utf_legacy);
		free(resolved_legacy);
	}

	return resolved_primary;
}

char *gamedir_path(const char *path)
{
	return resolve_path(config.game_dir, path, true);
}

char *gamedir_path_icase(const char *path)
{
	char *resolved = gamedir_path(path);
	if (!file_exists(resolved)) {
		char *tmp = path_get_icase(resolved);
		free(resolved);
		resolved = tmp;
	}
	return resolved;
}

#define SAVEDIR_CACHE_SLOTS 4
#define SAVEDIR_CACHE_TTL_MS 250u

struct savedir_cache_entry {
	char *name;
	uint32_t hash_icase;
};

struct savedir_cache_slot {
	char *dir_path;
	bool dir_missing;
	bool dir_unreadable;
	uint64_t mutation_gen;
	uint64_t last_validated_ms;
	uint64_t last_used_seq;
	ustat dir_stat;
	struct savedir_cache_entry *entries;
	size_t nr_entries;
	int32_t *buckets;
	size_t bucket_cap;
};

static struct savedir_cache_slot savedir_cache[SAVEDIR_CACHE_SLOTS];
static uint64_t savedir_cache_seq = 0;

#ifdef _WIN32
static SRWLOCK savedir_cache_lock = SRWLOCK_INIT;
static inline void savedir_lock(void) { AcquireSRWLockExclusive(&savedir_cache_lock); }
static inline void savedir_unlock(void) { ReleaseSRWLockExclusive(&savedir_cache_lock); }
#else
static pthread_mutex_t savedir_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void savedir_lock(void) { pthread_mutex_lock(&savedir_cache_lock); }
static inline void savedir_unlock(void) { pthread_mutex_unlock(&savedir_cache_lock); }
#endif

static uint64_t savedir_monotonic_ms(void)
{
#ifdef _WIN32
	return (uint64_t)GetTickCount64();
#else
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000L);
	return 0;
#endif
}

static bool savedir_stat_eq(const ustat *a, const ustat *b)
{
	if (a->st_dev != b->st_dev || a->st_ino != b->st_ino
			|| a->st_size != b->st_size
			|| a->st_mtime != b->st_mtime
			|| a->st_ctime != b->st_ctime)
		return false;
#if defined(__APPLE__)
	if (a->st_mtimespec.tv_nsec != b->st_mtimespec.tv_nsec
			|| a->st_ctimespec.tv_nsec != b->st_ctimespec.tv_nsec)
		return false;
#elif defined(__linux__) || defined(__ANDROID__)
	if (a->st_mtim.tv_nsec != b->st_mtim.tv_nsec
			|| a->st_ctim.tv_nsec != b->st_ctim.tv_nsec)
		return false;
#endif
	return true;
}

static uint32_t savedir_hash_ascii_icase(const char *s)
{
	uint32_t h = 2166136261u;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		unsigned char c = *p;
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c - 'A' + 'a');
		h ^= c;
		h *= 16777619u;
	}
	return h;
}

static void savedir_slot_reset(struct savedir_cache_slot *slot)
{
	free(slot->dir_path);
	for (size_t i = 0; i < slot->nr_entries; i++)
		free(slot->entries[i].name);
	free(slot->entries);
	free(slot->buckets);
	memset(slot, 0, sizeof(*slot));
}

static void savedir_slot_populate(struct savedir_cache_slot *slot, const char *parent_dir,
		uint64_t cur_gen, uint64_t now_ms)
{
	savedir_slot_reset(slot);
	slot->dir_path = xstrdup(parent_dir);
	slot->mutation_gen = cur_gen;
	slot->last_validated_ms = now_ms;

	ustat st;
	if (stat_utf8(parent_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		slot->dir_missing = true;
		return;
	}
	slot->dir_stat = st;

	UDIR *d = opendir_utf8(parent_dir);
	if (!d) {
		// Directory exists (stat_utf8 succeeded with S_ISDIR) but cannot be listed
		// (e.g. traverse-only permissions): fall back to direct file_exists() probes.
		slot->dir_unreadable = true;
		return;
	}

	size_t cap = 0;
	char *d_name;
	while ((d_name = readdir_utf8(d)) != NULL) {
		if (!strcmp(d_name, ".") || !strcmp(d_name, "..")) {
			free(d_name);
			continue;
		}
		if (slot->nr_entries == cap) {
			size_t ncap = cap ? cap * 2 : 64;
			slot->entries = xrealloc(slot->entries, ncap * sizeof(*slot->entries));
			cap = ncap;
		}
		slot->entries[slot->nr_entries].name = d_name;
		slot->entries[slot->nr_entries].hash_icase = savedir_hash_ascii_icase(d_name);
		slot->nr_entries++;
	}
	closedir_utf8(d);

	if (slot->nr_entries > 0) {
		size_t bcap = 16;
		while (bcap < slot->nr_entries * 2)
			bcap <<= 1;
		slot->buckets = xmalloc(bcap * sizeof(*slot->buckets));
		for (size_t i = 0; i < bcap; i++)
			slot->buckets[i] = -1;
		slot->bucket_cap = bcap;
		size_t mask = bcap - 1;
		for (size_t i = 0; i < slot->nr_entries; i++) {
			size_t idx = slot->entries[i].hash_icase & mask;
			while (slot->buckets[idx] != -1)
				idx = (idx + 1) & mask;
			slot->buckets[idx] = (int32_t)i;
		}
	}
}

static bool savedir_split_parent_base(const char *resolved, char **parent_out, const char **base_out)
{
	const char *slash = strrchr(resolved, '/');
	if (!slash || slash[1] == '\0')
		return false;
	const char *base = slash + 1;
	if (!strcmp(base, ".") || !strcmp(base, ".."))
		return false;

	size_t plen = (size_t)(slash - resolved);
	while (plen > 1 && resolved[plen - 1] == '/'
#ifdef __SWITCH__
			&& !(plen >= 2 && resolved[plen - 2] == ':')
#endif
			) {
		plen--;
	}

	char *parent;
	if (plen == 0) {
		parent = xstrdup("/");
	} else if (plen == 2 && isalpha((unsigned char)resolved[0]) && resolved[1] == ':') {
		parent = xmalloc(4);
		memcpy(parent, resolved, 3);
		parent[3] = '\0';
	}
#ifdef __SWITCH__
	else if (plen > 0 && resolved[plen - 1] == ':') {
		parent = xmalloc(plen + 2);
		memcpy(parent, resolved, plen + 1);
		parent[plen + 1] = '\0';
	}
#endif
	else {
		parent = xmalloc(plen + 1);
		memcpy(parent, resolved, plen);
		parent[plen] = '\0';
	}

	*parent_out = parent;
	*base_out = base;
	return true;
}

static bool savedir_cache_lookup(const char *resolved, char **matched_out, bool *dir_missing_out)
{
	*matched_out = NULL;
	if (dir_missing_out)
		*dir_missing_out = false;

	char *parent_dir = NULL;
	const char *base_name = NULL;
	if (!savedir_split_parent_base(resolved, &parent_dir, &base_name))
		return false;

	savedir_lock();
	uint64_t cur_gen = file_mutation_gen();
	uint64_t now_ms = savedir_monotonic_ms();

	struct savedir_cache_slot *slot = NULL;
	for (int i = 0; i < SAVEDIR_CACHE_SLOTS; i++) {
		if (savedir_cache[i].dir_path && !strcmp(savedir_cache[i].dir_path, parent_dir)) {
			slot = &savedir_cache[i];
			break;
		}
	}

	bool valid = false;
	if (slot && slot->mutation_gen == cur_gen) {
		if (now_ms > 0 && now_ms >= slot->last_validated_ms
				&& (now_ms - slot->last_validated_ms) < SAVEDIR_CACHE_TTL_MS) {
			valid = true;
		} else {
			ustat st;
			int rc = stat_utf8(parent_dir, &st);
			if (slot->dir_missing) {
				valid = (rc != 0 || !S_ISDIR(st.st_mode));
			} else if (slot->dir_unreadable) {
				valid = (rc == 0 && S_ISDIR(st.st_mode)
						&& slot->dir_stat.st_mode == st.st_mode
						&& savedir_stat_eq(&slot->dir_stat, &st));
			} else {
				valid = (rc == 0 && S_ISDIR(st.st_mode) && savedir_stat_eq(&slot->dir_stat, &st));
			}
			if (valid)
				slot->last_validated_ms = now_ms;
		}
	}

	if (!valid) {
		if (!slot) {
			slot = &savedir_cache[0];
			for (int i = 0; i < SAVEDIR_CACHE_SLOTS; i++) {
				if (!savedir_cache[i].dir_path) {
					slot = &savedir_cache[i];
					break;
				}
				if (savedir_cache[i].last_used_seq < slot->last_used_seq)
					slot = &savedir_cache[i];
			}
		}
		savedir_slot_populate(slot, parent_dir, cur_gen, now_ms);
	}

	slot->last_used_seq = ++savedir_cache_seq;
	if (slot->dir_unreadable) {
		savedir_unlock();
		free(parent_dir);
		return false;
	}
	if (dir_missing_out)
		*dir_missing_out = slot->dir_missing;

	if (!slot->dir_missing && slot->nr_entries > 0 && slot->buckets) {
		uint32_t h = savedir_hash_ascii_icase(base_name);
		size_t mask = slot->bucket_cap - 1;
		size_t idx = h & mask;
		const char *exact = NULL;
		const char *icase = NULL;
		while (slot->buckets[idx] != -1) {
			struct savedir_cache_entry *e = &slot->entries[slot->buckets[idx]];
			if (e->hash_icase == h) {
				if (!strcmp(e->name, base_name)) {
					exact = e->name;
					break;
				}
				if (!icase && !strcasecmp(e->name, base_name))
					icase = e->name;
			}
			idx = (idx + 1) & mask;
		}
		const char *found = exact ? exact : icase;
		if (found)
			*matched_out = path_join(parent_dir, found);
	}

	savedir_unlock();
	free(parent_dir);
	return true;
}

void savedir_cache_invalidate(void)
{
	file_note_fs_mutation();
}

static bool savedir_strip_memory_prefix(const char *path, const char **rest_out)
{
	if (!path)
		return false;
	const char *p = path;
	while (p[0] == '.' && (p[1] == '/' || p[1] == '\\'))
		p += 2;
	static const char tag[] = "<memory>";
	for (size_t i = 0; i < sizeof(tag) - 1; i++) {
		unsigned char c = (unsigned char)p[i];
		if (!c)
			return false;
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c - 'A' + 'a');
		if (c != (unsigned char)tag[i])
			return false;
	}
	*rest_out = p + (sizeof(tag) - 1);
	return true;
}

static uint64_t savedir_fnv1a64_ascii_icase(const char *s, size_t len)
{
	uint64_t h = 14695981039346656037ULL;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c - 'A' + 'a');
		h ^= c;
		h *= 1099511628211ULL;
	}
	return h;
}

static _Atomic uint64_t g_savedir_mem_session_nonce = 0;

static uint64_t savedir_current_pid(void)
{
#ifdef _WIN32
	return (uint64_t)GetCurrentProcessId();
#elif defined(__SWITCH__)
	return 1ULL;
#else
	return (uint64_t)getpid();
#endif
}

static uint64_t savedir_make_session_nonce(uint64_t prev)
{
	uint64_t h = 14695981039346656037ULL ^ savedir_current_pid();
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	uint64_t rt = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
	uint64_t mono = (uint64_t)GetTickCount64();
	h = (h ^ rt) * 1099511628211ULL;
	h = (h ^ mono) * 1099511628211ULL;
#else
	struct timespec rt = { 0 }, mono = { 0 };
	clock_gettime(CLOCK_REALTIME, &rt);
	clock_gettime(CLOCK_MONOTONIC, &mono);
	h = (h ^ (uint64_t)rt.tv_sec) * 1099511628211ULL;
	h = (h ^ (uint64_t)rt.tv_nsec) * 1099511628211ULL;
	h = (h ^ (uint64_t)mono.tv_sec) * 1099511628211ULL;
	h = (h ^ (uint64_t)mono.tv_nsec) * 1099511628211ULL;
#endif
	h ^= (uintptr_t)&g_savedir_mem_session_nonce;
	h *= 1099511628211ULL;
	h ^= prev + 1ULL;
	if (h == 0 || h == prev)
		h = prev + 1ULL;
	if (h == 0)
		h = 1ULL;
	return h;
}

static uint64_t savedir_current_mem_session_nonce(void)
{
	uint64_t nonce = atomic_load_explicit(&g_savedir_mem_session_nonce, memory_order_acquire);
	if (nonce != 0)
		return nonce;
	uint64_t candidate = savedir_make_session_nonce(0);
	uint64_t expected = 0;
	if (atomic_compare_exchange_strong_explicit(&g_savedir_mem_session_nonce, &expected, candidate,
			memory_order_acq_rel, memory_order_acquire))
		return candidate;
	return expected;
}

static size_t savedir_format_session_prefix(char *buf, size_t cap, uint64_t pid, uint64_t nonce)
{
	int n = snprintf(buf, cap, "%sp%" PRIu64 ".%016" PRIx64 ".", SAVEDIR_MEMORY_PREFIX, pid, nonce);
	if (n < 0 || (size_t)n >= cap)
		return 0;
	return (size_t)n;
}

static bool savedir_parse_memory_owner(const char *d_name, uint64_t *pid_out, uint64_t *nonce_out)
{
	const size_t pfx_len = sizeof(SAVEDIR_MEMORY_PREFIX) - 1;
	const char *p = d_name;
	if (p[0] == '.' && !strncmp(p + 1, SAVEDIR_MEMORY_PREFIX, pfx_len))
		p += 1 + pfx_len;
	else if (!strncmp(p, SAVEDIR_MEMORY_PREFIX, pfx_len))
		p += pfx_len;
	else
		return false;

	if (*p != 'p')
		return false;
	p++;
	if (*p < '0' || *p > '9')
		return false;
	uint64_t pid = 0;
	while (*p >= '0' && *p <= '9') {
		uint64_t digit = (uint64_t)(*p - '0');
		if (pid > (UINT64_MAX - digit) / 10)
			return false;
		pid = pid * 10 + digit;
		p++;
	}
	if (*p != '.')
		return false;
	p++;
	uint64_t nonce = 0;
	size_t hex_digits = 0;
	while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
		if (hex_digits >= 16)
			return false;
		uint64_t v = (*p >= '0' && *p <= '9') ? (uint64_t)(*p - '0')
			: (*p >= 'a' && *p <= 'f') ? (uint64_t)(*p - 'a' + 10)
			: (uint64_t)(*p - 'A' + 10);
		nonce = (nonce << 4) | v;
		hex_digits++;
		p++;
	}
	if (*p != '.' || hex_digits == 0 || nonce == 0)
		return false;
	*pid_out = pid;
	*nonce_out = nonce;
	return true;
}

static bool savedir_owner_is_dead(uint64_t pid, uint64_t nonce, uint64_t my_pid, uint64_t my_nonce)
{
#ifdef __SWITCH__
	// Switch runs a single game instance at a time and always uses pid=1;
	// at startup (before the new session writes any snapshots), any leftover
	// .alicehub-mem.* file on the SD card is guaranteed stale.
	(void)pid;
	(void)nonce;
	(void)my_pid;
	(void)my_nonce;
	return true;
#else
	if (pid == my_pid)
		return nonce != my_nonce;
#if defined(_WIN32)
	if (pid == 0 || pid > UINT32_MAX)
		return false;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
	if (h) {
		DWORD exit_code = 0;
		bool dead = GetExitCodeProcess(h, &exit_code) && exit_code != STILL_ACTIVE;
		CloseHandle(h);
		return dead;
	}
	return GetLastError() == ERROR_INVALID_PARAMETER;
#else
	if (pid == 0 || pid > (uint64_t)INT32_MAX)
		return false;
	if (kill((pid_t)pid, 0) == 0)
		return false;
	return errno == ESRCH;
#endif
#endif
}

static char *savedir_format_memory_basename(const char *utf_rest)
{
	static const char hex[] = "0123456789abcdef";
	char pfx[64];
	size_t pfx_len = savedir_format_session_prefix(pfx, sizeof(pfx),
		savedir_current_pid(), savedir_current_mem_session_nonce());
	const size_t max_body = 130;
	size_t rest_len = utf_rest ? strlen(utf_rest) : 0;
	if (!rest_len) {
		// '~' is always percent-encoded for non-empty inputs (and hash suffixes
		// never place '~' at index 0), so "~empty" cannot collide with any input.
		char *out = xmalloc(pfx_len + sizeof("~empty"));
		memcpy(out, pfx, pfx_len);
		memcpy(out + pfx_len, "~empty", sizeof("~empty"));
		return out;
	}

	size_t enc_cap = rest_len * 3 + 1;
	char *enc = xmalloc(enc_cap);
	size_t enc_len = 0;
	for (size_t i = 0; i < rest_len; i++) {
		unsigned char c = (unsigned char)utf_rest[i];
		bool safe = (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z')
			|| (c >= '0' && c <= '9')
			|| c == '_' || c == '-'
			|| (c == '.' && i + 1 < rest_len);
		if (safe) {
			enc[enc_len++] = (char)c;
		} else {
			enc[enc_len++] = '%';
			enc[enc_len++] = hex[c >> 4];
			enc[enc_len++] = hex[c & 0x0f];
		}
	}
	enc[enc_len] = '\0';

	if (enc_len <= max_body) {
		char *out = xmalloc(pfx_len + enc_len + 1);
		memcpy(out, pfx, pfx_len);
		memcpy(out + pfx_len, enc, enc_len + 1);
		free(enc);
		return out;
	}

	// Keep filename comfortably under 255-byte component limits even with
	// atomic .tmp / .switch-old suffixes, while avoiding collisions via hash.
	size_t cut = max_body - 17; // reserve '~' + 16 hex chars
	if (cut >= 1 && enc[cut - 1] == '%')
		cut -= 1;
	else if (cut >= 2 && enc[cut - 2] == '%')
		cut -= 2;
	uint64_t h = savedir_fnv1a64_ascii_icase(utf_rest, rest_len);
	char *out = xmalloc(pfx_len + cut + 1 + 16 + 1);
	memcpy(out, pfx, pfx_len);
	memcpy(out + pfx_len, enc, cut);
	out[pfx_len + cut] = '~';
	for (int i = 0; i < 16; i++)
		out[pfx_len + cut + 1 + i] = hex[(h >> ((15 - i) * 4)) & 0x0f];
	out[pfx_len + cut + 17] = '\0';
	free(enc);
	return out;
}

struct savedir_mem_entry {
	char *logical_name;
	char *logical_utf8;
	char *disk_path;
};

static struct savedir_mem_entry *savedir_mem_entries = NULL;
static size_t savedir_mem_entry_count = 0;
static size_t savedir_mem_entry_cap = 0;

static void savedir_mem_registry_clear(void)
{
	savedir_lock();
	for (size_t i = 0; i < savedir_mem_entry_count; i++) {
		free(savedir_mem_entries[i].logical_name);
		free(savedir_mem_entries[i].logical_utf8);
		free(savedir_mem_entries[i].disk_path);
	}
	free(savedir_mem_entries);
	savedir_mem_entries = NULL;
	savedir_mem_entry_count = 0;
	savedir_mem_entry_cap = 0;
	savedir_unlock();
}

static bool savedir_ascii_ieq_n(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca >= 'A' && ca <= 'Z')
			ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb)
			return false;
	}
	return true;
}

static bool savedir_ascii_ieq(const char *a, const char *b)
{
	size_t la = strlen(a);
	if (la != strlen(b))
		return false;
	return savedir_ascii_ieq_n(a, b, la);
}

static bool savedir_wildcard_match_icase(const char *pat, const char *s)
{
	const char *star = strchr(pat, '*');
	if (!star)
		return savedir_ascii_ieq(pat, s);
	size_t pre = (size_t)(star - pat);
	size_t suf = strlen(star + 1);
	size_t slen = strlen(s);
	if (slen < pre + suf)
		return false;
	return savedir_ascii_ieq_n(pat, s, pre)
		&& savedir_ascii_ieq_n(star + 1, s + slen - suf, suf);
}

static void savedir_mem_registry_record(const char *raw_rest, const char *utf_rest, const char *disk_path)
{
	if (!disk_path)
		return;
	static const char tag[] = "<memory>";
	const size_t tag_len = sizeof(tag) - 1;
	size_t raw_len = raw_rest ? strlen(raw_rest) : 0;
	char *logical = xmalloc(tag_len + raw_len + 1);
	memcpy(logical, tag, tag_len);
	if (raw_len)
		memcpy(logical + tag_len, raw_rest, raw_len);
	logical[tag_len + raw_len] = '\0';

	const char *utf = utf_rest ? utf_rest : "";

	savedir_lock();
	for (size_t i = 0; i < savedir_mem_entry_count; i++) {
		if (savedir_ascii_ieq(savedir_mem_entries[i].disk_path, disk_path)) {
			if (!file_exists(savedir_mem_entries[i].disk_path)) {
				free(savedir_mem_entries[i].logical_name);
				free(savedir_mem_entries[i].logical_utf8);
				free(savedir_mem_entries[i].disk_path);
				savedir_mem_entries[i].logical_name = logical;
				savedir_mem_entries[i].logical_utf8 = xstrdup(utf);
				savedir_mem_entries[i].disk_path = xstrdup(disk_path);
			} else {
				free(logical);
			}
			savedir_unlock();
			return;
		}
	}
	if (savedir_mem_entry_count == savedir_mem_entry_cap) {
		size_t ncap = savedir_mem_entry_cap ? savedir_mem_entry_cap * 2 : 16;
		savedir_mem_entries = xrealloc(savedir_mem_entries, ncap * sizeof(*savedir_mem_entries));
		savedir_mem_entry_cap = ncap;
	}
	savedir_mem_entries[savedir_mem_entry_count++] = (struct savedir_mem_entry) {
		.logical_name = logical,
		.logical_utf8 = xstrdup(utf),
		.disk_path = xstrdup(disk_path),
	};
	savedir_unlock();
}

int savedir_memory_find(const char *pattern, int *count, char **match_out)
{
	if (!pattern || !count || !match_out)
		return -1;
	const char *target = pattern;
	if (config.save_dir && config.save_dir[0]) {
		const char *stripped_rel = NULL;
		if (strip_cjk_dir_prefix(config.save_dir, target, &stripped_rel))
			target = stripped_rel;
	}
	const char *mem_rest = NULL;
	if (!savedir_strip_memory_prefix(target, &mem_rest))
		return -1;

	char *pat_utf8 = unix_path_with(mem_rest, false);
	int found = 0;
	int visible_idx = 0;
	savedir_lock();
	for (size_t i = 0; i < savedir_mem_entry_count; i++) {
		if (!file_exists(savedir_mem_entries[i].disk_path))
			continue;
		if (visible_idx >= *count) {
			*count = visible_idx;
			if (savedir_wildcard_match_icase(pat_utf8, savedir_mem_entries[i].logical_utf8)) {
				*match_out = xstrdup(savedir_mem_entries[i].logical_name);
				found = 1;
				break;
			}
		}
		visible_idx++;
	}
	savedir_unlock();
	free(pat_utf8);
	return found;
}

static void savedir_cleanup_stale_memory_saves(uint64_t my_pid, uint64_t my_nonce)
{
	if (!config.save_dir || !config.save_dir[0])
		return;
	UDIR *d = opendir_utf8(config.save_dir);
	if (!d)
		return;
	char *d_name;
	while ((d_name = readdir_utf8(d)) != NULL) {
		uint64_t owner_pid = 0;
		uint64_t owner_nonce = 0;
		if (savedir_parse_memory_owner(d_name, &owner_pid, &owner_nonce)
				&& savedir_owner_is_dead(owner_pid, owner_nonce, my_pid, my_nonce)) {
			char *full = path_join(config.save_dir, d_name);
			ustat st;
			if (stat_utf8(full, &st) == 0 && S_ISREG(st.st_mode))
				remove_utf8(full);
			free(full);
		}
		free(d_name);
	}
	closedir_utf8(d);
}

void savedir_begin_memory_session(void)
{
	savedir_mem_registry_clear();
	uint64_t prev = atomic_load_explicit(&g_savedir_mem_session_nonce, memory_order_acquire);
	uint64_t nonce;
	do {
		nonce = savedir_make_session_nonce(prev);
	} while (!atomic_compare_exchange_weak_explicit(&g_savedir_mem_session_nonce, &prev, nonce,
			memory_order_acq_rel, memory_order_acquire));
	savedir_cleanup_stale_memory_saves(savedir_current_pid(), nonce);
}

void savedir_cleanup_memory_saves(void)
{
	savedir_mem_registry_clear();
	if (!config.save_dir || !config.save_dir[0])
		return;
	uint64_t my_nonce = atomic_load_explicit(&g_savedir_mem_session_nonce, memory_order_acquire);
	if (my_nonce == 0)
		return;
	char pfx[64];
	size_t pfx_len = savedir_format_session_prefix(pfx, sizeof(pfx), savedir_current_pid(), my_nonce);
	if (!pfx_len)
		return;
	UDIR *d = opendir_utf8(config.save_dir);
	if (!d)
		return;
	char *d_name;
	while ((d_name = readdir_utf8(d)) != NULL) {
		bool match = !strncmp(d_name, pfx, pfx_len)
			|| (d_name[0] == '.' && !strncmp(d_name + 1, pfx, pfx_len));
		if (match) {
			char *full = path_join(config.save_dir, d_name);
			ustat st;
			if (stat_utf8(full, &st) == 0 && S_ISREG(st.st_mode))
				remove_utf8(full);
			free(full);
		}
		free(d_name);
	}
	closedir_utf8(d);
}

static char *resolve_savedir_path(const char *path, bool *exists_out)
{
	if (exists_out)
		*exists_out = false;

	const char *target = path ? path : "";
	if (config.save_dir && config.save_dir[0]) {
		const char *stripped_rel = NULL;
		if (strip_cjk_dir_prefix(config.save_dir, target, &stripped_rel))
			target = stripped_rel;
	}

	const char *mem_rest = NULL;
	if (savedir_strip_memory_prefix(target, &mem_rest)) {
		char *utf_rest = unix_path_with(mem_rest, false);
		char *mem_base = savedir_format_memory_basename(utf_rest);
		const char *dir = (config.save_dir && config.save_dir[0]) ? config.save_dir : ".";
		char *resolved_mem = join_dir_and_rel(dir, mem_base);
		free(mem_base);
		char *final_mem = resolved_mem;
		if (!config.save_dir || !config.save_dir[0]) {
			if (exists_out)
				*exists_out = file_exists(resolved_mem);
		} else {
			char *matched = NULL;
			bool dir_missing = false;
			if (!savedir_cache_lookup(resolved_mem, &matched, &dir_missing)) {
				if (exists_out)
					*exists_out = file_exists(resolved_mem);
			} else if (matched) {
				if (!exists_out) {
					free(resolved_mem);
					final_mem = matched;
				} else if (file_exists(matched)) {
					*exists_out = true;
					free(resolved_mem);
					final_mem = matched;
				} else {
					file_note_fs_mutation();
					free(matched);
				}
			}
		}
		if (!exists_out)
			savedir_mem_registry_record(mem_rest, utf_rest, final_mem);
		free(utf_rest);
		return final_mem;
	}

	if (!config.save_dir || !config.save_dir[0]) {
		char *p = resolve_path(config.save_dir ? config.save_dir : ".", path, false);
		if (exists_out)
			*exists_out = p && file_exists(p);
		return p;
	}

	char *utf_primary = unix_path_with(target, false);
	char *resolved_primary = join_dir_and_rel(config.save_dir, utf_primary);

	char *matched = NULL;
	bool dir_missing = false;
	if (!savedir_cache_lookup(resolved_primary, &matched, &dir_missing)) {
		free(utf_primary);
		free(resolved_primary);
		char *p = resolve_path(config.save_dir, path, false);
		if (exists_out)
			*exists_out = p && file_exists(p);
		return p;
	}

	if (matched) {
		free(utf_primary);
		if (!exists_out) {
			free(resolved_primary);
			return matched;
		}
		if (file_exists(matched)) {
			*exists_out = true;
			free(resolved_primary);
			return matched;
		}
		// External deletion inside the TTL window: invalidate immediately.
		file_note_fs_mutation();
		free(matched);
		return resolved_primary;
	}

	if (sys4_text_is_gbk) {
		char *utf_legacy = unix_path_with(target, true);
		char *resolved_legacy = join_dir_and_rel(config.save_dir, utf_legacy);
		if (strcmp(resolved_legacy, resolved_primary) != 0) {
			char *legacy_match = NULL;
			bool legacy_missing = false;
			if (savedir_cache_lookup(resolved_legacy, &legacy_match, &legacy_missing) && legacy_match) {
				free(utf_legacy);
				free(resolved_legacy);
				free(utf_primary);
				if (!exists_out) {
					free(resolved_primary);
					return legacy_match;
				}
				if (file_exists(legacy_match)) {
					*exists_out = true;
					free(resolved_primary);
					return legacy_match;
				}
				file_note_fs_mutation();
				free(legacy_match);
				return resolved_primary;
			}
#ifdef __SWITCH__
			if (legacy_missing && strchr(utf_legacy, '/')) {
				char *icase = path_get_icase(resolved_legacy);
				if (icase) {
					free(utf_legacy);
					free(resolved_legacy);
					free(utf_primary);
					free(resolved_primary);
					if (exists_out)
						*exists_out = true;
					return icase;
				}
			}
#endif
		}
		free(utf_legacy);
		free(resolved_legacy);
	}

#ifdef __SWITCH__
	if (dir_missing && strchr(utf_primary, '/')) {
		char *icase = path_get_icase(resolved_primary);
		if (icase) {
			free(utf_primary);
			free(resolved_primary);
			if (exists_out)
				*exists_out = true;
			return icase;
		}
	}
#endif

	free(utf_primary);
	return resolved_primary;
}

char *savedir_path(const char *path)
{
	return resolve_savedir_path(path, NULL);
}

bool savedir_file_exists(const char *path)
{
	if (!path || !*path)
		return false;
	bool exists = false;
	char *resolved = resolve_savedir_path(path, &exists);
	free(resolved);
	return exists;
}

static void get_local_tm(struct tm *tm_out, long *nsec_out)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	time_t t = ts.tv_sec;
	memset(tm_out, 0, sizeof(*tm_out));
#if (defined(_WIN32) || defined(__WIN32__))
	localtime_s(tm_out, &t);
#else
	localtime_r(&t, tm_out);
#endif
	if (nsec_out)
		*nsec_out = ts.tv_nsec;
}

void get_date(int *year, int *month, int *mday, int *wday)
{
	struct tm tm;
	get_local_tm(&tm, NULL);

	*year  = tm.tm_year + 1900;
	*month = tm.tm_mon + 1;
	*mday  = tm.tm_mday;
	*wday  = tm.tm_wday;
}

void get_time(int *hour, int *min, int *sec, int *ms)
{
	struct tm tm;
	long nsec = 0;
	get_local_tm(&tm, &nsec);

	*hour = tm.tm_hour;
	*min  = tm.tm_min;
	*sec  = tm.tm_sec;
	*ms   = (int)(nsec / 1000000);
}

void indent_message(int indent, const char *fmt, ...)
{
	for (int i = 0; i < indent; i++)
		putchar('\t');

	va_list ap;
	va_start(ap, fmt);
	sys_vmessage(fmt, ap);
	va_end(ap);
}

void log_message(const char *log, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (dbg_dap)
		dbg_dap_log(log, fmt, ap);
	else
		sys_vmessage(fmt, ap);
	va_end(ap);
}
