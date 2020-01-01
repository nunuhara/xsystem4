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

// int Init()
hll_warn_unimplemented(PlayMovie, Init, 0);
// int Load(string pIFileName)
hll_unimplemented(PlayMovie, Load);
// int Play()
hll_unimplemented(PlayMovie, Play);
// void Stop()
hll_unimplemented(PlayMovie, Stop);
// int IsPlay()
hll_unimplemented(PlayMovie, IsPlay);
// void Release()
hll_unimplemented(PlayMovie, Release);

hll_deflib(PlayMovie) {
	hll_export(Init),
	hll_export(Load),
	hll_export(Play),
	hll_export(Stop),
	hll_export(IsPlay),
	hll_export(Release),
	NULL
};
