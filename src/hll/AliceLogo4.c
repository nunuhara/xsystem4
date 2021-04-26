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

#include "hll.h"

HLL_WARN_UNIMPLEMENTED(1, int,  AliceLogo4, Init, void *imainsystem);
HLL_WARN_UNIMPLEMENTED( , void, AliceLogo4, SetWaveNum, int n, int wave);
HLL_WARN_UNIMPLEMENTED( , void, AliceLogo4, Run, int type, int loop_flag);

HLL_LIBRARY(AliceLogo4,
	    HLL_EXPORT(Init, AliceLogo4_Init),
	    HLL_EXPORT(SetWaveNum, AliceLogo4_SetWaveNum),
	    HLL_EXPORT(Run, AliceLogo4_Run));
