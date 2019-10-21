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
#include "ain.h"

mem_alloc void *_xmalloc(size_t size, const char *func)
{
	void *ptr = malloc(size);
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

noreturn void sys_error(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	sys_exit(1);
}

void sys_warning(char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void sys_message(char *fmt, ...)
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

int main(int argc, char *argv[])
{
	int err = AIN_SUCCESS;
	struct ain *ain;

	if (argc != 2) {
		printf("wrong number of arguments\n");
		return 1;
	}

	if (!(ain = ain_open(argv[1], &err))) {
		ERROR("%s", ain_strerror(err));
	}

	vm_execute_ain(ain);
	sys_exit(0);
}
