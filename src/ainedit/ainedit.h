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

#ifndef AINEDIT_AINEDIT_H
#define AINEDIT_AINEDIT_H

#include <stdint.h>
#include "system4/ain.h"
#include "system4/instructions.h"

enum {
	ASM_RAW        = 1,
	ASM_NO_STRINGS = 2,
};

// asm.c
void asm_assemble_jam(const char *filename, struct ain *ain, uint32_t flags);
struct instruction *asm_get_instruction(const char *name);

// json.c
void read_declarations(const char *filename, struct ain *ain);

// repack.c
void ain_write(const char *filename, struct ain *ain);

#endif /* AINEDIT_AINEDIT_H */
