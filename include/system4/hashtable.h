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

#ifndef SYSTEM4_HASHTABLE_H
#define SYSTEM4_HASHTABLE_H

#include <stddef.h>

struct ht_slot {
	char *key;
	void *value;
};

struct hash_table;

struct hash_table *ht_create(size_t nr_buckets);
void ht_free(struct hash_table *ht);

void *ht_get(struct hash_table *ht, const char *key, void *dflt);
struct ht_slot *ht_put(struct hash_table *ht, const char *key, void *dflt);
void ht_foreach_value(struct hash_table *ht, void(*fun)(void*));

#endif /* SYSTEM4_HASHTABLE_H */
