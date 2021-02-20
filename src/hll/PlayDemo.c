/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

HLL_WARN_UNIMPLEMENTED(0, int, PlayDemo, Init, void *imainsystem);
//int PlayDemo_Load(struct string *filename, int ch, int bgm);
//int PlayDemo_Play(void);
//void PlayDemo_SetCheckClick(int flag);


HLL_LIBRARY(PlayDemo,
			HLL_EXPORT(Init, PlayDemo_Init)
			//HLL_EXPORT(Load, PlayDemo_Load),
			//HLL_EXPORT(Play, PlayDemo_Play),
			//HLL_EXPORT(SetCheckClick, PlayDemo_SetCheckClick)
			);
