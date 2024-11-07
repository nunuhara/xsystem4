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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "debugger.h"

// In a Windows environment, console output should be SJIS-encoded
// (assuming the user is using Japanese non-unicode locale).
#if (defined(_WIN32) || defined(__WIN32))
#define NATIVE_SJIS
#endif

const char *display_sjis0(const char *sjis)
{
#ifdef NATIVE_SJIS
	return sjis;
#else
	static char *utf = NULL;
	free(utf);
	utf = sjis2utf(sjis, 0);
	return utf;
#endif
}

const char *display_sjis1(const char *sjis)
{
#ifdef NATIVE_SJIS
	return sjis;
#else
	static char *utf = NULL;
	free(utf);
	utf = sjis2utf(sjis, 0);
	return utf;
#endif
}

const char *display_sjis2(const char *sjis)
{
#ifdef NATIVE_SJIS
	return sjis;
#else
	static char *utf = NULL;
	free(utf);
	utf = sjis2utf(sjis, 0);
	return utf;
#endif
}

const char *display_utf0(const char *utf)
{
#ifdef NATIVE_SJIS
	static char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

const char *display_utf1(const char *utf)
{
#ifdef NATIVE_SJIS
	static char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

const char *display_utf2(const char *utf)
{
#ifdef NATIVE_SJIS
	static char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

char *unix_path(const char *path)
{
	char *utf = sjis2utf(path, strlen(path));
	for (int i = 0; utf[i]; i++) {
		if (utf[i] == '\\')
			utf[i] = '/';
	}
	return utf;
}

bool is_absolute_path(const char *path)
{
#if (defined(_WIN32) || defined(__WIN32__))
	int i = (isalpha(path[0]) && path[1] == ':') ? 2 : 0;
	return path[i] == '/' || path[i] == '\\';
#else
	return path[0] == '/';
#endif
}

static char *resolve_path(const char *dir, const char *path)
{
	char *utf = unix_path(path);
	if (is_absolute_path(utf))
		return utf;

	char *resolved = xmalloc(strlen(dir) + strlen(utf) + 2);
	strcpy(resolved, dir);
	strcat(resolved, "/");
	strcat(resolved, utf);

	free(utf);
	return resolved;
}

char *gamedir_path(const char *path)
{
	return resolve_path(config.game_dir, path);
}

char *gamedir_path_icase(const char *path)
{
	char *resolved = resolve_path(config.game_dir, path);
	if (!file_exists(resolved)) {
		char *tmp = path_get_icase(resolved);
		free(resolved);
		resolved = tmp;
	}
	return resolved;
}

char *savedir_path(const char *path)
{
	return resolve_path(config.save_dir, path);
}

void get_date(int *year, int *month, int *mday, int *wday)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	*year  = tm->tm_year + 1900;
	*month = tm->tm_mon + 1;
	*mday  = tm->tm_mday;
	*wday  = tm->tm_wday;
}

void get_time(int *hour, int *min, int *sec, int *ms)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	*hour = tm->tm_hour;
	*min  = tm->tm_min;
	*sec  = tm->tm_sec;
	*ms   = ts.tv_nsec / 1000000;
}

void indent_message(int indent, const char *fmt, ...)
{
	for (int i = 0; i < indent; i++)
		putchar('\t');

	va_list ap;
	va_start(ap, fmt);
	sys_vmessage(fmt, ap);
	va_end(ap);
}

void log_message(const char *log, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (dbg_dap)
		dbg_dap_log(log, fmt, ap);
	else
		sys_vmessage(fmt, ap);
	va_end(ap);
}
