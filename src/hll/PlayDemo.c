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

#include "system4/string.h"
#include "hll.h"

/*
 * Intentionally succeed (return 1) to gracefully skip OP/ED demo playback:
 * returning 0 triggers blocking error popups (system.MsgBox) in Daibanchou.
 */
static int PlayDemo_Init(possibly_unused void *imainsystem)
{
	return 1;
}

static int PlayDemo_Load(possibly_unused struct string *filename, possibly_unused int ch,
		possibly_unused int bgm)
{
	return 1;
}

static int PlayDemo_Play(void)
{
	return 1;
}

static void PlayDemo_SetCheckClick(possibly_unused int flag)
{
}

HLL_LIBRARY(PlayDemo,
	    HLL_EXPORT(Init, PlayDemo_Init),
	    HLL_EXPORT(Load, PlayDemo_Load),
	    HLL_EXPORT(Play, PlayDemo_Play),
	    HLL_EXPORT(SetCheckClick, PlayDemo_SetCheckClick)
	    );
