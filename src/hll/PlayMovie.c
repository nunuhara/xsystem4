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

#include "system4/string.h"

#include "hll.h"
#include "movie.h"
#include "gfx/gfx.h"

static struct movie_context *mc;

static int PlayMovie_Init(void)
{
	return 1;
}

static int PlayMovie_Load(struct string *filename)
{
	if (mc)
		movie_free(mc);
	mc = movie_load(filename->text);
	return !!mc;
}

static int PlayMovie_Play(void)
{
	if (!mc)
		return false;
	return movie_play(mc);
}

HLL_WARN_UNIMPLEMENTED(, void, PlayMovie, Stop, void);

// PlayMovie is used like this:
//
//   PlayMovie.Load(filename);
//   PlayMovie.Play();
//   while (PlayMovie.IsPlay()) {
//       system.Peek();
//       system.Sleep(1);
//   }
//
// SACT.Update is not called inside the loop, so we do rendering in IsPlay().
static int PlayMovie_IsPlay(void)
{
	if (!mc)
		return false;
	if (!movie_draw(mc, NULL))
		return false;
	return !movie_is_end(mc);
}

static void PlayMovie_Release(void)
{
	if (mc)
		movie_free(mc);
	mc = NULL;
}

HLL_LIBRARY(PlayMovie,
	    HLL_EXPORT(Init, PlayMovie_Init),
	    HLL_EXPORT(Load, PlayMovie_Load),
	    HLL_EXPORT(Play, PlayMovie_Play),
	    HLL_EXPORT(Stop, PlayMovie_Stop),
	    HLL_EXPORT(IsPlay, PlayMovie_IsPlay),
	    HLL_EXPORT(Release, PlayMovie_Release)
	);
