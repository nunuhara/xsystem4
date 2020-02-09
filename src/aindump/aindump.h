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

#ifndef AINDUMP_H
#define AINDUMP_H

#include <stdio.h>

struct ain;
struct ain_function;

enum {
	DASM_RAW = 1,
	DASM_NO_STRINGS = 2,
};

void disassemble_ain(FILE *out, struct ain *ain, unsigned int flags);;

void ain_dump_function(FILE *out, struct ain *ain, struct ain_function *f);
void ain_dump_json(FILE *out, struct ain *ain);

#endif /* AINDUMP_H */
