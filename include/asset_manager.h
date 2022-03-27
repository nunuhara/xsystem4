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

#ifndef SYSTEM4_ASSET_MANAGER_H
#define SYSTEM4_ASSET_MANAGER_H

#include <stdbool.h>

struct archive_data;
struct cg;
struct cg_metrics;

enum asset_type {
	ASSET_BGM,
	ASSET_SOUND,
	ASSET_VOICE,
	ASSET_CG,
	ASSET_FLAT,
	ASSET_PACT,
	ASSET_DATA
};
#define ASSET_TYPE_MAX (ASSET_DATA+1)

const char *asset_strtype(enum asset_type type);
void asset_manager_init(void);
bool asset_exists(enum asset_type type, int no);
struct archive_data *asset_get(enum asset_type type, int no);

// FIXME: CG code should be refactored to read from buffer only, not archive,
//        then caller can just use asset_get and cg_load
struct cg *asset_cg_load(int no);
struct cg *asset_cg_load_by_name(const char *name, int *no);
int asset_cg_name_to_index(const char *name);
bool asset_cg_exists(int no);
bool asset_cg_get_metrics(int no, struct cg_metrics *metrics);
void asset_cg_index_init(void);

#endif /* SYSTEM4_ASSET_MANAGER_H */
