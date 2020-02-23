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

#ifndef SYSTEM4_FILE_H
#define SYSTEM4_FILE_H

#include <stddef.h>
#include <stdbool.h>

FILE *file_open_utf8(const char *path, const char *mode);
void *file_read(const char *path, size_t *len_out);
bool file_exists(const char *path);
int mkdir_p(const char *path);

#endif /* SYSTEM4_FILE_H */
