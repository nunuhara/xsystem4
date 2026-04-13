/* Copyright (C) 2026 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include "hll.h"

static void Discord_Update(void)
{

}

static void Discord_UpdateActivity(struct string **pIText, struct string **pIText2)
{

}

HLL_LIBRARY(Discord,
	    HLL_EXPORT(Update, Discord_Update),
	    HLL_EXPORT(UpdateActivity, Discord_UpdateActivity)
	    );
