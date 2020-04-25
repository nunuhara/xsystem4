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

#ifndef SYSTEM4_ARCHIVE_H
#define SYSTEM4_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum ald_error {
	ARCHIVE_SUCCESS,
	ARCHIVE_FILE_ERROR,
	ARCHIVE_BAD_ARCHIVE_ERROR,
	ARCHIVE_MAX_ERROR
};

enum {
	ARCHIVE_MMAP = 1
};

struct archive {
	bool mmapped;
	struct archive_ops *ops;
};

struct archive_ops {
	bool (*exists)(struct archive *ar, int no);
	bool (*exists_by_name)(struct archive *ar, const char *name);
	struct archive_data *(*get)(struct archive *ar, int no);
	struct archive_data *(*get_by_name)(struct archive *ar, const char *name);
	bool (*load_file)(struct archive_data *data);
	void (*for_each)(struct archive *ar, void (*iter)(struct archive_data *data, void *user), void *user);
	void (*free_data)(struct archive_data *data);
	void (*free)(struct archive *ar);
};

struct archive_data {
	size_t size;
	uint8_t *data;
	char *name;
	int no;
	void *private;
	struct archive *archive;
};

/*
 * Returns a human readable description of an error.
 */
const char *archive_strerror(int error);

/*
 * Check if data exists in an archive.
 */
static inline bool archive_exists(struct archive *ar, int no)
{
	return ar->ops->exists ? ar->ops->exists(ar, no) : false;
}

static inline bool archive_exists_by_name(struct archive *ar, const char *name)
{
	return ar->ops->exists_by_name ? ar->ops->exists_by_name(ar, name) : false;
}

/*
 * Retrieve a file from an archive by ID.
 */
static inline struct archive_data *archive_get(struct archive *ar, int no)
{
	return ar->ops->get ? ar->ops->get(ar, no) : NULL;
}

/*
 * Retrieve a file from an archive by name.
 */
static inline struct archive_data *archive_get_by_name(struct archive *ar, const char *name)
{
	return ar->ops->get_by_name ? ar->ops->get_by_name(ar, name) : NULL;
}

/*
 * Load a file into memory, given an uninitialized descriptor.
 * This should be used in conjunction with archive_for_each.
 */
static inline bool archive_load_file(struct archive_data *data)
{
	return data->archive->ops->load_file ? data->archive->ops->load_file(data) : false;
}

static inline void archive_for_each(struct archive *ar, void (*iter)(struct archive_data *data, void *user), void *user)
{
	if (ar->ops->for_each)
		ar->ops->for_each(ar, iter, user);
}

/*
 * Free an archive_data structure returned by `archive_get`.
 */
static inline void archive_free_data(struct archive_data *data)
{
	data->archive->ops->free_data(data);
}

/*
 * Free an ald_archive structure returned by `ald_open`.
 */
static inline void archive_free(struct archive *ar)
{
	ar->ops->free(ar);
}

#endif /* SYSTEM4_ARCHIVE_H */
