/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

HLL_WARN_UNIMPLEMENTED(0, int, MamanyoSDemo, Init, void *imainsystem);
//int MamanyoSDemo_Run(void);
//int MamanyoSDemo_AddChara(int nNum);

HLL_LIBRARY(MamanyoSDemo,
	    HLL_EXPORT(Init, MamanyoSDemo_Init),
	    HLL_TODO_EXPORT(Run, MamanyoSDemo_Run),
	    HLL_TODO_EXPORT(AddChara, MamanyoSDemo_AddChara)
	    );
