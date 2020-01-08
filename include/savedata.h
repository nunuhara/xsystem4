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

#ifndef SYSTEM4_SAVEDATA_H
#define SYSTEM4_SAVEDATA_H

#include "cJSON.h"
#include "vm.h"

cJSON *page_to_json(struct page *page);
cJSON *vm_value_to_json(enum ain_data_type type, union vm_value val);
void json_load_page(struct page *page, cJSON *vars);
union vm_value json_to_vm_value(enum ain_data_type type, enum ain_data_type struct_type, int array_rank, cJSON *json);

int save_json(const char *filename, cJSON *json);
int save_globals(const char *keyname, const char *filename);
int save_group(const char *keyname, const char *filename, const char *group_name, int *n);
int load_globals(const char *keyname, const char *filename, const char *group_name, int *n);

#endif /* SYSTEM4_SAVEDATA_H */
