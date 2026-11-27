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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/buffer.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/zlib.h"

#include "hll.h"
#include "savedata.h"
#include "xsystem4.h"

static struct string *save_path = NULL;
static struct hash_table *table = NULL;
static uint8_t *last_saved_raw = NULL;
static size_t last_saved_raw_size = 0;
static bool has_last_saved_raw = false;

static void csd_clear_saved_cache(void)
{
	free(last_saved_raw);
	last_saved_raw = NULL;
	last_saved_raw_size = 0;
	has_last_saved_raw = false;
}

enum csd_type {
	CSD_INT = 1,
	CSD_FLOAT = 2,
	CSD_STRING = 3,
	CSD_BOOL = 4,
};

struct csd_entry {
	enum csd_type type;
	union {
		int32_t i;
		float f;
		struct string *s;
	};
};

static struct hash_table *csd_table(void)
{
	if (!table) {
		table = ht_create(64);
	}
	return table;
}

static void csd_free_entry(void *p)
{
	struct csd_entry *entry = p;
	if (!entry)
		return;
	if (entry->type == CSD_STRING && entry->s)
		free_string(entry->s);
	free(entry);
}

static void csd_clear_table(void)
{
	csd_clear_saved_cache();
	if (!table)
		return;
	ht_foreach_value(table, csd_free_entry);
	ht_free(table);
	table = NULL;
}

static char *csd_save_path(void)
{
	return savedir_path(save_path ? save_path->text : "CommonSystemData.sav");
}

struct csd_collect_ctx {
	struct ht_slot **slots;
	size_t count;
	size_t cap;
};

static void csd_collect_slot(struct ht_slot *slot, void *data)
{
	if (!slot->key || !slot->value)
		return;
	struct csd_collect_ctx *ctx = data;
	if (ctx->count == ctx->cap) {
		size_t new_cap = ctx->cap ? ctx->cap * 2 : 16;
		ctx->slots = xrealloc_array(ctx->slots, ctx->cap, new_cap, sizeof(struct ht_slot *));
		ctx->cap = new_cap;
	}
	ctx->slots[ctx->count++] = slot;
}

static int csd_cmp_slots(const void *a, const void *b)
{
	const struct ht_slot *sa = *(const struct ht_slot * const *)a;
	const struct ht_slot *sb = *(const struct ht_slot * const *)b;
	return strcmp(sa->key, sb->key);
}

static uint8_t *csd_serialize_raw(size_t *size_out)
{
	struct csd_collect_ctx ctx = { 0 };
	if (table) {
		ht_foreach(table, csd_collect_slot, &ctx);
		if (ctx.count > 1)
			qsort(ctx.slots, ctx.count, sizeof(struct ht_slot *), csd_cmp_slots);
	}

	struct buffer w;
	buffer_init(&w, NULL, 0);
	buffer_write_int32(&w, (uint32_t)ctx.count);
	for (size_t i = 0; i < ctx.count; i++) {
		struct ht_slot *slot = ctx.slots[i];
		struct csd_entry *entry = slot->value;
		buffer_write_cstringz(&w, slot->key);
		buffer_write_int32(&w, entry->type);
		switch (entry->type) {
		case CSD_INT:
			buffer_write_int32(&w, entry->i);
			break;
		case CSD_FLOAT:
			buffer_write_float(&w, entry->f);
			break;
		case CSD_STRING:
			buffer_write_cstringz(&w, entry->s ? entry->s->text : "");
			break;
		case CSD_BOOL:
			buffer_write_int32(&w, entry->i ? 1 : 0);
			break;
		}
	}
	free(ctx.slots);
	*size_out = w.index;
	return w.buf;
}

static void csd_update_saved_cache(uint8_t *raw, size_t raw_size)
{
	free(last_saved_raw);
	last_saved_raw = raw;
	last_saved_raw_size = raw_size;
	has_last_saved_raw = true;
}

static bool csd_save(void)
{
	size_t raw_size = 0;
	uint8_t *raw_buf = csd_serialize_raw(&raw_size);

	char *path = csd_save_path();
	if (has_last_saved_raw && raw_size == last_saved_raw_size &&
	    (raw_size == 0 || memcmp(raw_buf, last_saved_raw, raw_size) == 0) &&
	    file_exists(path)) {
		free(path);
		free(raw_buf);
		return true;
	}

	size_t comp_bound = zlib_compress_bound(raw_size);
	uint8_t *comp_buf = xmalloc(comp_bound);
	size_t comp_size = zlib_compress(comp_buf, comp_bound, raw_buf, raw_size, ZLIB_DEFAULT_COMPRESSION);
	if (!comp_size) {
		free(path);
		free(comp_buf);
		free(raw_buf);
		return false;
	}

	uint8_t header[16] = { 'C', 'S', 'D', 0 };
	LittleEndian_putDW(header, 4, 0);
	LittleEndian_putDW(header, 8, (uint32_t)raw_size);
	LittleEndian_putDW(header, 12, (uint32_t)comp_size);

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	if (!fp) {
		WARNING("Failed to open CommonSystemData file '%s': %s", display_utf0(path), strerror(errno));
		free(path);
		free(comp_buf);
		free(raw_buf);
		return false;
	}

	bool ok = fwrite(header, sizeof(header), 1, fp) == 1 &&
		(comp_size == 0 || fwrite(comp_buf, comp_size, 1, fp) == 1);
	free(comp_buf);
	if (!ok) {
		atomic_save_abort(fp, tmp_path);
		free(path);
		free(raw_buf);
		return false;
	}
	ok = atomic_save_commit(fp, path, tmp_path, false);
	free(path);
	if (ok)
		csd_update_saved_cache(raw_buf, raw_size);
	else
		free(raw_buf);
	return ok;
}

static struct csd_entry *csd_put_key(const char *key, enum csd_type type)
{
	struct ht_slot *slot = ht_put(csd_table(), key, NULL);
	struct csd_entry *entry = slot->value;
	if (!entry) {
		entry = xcalloc(1, sizeof(struct csd_entry));
		slot->value = entry;
	} else if (entry->type == CSD_STRING && entry->s) {
		free_string(entry->s);
		entry->s = NULL;
	}
	entry->type = type;
	return entry;
}

static struct csd_entry *csd_get_key(struct string *name, enum csd_type type)
{
	void *p = NULL;
	if (!table || !_ht_get(table, name->text, &p) || !p)
		return NULL;
	struct csd_entry *entry = p;
	return entry->type == type ? entry : NULL;
}

static void CommonSystemData_SetFullPathSaveFileName(struct string *filename)
{
	if (save_path)
		free_string(save_path);
	save_path = string_dup(filename);
	csd_clear_saved_cache();
	NOTICE("SetFullPathSaveFileName(%s)", filename->text);
}

static bool CommonSystemData_LoadAtStartup(void)
{
	csd_clear_table();

	char *path = csd_save_path();
	if (!file_exists(path)) {
		free(path);
		return true;
	}

	size_t file_size = 0;
	uint8_t *file_buf = file_read(path, &file_size);
	free(path);
	if (!file_buf)
		return false;

	if (file_size < 16 || memcmp(file_buf, "CSD\0", 4) != 0) {
		free(file_buf);
		return false;
	}
	uint32_t version = LittleEndian_getDW(file_buf, 4);
	uint32_t raw_size = LittleEndian_getDW(file_buf, 8);
	uint32_t comp_size = LittleEndian_getDW(file_buf, 12);
	if (version != 0 || 16 + (size_t)comp_size > file_size) {
		free(file_buf);
		return false;
	}

	if (raw_size == 0) {
		free(file_buf);
		return true;
	}

	uint8_t *raw_buf = xmalloc(raw_size);
	if (!zlib_decompress_exact(raw_buf, raw_size, file_buf + 16, comp_size)) {
		free(raw_buf);
		free(file_buf);
		return false;
	}
	free(file_buf);

	struct buffer r;
	buffer_init(&r, raw_buf, raw_size);
	if (buffer_remaining(&r) < 4) {
		free(raw_buf);
		return false;
	}
	int32_t count = buffer_read_int32(&r);
	for (int32_t i = 0; i < count && buffer_remaining(&r) > 0; i++) {
		char *key = buffer_skip_string(&r);
		if (buffer_remaining(&r) < 4)
			break;
		int32_t type = buffer_read_int32(&r);
		struct csd_entry *entry;
		switch (type) {
		case CSD_INT:
			if (buffer_remaining(&r) < 4) break;
			entry = csd_put_key(key, CSD_INT);
			entry->i = buffer_read_int32(&r);
			break;
		case CSD_FLOAT:
			if (buffer_remaining(&r) < 4) break;
			entry = csd_put_key(key, CSD_FLOAT);
			entry->f = buffer_read_float(&r);
			break;
		case CSD_STRING:
			entry = csd_put_key(key, CSD_STRING);
			entry->s = buffer_read_string(&r);
			break;
		case CSD_BOOL:
			if (buffer_remaining(&r) < 4) break;
			entry = csd_put_key(key, CSD_BOOL);
			entry->i = !!buffer_read_int32(&r);
			break;
		default:
			free(raw_buf);
			return false;
		}
	}
	free(raw_buf);
	size_t cached_size = 0;
	uint8_t *cached_raw = csd_serialize_raw(&cached_size);
	csd_update_saved_cache(cached_raw, cached_size);
	return true;
}

static void CommonSystemData_ModuleFini(void)
{
	csd_clear_table();
	if (save_path) {
		free_string(save_path);
		save_path = NULL;
	}
}

static bool CommonSystemData_SetDataInt(struct string *name, int value)
{
	struct csd_entry *entry = csd_put_key(name->text, CSD_INT);
	entry->i = value;
	return csd_save();
}

static bool CommonSystemData_SetDataFloat(struct string *name, float value)
{
	struct csd_entry *entry = csd_put_key(name->text, CSD_FLOAT);
	entry->f = value;
	return csd_save();
}

static bool CommonSystemData_SetDataString(struct string *name, struct string *value)
{
	struct csd_entry *entry = csd_put_key(name->text, CSD_STRING);
	entry->s = string_dup(value);
	return csd_save();
}

static bool CommonSystemData_SetDataBool(struct string *name, bool value)
{
	struct csd_entry *entry = csd_put_key(name->text, CSD_BOOL);
	entry->i = value ? 1 : 0;
	return csd_save();
}

static bool CommonSystemData_GetDataInt(struct string *name, int *value)
{
	struct csd_entry *entry = csd_get_key(name, CSD_INT);
	if (entry) {
		*value = entry->i;
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataFloat(struct string *name, float *value)
{
	struct csd_entry *entry = csd_get_key(name, CSD_FLOAT);
	if (entry) {
		*value = entry->f;
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataString(struct string *name, struct string **value)
{
	struct csd_entry *entry = csd_get_key(name, CSD_STRING);
	if (entry) {
		if (*value)
			free_string(*value);
		*value = string_dup(entry->s);
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataBool(struct string *name, int *value)
{
	struct csd_entry *entry = csd_get_key(name, CSD_BOOL);
	if (entry) {
		*value = entry->i ? 1 : 0;
		return true;
	}
	return false;
}

HLL_LIBRARY(CommonSystemData,
	    HLL_EXPORT(_ModuleFini, CommonSystemData_ModuleFini),
	    HLL_EXPORT(SetFullPathSaveFileName, CommonSystemData_SetFullPathSaveFileName),
	    HLL_EXPORT(LoadAtStartup, CommonSystemData_LoadAtStartup),
	    HLL_EXPORT(SetDataInt, CommonSystemData_SetDataInt),
	    HLL_EXPORT(SetDataFloat, CommonSystemData_SetDataFloat),
	    HLL_EXPORT(SetDataString, CommonSystemData_SetDataString),
	    HLL_EXPORT(SetDataBool, CommonSystemData_SetDataBool),
	    HLL_EXPORT(GetDataInt, CommonSystemData_GetDataInt),
	    HLL_EXPORT(GetDataFloat, CommonSystemData_GetDataFloat),
	    HLL_EXPORT(GetDataString, CommonSystemData_GetDataString),
	    HLL_EXPORT(GetDataBool, CommonSystemData_GetDataBool));
