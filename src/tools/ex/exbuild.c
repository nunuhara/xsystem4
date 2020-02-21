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
#include "system4.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "ast.h"

int ex_dump(FILE *out, struct ex *ex);

int main(int argc, char *argv[])
{
	struct ex *ex = ex_parse(stdin);
	ex_dump(stdout, ex);
	fflush(stdout);
	ex_free(ex);

	return 0;
}
