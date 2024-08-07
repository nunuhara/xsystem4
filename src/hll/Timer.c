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

#include <time.h>

#include "hll.h"
#include "vm.h"

HLL_WARN_UNIMPLEMENTED(, void, Timer, Init, void *imainsystem);

static int Timer_Get(void)
{
	// Returns the same value as system.GetTime().
	return vm_time();
}

static void Timer_Wait(int time)
{
	vm_sleep(time);
}

static void Timer_GetDayTime(int *year, int *month, int *day_of_week, int *day,
							 int *hour, int *minute, int *second, int *milliseconds)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm *tm = localtime(&ts.tv_sec);

	*year         = tm->tm_year + 1900;
	*month        = tm->tm_mon + 1;
	*day_of_week  = tm->tm_wday;
	*day          = tm->tm_mday;
	*hour         = tm->tm_hour;
	*minute       = tm->tm_min;
	*second       = tm->tm_sec;
	*milliseconds = ts.tv_nsec / 1000000;
}

HLL_LIBRARY(Timer,
			HLL_EXPORT(Init, Timer_Init),
			HLL_EXPORT(Get, Timer_Get),
			HLL_EXPORT(Wait, Timer_Wait),
			HLL_EXPORT(GetDayTime, Timer_GetDayTime));
