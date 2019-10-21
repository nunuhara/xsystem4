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

#ifndef SYSTEM4_PAGE_H
#define SYSTEM4_PAGE_H

#include <stdint.h>

union vm_value;
struct ain_variable;

int32_t alloc_page(int nr_vars);
union vm_value *copy_page(union vm_value *src, struct ain_variable *vars, int nr_vars);
void delete_page(union vm_value *page, struct ain_variable *vars, int nr_vars);

#endif /* SYSTEM4_PAGE_H */
