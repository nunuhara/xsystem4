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

#ifndef SYSTEM4_ID_POOL_H
#define SYSTEM4_ID_POOL_H

// A reasonable base value for pointer-based handles.
#define ID_POOL_HANDLE_BASE 0x100000

struct id_pool {
	void **slots;
	int nr_slots;
	int base;
};

void id_pool_init(struct id_pool *pool, int base);
void id_pool_delete(struct id_pool *pool);
void *id_pool_release(struct id_pool *pool, int id);
int id_pool_count(struct id_pool *pool);
int id_pool_get_unused(struct id_pool *pool);
int id_pool_get_first(struct id_pool *pool);
int id_pool_get_next(struct id_pool *pool, int id);
void *id_pool_get(struct id_pool *pool, int id);
void *id_pool_set(struct id_pool *pool, int id, void *data);

#endif /* SYSTEM4_ID_POOL_H */
