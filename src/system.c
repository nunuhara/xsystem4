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

#include "system4.h"
#include "system4/utfsjis.h"

mem_alloc void *_xmalloc(size_t size, const char *func)
{
	void *ptr = malloc(size);
	if (!ptr) {
		sys_error("*ERROR*(%s): Out of memory\n", func);
	}
	return ptr;
}

mem_alloc void *_xcalloc(size_t nmemb, size_t size, const char *func)
{
	void *ptr = calloc(nmemb, size);
	if (!ptr) {
		sys_error("*ERROR*(%s): Out of memory\n", func);
	}
	return ptr;
}

mem_alloc void *_xrealloc(void *ptr, size_t size, const char *func)
{
	ptr = realloc(ptr, size);
	if (!ptr) {
		sys_error("*ERROR*(%s): Out of memory\n", func);
	}
	return ptr;
}

mem_alloc char *_xstrdup(const char *in, const char *func)
{
	char *out = strdup(in);
	if (!out) {
		sys_error("*ERROR*(%s): Out of memory\n", func);
	}
	return out;
}

mem_alloc void *xrealloc_array(void *dst, size_t old_nmemb, size_t new_nmemb, size_t size)
{
	dst = xrealloc(dst, new_nmemb * size);
	if (new_nmemb > old_nmemb)
		memset((char*)dst + old_nmemb*size, 0, (new_nmemb - old_nmemb) * size);
	return dst;
}

noreturn void sys_verror(const char *fmt, va_list ap)
{
	vfprintf(stderr, fmt, ap);
	sys_exit(1);
}

noreturn void sys_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sys_verror(fmt, ap);
}

void sys_vwarning(const char *fmt, va_list ap)
{
	vfprintf(stderr, fmt, ap);
}

void sys_warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sys_vwarning(fmt, ap);
	va_end(ap);
}

void sys_message(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

noreturn void sys_exit(int code)
{
	// TODO: cleanup
	exit(code);
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

char *gamedir_path(const char *path)
{
	char *utf = unix_path(path);
	char *gamepath = xmalloc(strlen(config.game_dir) + strlen(utf) + 2);
	strcpy(gamepath, config.game_dir);
	strcat(gamepath, "/");
	strcat(gamepath, utf);

	free(utf);
	return gamepath;
}

char *savedir_path(const char *filename)
{
	char *path = xmalloc(strlen(config.save_dir) + 1 + strlen(filename) + 1);
	strcpy(path, config.save_dir);
	strcat(path, "/");
	strcat(path, filename);
	return path;
}
