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

#ifndef SYSTEM4_DEBUGGER_H
#define SYSTEM4_DEBUGGER_H
#ifdef DEBUGGER_ENABLED

#include <stdbool.h>
#include "system4/instructions.h"

extern bool dbg_enabled;

void dbg_init(void);
void dbg_fini(void);
void dbg_repl(void);

enum opcode dbg_handle_breakpoint(unsigned bp_no);

#endif /* DEBUGGER_ENABLED */
#endif /* SYSTEM4_DEBUGGER_H */
