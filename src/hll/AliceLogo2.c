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

#include <stdlib.h>

#include "system4/alk.h"
#include "system4/cg.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "hll.h"
#include "xsystem4.h"

HLL_QUIET_UNIMPLEMENTED(1, int, AliceLogo2, Init, void *imainsystem);
HLL_WARN_UNIMPLEMENTED( , void, AliceLogo2, SetWaveNum, int n, int wave);

static void AliceLogo2_Run(int type, int loop_flag)
{
	if (type || loop_flag || !game_sengokurance || !sys4_text_is_gbk) {
		UNIMPLEMENTED("");
		return;
	}

	char *path = gamedir_path_icase("AliceLogo2.alk");
	if (!path)
		return;
	int error = ARCHIVE_SUCCESS;
	struct alk_archive *alk = alk_open(path, MMAP_IF_64BIT, &error);
	free(path);
	if (!alk)
		return;

	// The final frame contains the distributor's logo and localization credit.
	struct cg *cg = cg_load(&alk->ar, 45);
	archive_free(&alk->ar);
	if (!cg)
		return;

	Texture logo;
	gfx_init_texture_with_cg(&logo, cg);
	cg_free(cg);
	Texture *screen = gfx_main_surface();
	gfx_copy_stretch(screen, 0, 0, screen->w, screen->h, &logo, 0, 0, logo.w, logo.h);
	gfx_delete_texture(&logo);
	gfx_swap();
	gfx_android_delay(1500);
}

HLL_LIBRARY(AliceLogo2,
	    HLL_EXPORT(Init, AliceLogo2_Init),
	    HLL_EXPORT(SetWaveNum, AliceLogo2_SetWaveNum),
	    HLL_EXPORT(Run, AliceLogo2_Run));
