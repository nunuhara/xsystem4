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

#include "xsystem4.h"
#include "input.h"
#include "hll.h"

static int ADVSYS_Init(void)
{
	return 1;
}

static int ADVSYS_KEY_IsDown(int key)
{
	return key_is_down(key);
}

static int ADVSYS_SYSTEM_IsResetOnce(void)
{
	// TODO
	return 0;
}

HLL_LIBRARY(ADVSYS,
	    HLL_EXPORT(Init, ADVSYS_Init),
	    HLL_EXPORT(KEY_IsDown, ADVSYS_KEY_IsDown),
	    HLL_EXPORT(SYSTEM_IsResetOnce, ADVSYS_SYSTEM_IsResetOnce));
