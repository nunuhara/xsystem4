/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

static int ValueEncryption_Get(int code, int backup_code, int xor_key)
{
	return code;
}

static void ValueEncryption_Set(int value, int *code, int *backup_code, int xor_key)
{
	*code = *backup_code = value;
}

HLL_LIBRARY(ValueEncryption,
	    HLL_EXPORT(Get, ValueEncryption_Get),
	    HLL_EXPORT(Set, ValueEncryption_Set)
	    );
