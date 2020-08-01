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
#include <string.h>
#include "system4.h"
#include "system4/hashtable.h"

struct ht_bucket {
	size_t nr_slots;
	struct ht_slot slots[];
};

struct hash_table {
	size_t nr_buckets;
	struct ht_bucket *buckets[];
};

static unsigned long string_hash(const char *_str)
{
	const unsigned char *str = (const unsigned char*)_str;
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

struct hash_table *ht_create(size_t nr_buckets)
{
	struct hash_table *ht = xmalloc(sizeof(struct hash_table) + sizeof(struct ht_bucket*)*nr_buckets);
	ht->nr_buckets = nr_buckets;
	for (size_t i = 0; i < nr_buckets; i++) {
		ht->buckets[i] = NULL;
	}
	return ht;
}

void *ht_get(struct hash_table *ht, const char *key, void *dflt)
{
	unsigned long k = string_hash(key) & (ht->nr_buckets - 1);
	if (!ht->buckets[k])
		return dflt;
	for (size_t i = 0; i < ht->buckets[k]->nr_slots; i++) {
		if (!strcmp(ht->buckets[k]->slots[i].key, key))
			return ht->buckets[k]->slots[i].value;
	}
	return dflt;
}

struct ht_slot *ht_put(struct hash_table *ht, const char *key, void *dflt)
{
	unsigned long k = string_hash(key) & (ht->nr_buckets - 1);

	// init new bucket
	if (!ht->buckets[k]) {
		ht->buckets[k] = xmalloc(sizeof(struct ht_bucket) + sizeof(struct ht_slot));
		ht->buckets[k]->nr_slots = 1;
		ht->buckets[k]->slots[0].key = strdup(key);
		ht->buckets[k]->slots[0].value = dflt;
		return &ht->buckets[k]->slots[0];
	}

	// search for key in bucket
	for (size_t i = 0; i < ht->buckets[k]->nr_slots; i++) {
		if (!strcmp(ht->buckets[k]->slots[i].key, key))
			return &ht->buckets[k]->slots[i];
	}

	// alloc new slot
	size_t i = ht->buckets[k]->nr_slots;
	ht->buckets[k] = xrealloc(ht->buckets[k], sizeof(struct ht_bucket) + sizeof(struct ht_slot)*(i+1));
	ht->buckets[k]->nr_slots = i+1;
	ht->buckets[k]->slots[i].key = strdup(key);
	ht->buckets[k]->slots[i].value = dflt;
	return &ht->buckets[k]->slots[i];
}

void ht_foreach_value(struct hash_table *ht, void(*fun)(void*))
{
	for (size_t i = 0; i < ht->nr_buckets; i++) {
		if (!ht->buckets[i])
			continue;
		for (size_t j = 0; j < ht->buckets[i]->nr_slots; j++) {
			fun(ht->buckets[i]->slots[j].value);
		}
	}
}

void ht_free(struct hash_table *ht)
{
	for (size_t i = 0; i < ht->nr_buckets; i++) {
		if (!ht->buckets[i])
			continue;
		for (size_t j = 0; j < ht->buckets[i]->nr_slots; j++) {
			free(ht->buckets[i]->slots[j].key);
		}
		free(ht->buckets[i]);
	}
	free(ht);
}
