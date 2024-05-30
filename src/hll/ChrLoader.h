/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_HLL_CHRLOADER_H
#define SYSTEM4_HLL_CHRLOADER_H

#include <stddef.h>

struct archive_data;
struct string;

void chr_loader_init(const char *dir);
bool chr_loader_exists(int id);
struct string *chr_loader_get_name(int id);
struct string *chr_loader_get_string(int id, int index);
int chr_loader_get_id(int id);
int chr_loader_get_status(int id, int type);
int chr_loader_get_blob_handle(int id, int index);
void *chr_loader_get_blob(int blob_handle, size_t *size);
struct archive_data *chr_loader_get_blob_as_archive_data(int blob_handle);

#endif /* SYSTEM4_HLL_CHRLOADER_H */
