/* Copyright (C) 2026
 * AIN v14 HashMap HLL implementation, GPL-2.0-or-later.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "hll.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"

struct hm_entry {
	struct string *key;
	int value;
	uint32_t hash;
	int next_in_bucket;
};

struct hashmap_obj {
	bool active;
	struct hm_entry *entries;
	int count;
	int cap;
	int *buckets;
	int nbuckets;
};

static struct hashmap_obj *s_maps = NULL;
static int s_nr_maps = 0;

static uint32_t hm_hash_bytes(const char *s, int len)
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < len; i++) {
		h ^= (uint8_t)s[i];
		h *= 16777619u;
	}
	return h;
}

static struct hashmap_obj *hm_get(int id)
{
	if (id <= 0 || id >= s_nr_maps || !s_maps[id].active)
		return NULL;
	return &s_maps[id];
}

static void hm_rehash(struct hashmap_obj *m, int new_nbuckets)
{
	int pow2 = 16;
	while (pow2 < new_nbuckets && pow2 < (1 << 30))
		pow2 <<= 1;
	new_nbuckets = pow2;
	free(m->buckets);
	m->nbuckets = new_nbuckets;
	m->buckets = xmalloc((size_t)new_nbuckets * sizeof(int));
	for (int i = 0; i < new_nbuckets; i++)
		m->buckets[i] = -1;
	int mask = new_nbuckets - 1;
	for (int i = 0; i < m->count; i++) {
		int b = (int)(m->entries[i].hash & (uint32_t)mask);
		m->entries[i].next_in_bucket = m->buckets[b];
		m->buckets[b] = i;
	}
}

static int hm_find_index(struct hashmap_obj *m, const struct string *key, uint32_t h)
{
	if (!m || !m->nbuckets || !m->count)
		return -1;
	int b = (int)(h & (uint32_t)(m->nbuckets - 1));
	for (int idx = m->buckets[b]; idx >= 0; idx = m->entries[idx].next_in_bucket) {
		if (m->entries[idx].hash == h &&
		    m->entries[idx].key->size == key->size &&
		    !memcmp(m->entries[idx].key->text, key->text, (size_t)key->size)) {
			return idx;
		}
	}
	return -1;
}

static void hm_clear_entries(struct hashmap_obj *m)
{
	for (int i = 0; i < m->count; i++)
		free_string(m->entries[i].key);
	m->count = 0;
	if (m->buckets) {
		for (int i = 0; i < m->nbuckets; i++)
			m->buckets[i] = -1;
	}
}

static void hm_ensure_slot(int id)
{
	if (id < s_nr_maps)
		return;
	int new_nr = s_nr_maps ? s_nr_maps * 2 : 32;
	while (new_nr <= id)
		new_nr *= 2;
	s_maps = xrealloc_array(s_maps, s_nr_maps, new_nr, sizeof(struct hashmap_obj));
	s_nr_maps = new_nr;
}

static int HashMap_Create(void)
{
	int id = 1;
	while (id < s_nr_maps && s_maps[id].active)
		id++;
	hm_ensure_slot(id);
	memset(&s_maps[id], 0, sizeof(struct hashmap_obj));
	s_maps[id].active = true;
	hm_rehash(&s_maps[id], 16);
	return id;
}

static void HashMap_Release(int id)
{
	struct hashmap_obj *m = hm_get(id);
	if (!m)
		return;
	hm_clear_entries(m);
	free(m->entries);
	free(m->buckets);
	memset(m, 0, sizeof(*m));
}

static void HashMap_Free(int id)
{
	struct hashmap_obj *m = hm_get(id);
	if (!m)
		return;
	hm_clear_entries(m);
}

static bool HashMap_Add(int id, struct string *key, int value)
{
	struct hashmap_obj *m = hm_get(id);
	if (!m || !key)
		return false;
	uint32_t h = hm_hash_bytes(key->text, key->size);
	int existing = hm_find_index(m, key, h);
	if (existing >= 0)
		return false;
	if (!m->nbuckets || (m->count + 1) * 4 >= m->nbuckets * 3)
		hm_rehash(m, m->nbuckets ? m->nbuckets * 2 : 16);
	if (m->count >= m->cap) {
		int new_cap = m->cap ? m->cap * 2 : 16;
		m->entries = xrealloc(m->entries, (size_t)new_cap * sizeof(struct hm_entry));
		m->cap = new_cap;
	}
	int idx = m->count++;
	int b = (int)(h & (uint32_t)(m->nbuckets - 1));
	m->entries[idx] = (struct hm_entry){
		.key = string_dup(key),
		.value = value,
		.hash = h,
		.next_in_bucket = m->buckets[b],
	};
	m->buckets[b] = idx;
	return true;
}

static void HashMap_Erase(int id, struct string *key)
{
	struct hashmap_obj *m = hm_get(id);
	if (!m || !key || !m->count)
		return;
	uint32_t h = hm_hash_bytes(key->text, key->size);
	int idx = hm_find_index(m, key, h);
	if (idx < 0)
		return;
	free_string(m->entries[idx].key);
	if (idx < m->count - 1) {
		memmove(&m->entries[idx], &m->entries[idx + 1],
			(size_t)(m->count - 1 - idx) * sizeof(struct hm_entry));
	}
	m->count--;
	hm_rehash(m, m->nbuckets);
}

static int HashMap_Numof(int id)
{
	struct hashmap_obj *m = hm_get(id);
	return m ? m->count : 0;
}

static bool HashMap_Empty(int id)
{
	struct hashmap_obj *m = hm_get(id);
	return !m || m->count == 0;
}

static bool HashMap_Any1(int id)
{
	struct hashmap_obj *m = hm_get(id);
	return m && m->count > 0;
}

static bool HashMap_Any2(int id, struct string *key)
{
	struct hashmap_obj *m = hm_get(id);
	if (!m || !key)
		return false;
	uint32_t h = hm_hash_bytes(key->text, key->size);
	return hm_find_index(m, key, h) >= 0;
}

static void *HashMap_ResolveAny(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 1)
		return (void *)HashMap_Any1;
	if (f->nr_arguments == 2)
		return (void *)HashMap_Any2;
	return NULL;
}

static bool HashMap_At(int id, struct string *key, int *value_out)
{
	struct hashmap_obj *m = hm_get(id);
	if (!m || !key)
		return false;
	uint32_t h = hm_hash_bytes(key->text, key->size);
	int idx = hm_find_index(m, key, h);
	if (idx < 0)
		return false;
	if (value_out)
		*value_out = m->entries[idx].value;
	return true;
}

static struct page *HashMap_GetKeyList(int id)
{
	struct hashmap_obj *m = hm_get(id);
	int n = m ? m->count : 0;
	struct ain_type str_t = { .data = AIN_STRING, .struc = -1 };
	struct page *arr = v12_array_new(&str_t, n, false);
	for (int i = 0; i < n; i++) {
		heap_unref(arr->values[i].i);
		arr->values[i].i = heap_alloc_string(string_dup(m->entries[i].key));
	}
	return arr;
}

static struct page *HashMap_GetValueList(int id)
{
	struct hashmap_obj *m = hm_get(id);
	int n = m ? m->count : 0;
	struct ain_type int_t = { .data = AIN_INT, .struc = -1 };
	struct page *arr = v12_array_new(&int_t, n, false);
	for (int i = 0; i < n; i++)
		arr->values[i].i = m->entries[i].value;
	return arr;
}

#define HMAP_MAGIC 0x50414d48 /* 'HMAP' */

static bool HashMap_Save(struct page **buf_ptr)
{
	if (!buf_ptr)
		return false;
	int active_maps = 0;
	size_t total_ints = 3; /* magic, version, active_maps */
	for (int id = 1; id < s_nr_maps; id++) {
		if (!s_maps[id].active)
			continue;
		active_maps++;
		total_ints += 2; /* id, count */
		for (int i = 0; i < s_maps[id].count; i++) {
			int klen = s_maps[id].entries[i].key->size;
			total_ints += 2 + (size_t)(((uint64_t)(uint32_t)klen + 3) / 4); /* klen, packed_bytes, val */
		}
	}
	struct ain_type int_t = { .data = AIN_INT, .struc = -1 };
	struct page *out = v12_array_new(&int_t, (int)total_ints, false);
	int p = 0;
	out->values[p++].i = HMAP_MAGIC;
	out->values[p++].i = 1;
	out->values[p++].i = active_maps;
	for (int id = 1; id < s_nr_maps; id++) {
		if (!s_maps[id].active)
			continue;
		out->values[p++].i = id;
		out->values[p++].i = s_maps[id].count;
		for (int i = 0; i < s_maps[id].count; i++) {
			struct string *k = s_maps[id].entries[i].key;
			int klen = k->size;
			int words = (int)(((uint64_t)(uint32_t)klen + 3) / 4);
			out->values[p++].i = klen;
			for (int w = 0; w < words; w++) {
				uint32_t word = 0;
				for (int b = 0; b < 4 && w * 4 + b < klen; b++)
					word |= ((uint32_t)(uint8_t)k->text[w * 4 + b]) << (b * 8);
				out->values[p++].i = (int32_t)word;
			}
			out->values[p++].i = s_maps[id].entries[i].value;
		}
	}
	if (*buf_ptr) {
		delete_page_vars(*buf_ptr);
		free_page(*buf_ptr);
	}
	*buf_ptr = out;
	return true;
}

static bool HashMap_Load(struct page **buf_ptr)
{
	if (!buf_ptr || !*buf_ptr || (*buf_ptr)->type != ARRAY_PAGE)
		return false;
	struct page *in = *buf_ptr;
	int n = in->nr_vars;
	if (n < 3 || in->values[0].i != HMAP_MAGIC || in->values[1].i != 1)
		return false;
	int active_maps = in->values[2].i;
	if (active_maps < 0)
		return false;
	for (int id = 1; id < s_nr_maps; id++) {
		if (s_maps[id].active)
			HashMap_Release(id);
	}
	int p = 3;
	for (int m_idx = 0; m_idx < active_maps; m_idx++) {
		if (n - p < 2)
			return false;
		int id = in->values[p++].i;
		int count = in->values[p++].i;
		if (id <= 0 || count < 0 || count > n - p)
			return false;
		hm_ensure_slot(id);
		if (s_maps[id].active)
			HashMap_Release(id);
		memset(&s_maps[id], 0, sizeof(struct hashmap_obj));
		s_maps[id].active = true;
		int target_buckets = count > (1 << 29) ? (1 << 30) : max(16, count * 2);
		hm_rehash(&s_maps[id], target_buckets);
		for (int i = 0; i < count; i++) {
			if (p >= n)
				return false;
			int klen = in->values[p++].i;
			if (klen < 0)
				return false;
			int remain = n - p;
			if (remain < 1)
				return false;
			uint64_t words64 = ((uint64_t)(uint32_t)klen + 3) / 4;
			if (words64 > (uint64_t)(remain - 1))
				return false;
			int words = (int)words64;
			char *kbuf = xmalloc((size_t)klen + 1);
			for (int w = 0; w < words; w++) {
				uint32_t word = (uint32_t)in->values[p++].i;
				for (int b = 0; b < 4 && w * 4 + b < klen; b++)
					kbuf[w * 4 + b] = (char)((word >> (b * 8)) & 0xff);
			}
			kbuf[klen] = '\0';
			int val = in->values[p++].i;
			struct string *ks = make_string(kbuf, (unsigned int)klen);
			free(kbuf);
			HashMap_Add(id, ks, val);
			free_string(ks);
		}
	}
	return true;
}

static void HashMap_ModuleFini(void)
{
	for (int id = 1; id < s_nr_maps; id++) {
		if (s_maps[id].active)
			HashMap_Release(id);
	}
	free(s_maps);
	s_maps = NULL;
	s_nr_maps = 0;
}

HLL_LIBRARY(HashMap,
	    HLL_EXPORT(_ModuleFini, HashMap_ModuleFini),
	    HLL_EXPORT(Create, HashMap_Create),
	    HLL_EXPORT(Release, HashMap_Release),
	    HLL_EXPORT(Free, HashMap_Free),
	    HLL_EXPORT(Add, HashMap_Add),
	    HLL_EXPORT(Erase, HashMap_Erase),
	    HLL_EXPORT(Numof, HashMap_Numof),
	    HLL_EXPORT(Empty, HashMap_Empty),
	    HLL_OVERLOAD(Any, HashMap_ResolveAny),
	    HLL_EXPORT(At, HashMap_At),
	    HLL_EXPORT(GetKeyList, HashMap_GetKeyList),
	    HLL_EXPORT(GetValueList, HashMap_GetValueList),
	    HLL_EXPORT(Save, HashMap_Save),
	    HLL_EXPORT(Load, HashMap_Load));
