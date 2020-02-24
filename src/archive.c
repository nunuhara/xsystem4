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

#include "system4.h"
#include "system4/ald.h"

static const char *errtab[ARCHIVE_MAX_ERROR] = {
	[ARCHIVE_SUCCESS]           = "Success",
	[ARCHIVE_FILE_ERROR]        = "Error opening ALD file",
	[ARCHIVE_BAD_ARCHIVE_ERROR] = "Invalid ALD file"
};

/* Get a message describing an error. */
const char *archive_strerror(int error)
{
	if (error < ARCHIVE_MAX_ERROR)
		return errtab[error];
	return "Invalid error number";
}
