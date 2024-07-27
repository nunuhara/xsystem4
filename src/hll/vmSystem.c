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

#include <errno.h>
#include <string.h>
#include <time.h>
#include <SDL.h>

#include "system4/string.h"

#include "hll.h"
#include "vm/page.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmSystem, Init, void *imainsystem);

void vmSystem_CurrentTime(struct page **page)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		WARNING("clock_gettime failed: %s", strerror(errno));
		return;
	}
	struct tm *tm = localtime(&ts.tv_sec);
	if (!tm) {
		WARNING("localtime failed: %s", strerror(errno));
		return;
	}

	union vm_value *date = (*page)->values;
	date[0].i = tm->tm_year + 1900;
	date[1].i = tm->tm_mon + 1;
	date[2].i = tm->tm_mday;
	date[3].i = tm->tm_hour;
	date[4].i = tm->tm_min;
	date[5].i = tm->tm_sec;
	date[6].i = tm->tm_wday;
}

static void vmSystem_Reset(void)
{
	vm_reset();
}

static void vmSystem_Shutdown(void)
{
	vm_exit(0);
}

static int vmSystem_OpenWeb(struct string *url)
{
	return SDL_OpenURL(url->text) == 0;
}

HLL_WARN_UNIMPLEMENTED(0, int, vmSystem, AddURLMenu, struct string *title, struct string *url);

HLL_LIBRARY(vmSystem,
	    HLL_EXPORT(Init, vmSystem_Init),
	    HLL_EXPORT(CurrentTime, vmSystem_CurrentTime),
	    HLL_EXPORT(Reset, vmSystem_Reset),
	    HLL_EXPORT(Shutdown, vmSystem_Shutdown),
	    HLL_EXPORT(OpenWeb, vmSystem_OpenWeb),
	    HLL_EXPORT(AddURLMenu, vmSystem_AddURLMenu)
	    );
