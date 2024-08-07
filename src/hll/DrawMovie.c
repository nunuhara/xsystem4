/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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
#include "movie.h"
#include "sact.h"

static struct movie_context *mc;

static void DrawMovie_Release(void)
{
	if (mc)
		movie_free(mc);
	mc = NULL;
}

static bool DrawMovie_Load(struct string *filename)
{
	if (mc)
		movie_free(mc);
	mc = movie_load(filename->text);
	return !!mc;
}

static bool DrawMovie_Run(void)
{
	if (!mc)
		return false;
	return movie_play(mc);
}

static bool DrawMovie_Draw(int sprite)
{
	if (!mc)
		return false;

	struct sact_sprite *sp = sact_try_get_sprite(sprite);
	if (!sp)
		return false;

	return movie_draw(mc, sp);
}

static bool DrawMovie_SetVolume(int volume)
{
	if (!mc)
		return false;
	return movie_set_volume(mc, volume);
}

static bool DrawMovie_IsEnd(void)
{
	if (!mc)
		return true;
	return movie_is_end(mc);
}

static int DrawMovie_GetCount(void)
{
	if (!mc)
		return 0;
	return movie_get_position(mc);
}

static void DrawMovie2_SetInnerVolume(int inner_volume)
{
	DrawMovie_SetVolume(inner_volume);
}

static void DrawMovie2_UpdateVolume(void)
{
	// Do nothing, as SetInnerVolume immediately takes effect.
}

HLL_QUIET_UNIMPLEMENTED(, void, DrawMovie3, UpdateVolume, bool IsMuteByInactiveWindow);

HLL_LIBRARY(DrawMovie,
	    HLL_EXPORT(Release, DrawMovie_Release),
	    HLL_EXPORT(Load, DrawMovie_Load),
	    HLL_EXPORT(Run, DrawMovie_Run),
	    HLL_EXPORT(Draw, DrawMovie_Draw),
	    HLL_EXPORT(SetVolume, DrawMovie_SetVolume),
	    HLL_EXPORT(IsEnd, DrawMovie_IsEnd),
	    HLL_EXPORT(GetCount, DrawMovie_GetCount)
	);

HLL_LIBRARY(DrawMovie2,
	    HLL_EXPORT(Release, DrawMovie_Release),
	    HLL_EXPORT(Load, DrawMovie_Load),
	    HLL_EXPORT(Run, DrawMovie_Run),
	    HLL_EXPORT(Draw, DrawMovie_Draw),
	    HLL_EXPORT(SetVolume, DrawMovie_SetVolume),
	    HLL_EXPORT(SetInnerVolume, DrawMovie2_SetInnerVolume),
	    HLL_EXPORT(UpdateVolume, DrawMovie2_UpdateVolume),
	    HLL_EXPORT(IsEnd, DrawMovie_IsEnd),
	    HLL_EXPORT(GetCount, DrawMovie_GetCount)
	);

HLL_LIBRARY(DrawMovie3,
	    HLL_EXPORT(Release, DrawMovie_Release),
	    HLL_EXPORT(Load, DrawMovie_Load),
	    HLL_EXPORT(Run, DrawMovie_Run),
	    HLL_EXPORT(Draw, DrawMovie_Draw),
	    HLL_EXPORT(SetInnerVolume, DrawMovie2_SetInnerVolume),
	    HLL_EXPORT(UpdateVolume, DrawMovie3_UpdateVolume),
	    HLL_EXPORT(IsEnd, DrawMovie_IsEnd),
	    HLL_EXPORT(GetCount, DrawMovie_GetCount)
	);
