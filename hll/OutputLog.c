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

#include <stdio.h>
#include "hll.h"
#include "../utfsjis.h"

hll_warn_unimplemented(OutputLog, Create)

hll_defun(Output, args) {
	struct string *s = heap[args[1].i].s;
	char *u = sjis2utf(s->text, s->size);
	printf("%s", u);
	free(u);
	hll_return(0);
}

hll_warn_unimplemented(OutputLog, Clear)
hll_warn_unimplemented(OutputLog, Save)
hll_warn_unimplemented(OutputLog, EnableAutoSave)
hll_warn_unimplemented(OutputLog, DisableAutoSave)

hll_deflib(OutputLog) {
	hll_export(Create),
	hll_export(Output),
	hll_export(Clear),
	hll_export(Save),
	hll_export(EnableAutoSave),
	hll_export(DisableAutoSave),
	NULL
};
