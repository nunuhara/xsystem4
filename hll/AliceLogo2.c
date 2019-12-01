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

// int Init(imain_system pIMainSystem)
hll_warn_unimplemented(AliceLogo2, Init, 1);
// void SetWaveNum(int nNum, int nWave)
hll_warn_unimplemented(AliceLogo2, SetWaveNum, 0);
// void Run(int nType, int nLoopFlag)
hll_warn_unimplemented(AliceLogo2, Run, 0);

hll_deflib(AliceLogo2) {
	hll_export(Init),
	hll_export(SetWaveNum),
	hll_export(Run),
	NULL
};
