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

#ifndef SYSTEM4_H
#define SYSTEM4_H

#include <stdarg.h>
#include <stddef.h>

// TODO: should put in a separate header and guard with feature checks
#define const_pure __attribute__((const))
#define mem_alloc __attribute__((malloc))
#define noreturn _Noreturn
#define possibly_unused __attribute__((unused))

#define ERROR(fmt, ...) \
	sys_error("*ERROR*(%s:%s:%d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define WARNING(fmt, ...) \
	sys_warning("*WARNING*(%s:%s:%d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define NOTICE(fmt, ...) \
	sys_message(fmt "\n", ##__VA_ARGS__)

noreturn void sys_verror(const char *fmt, va_list ap);
noreturn void sys_error(const char *fmt, ...);
void sys_vwarning(const char *fmt, va_list ap);
void sys_warning(const char *fmt, ...);
void sys_message(const char *fmt, ...);

noreturn void sys_exit(int code);

#define xmalloc(size) _xmalloc(size, __func__)
mem_alloc void *_xmalloc(size_t size, const char *func);

#define xcalloc(nmemb, size) _xcalloc(nmemb, size, __func__)
mem_alloc void *_xcalloc(size_t nmemb, size_t size, const char *func);

#define xrealloc(ptr, size) _xrealloc(ptr, size, __func__)
mem_alloc void *_xrealloc(void *ptr, size_t size, const char *func);

#define xstrdup(str) _xstrdup(str, __func__)
mem_alloc char *_xstrdup(const char *in, const char *func);

#define max(a, b)				\
	({					\
		__typeof__ (a) _a = (a);	\
		__typeof__ (b) _b = (b);	\
		_a > _b ? _a : _b;		\
	})

#define min(a, b)				\
	({					\
		__typeof__ (a) _a = (a);	\
		__typeof__ (b) _b = (b);	\
		_a < _b ? _a : _b;		\
	})

struct config {
	char *game_name;
	char *ain_filename;
	char *save_dir;
	int view_width;
	int view_height;
};

struct config config;

#endif /* SYSTEM4_H */
