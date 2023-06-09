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
	ASSET_DATA,
	ASSET_FLASH
};
#define ASSET_TYPE_MAX (ASSET_FLASH+1)

const char *asset_strtype(enum asset_type type);
void asset_manager_init(void);
bool asset_manager_load_archive(enum asset_type type, const char *archive_name);
bool asset_exists(enum asset_type type, int no);
bool asset_exists_by_name(enum asset_type type, const char *name, int *id_out);
struct archive_data *asset_get(enum asset_type type, int no);
struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *id_out);

struct cg *asset_cg_load(int no);
struct cg *asset_cg_load_by_name(const char *name, int *id_out);
bool asset_cg_get_metrics(int no, struct cg_metrics *metrics);
bool asset_cg_get_metrics_by_name(const char *name, struct cg_metrics *metrics);

#endif /* SYSTEM4_ASSET_MANAGER_H */
