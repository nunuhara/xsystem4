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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/afa.h"
#include "system4/archive.h"
#include "system4/buffer.h"
#include "system4/string.h"

static bool afa_exists(struct archive *ar, int no);
static struct archive_data *afa_get(struct archive *ar, int no);
static struct archive_data *afa_get_by_name(struct archive *ar, const char *name);
static bool afa_load_file(struct archive_data *data);
static void afa_for_each(struct archive *ar, void (*iter)(struct archive_data *data, void *user), void *user);
static void afa_free_data(struct archive_data *data);
static void afa_free(struct archive *ar);

struct archive_ops afa_archive_ops = {
	.exists = afa_exists,
	.get = afa_get,
	.get_by_name = afa_get_by_name,
	.load_file = afa_load_file,
	.for_each = afa_for_each,
	.free_data = afa_free_data,
	.free = afa_free,
};

static bool afa_exists(struct archive *_ar, int no)
{
	struct afa_archive *ar = (struct afa_archive*)_ar;
	return (uint32_t)no < ar->nr_files;
}

static bool afa_load_file(struct archive_data *data)
{
	if (data->data)
		return true;

	struct afa_archive *ar = (struct afa_archive*)data->archive;
	struct afa_entry *e = &ar->files[data->no];

	if (ar->ar.mmapped) {
		data->data = (uint8_t*)ar->mmap_ptr + ar->data_start + e->off;
		return true;
	}

	FILE *f = fopen(ar->filename, "rb");
	if (!f) {
		WARNING("Failed to open '%s': %s", ar->filename, strerror(errno));
		free(data);
		return false;
	}

	fseek(f, ar->data_start + e->off, SEEK_SET);

	data->data = xmalloc(e->size);
	if (fread(data->data, e->size, 1, f) != e->size) {
		WARNING("Failed to read '%s': %s", ar->filename, strerror(errno));
		free(data->data);
		free(data);
		return false;
	}

	return true;
}

static struct archive_data *afa_get_descriptor(struct archive *_ar, int no)
{
	struct afa_archive *ar = (struct afa_archive*)_ar;
	if ((uint32_t)no >= ar->nr_files)
		return NULL;
	struct afa_entry *e = &ar->files[no];
	struct archive_data *data = xcalloc(1, sizeof(struct archive_data));
	data->size = e->size;
	data->name = e->name->text;
	data->no = no;
	data->archive = _ar;
	return data;
}

static struct archive_data *afa_get(struct archive *_ar, int no)
{
	struct archive_data *data = afa_get_descriptor(_ar, no);
	if (!data)
		return NULL;
	if (!afa_load_file(data)) {
		afa_free_data(data);
		return NULL;
	}
	return data;
}

static struct archive_data *afa_get_by_name(struct archive *_ar, const char *name)
{
	// TODO: index filenames
	struct afa_archive *ar = (struct afa_archive*)_ar;
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		if (!strcmp(name, ar->files[i].name->text)) {
			return afa_get(_ar, i);
		}
	}
	return NULL;
}

static void afa_for_each(struct archive *_ar, void (*iter)(struct archive_data *data, void *user), void *user)
{
	struct afa_archive *ar = (struct afa_archive*)_ar;
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		struct archive_data *data = afa_get_descriptor(_ar, i);
		if (!data)
			continue;
		iter(data, user);
		afa_free_data(data);
	}
}

static void afa_free_data(struct archive_data *data)
{
	if (data->data && !data->archive->mmapped)
		free(data->data);
	free(data);
}

static void afa_free(struct archive *_ar)
{
	struct afa_archive *ar = (struct afa_archive*)_ar;
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		free_string(ar->files[i].name);
	}
	free(ar->filename);
	free(ar->files);
	free(ar);
}

static bool afa_read_header(FILE *f, struct afa_archive *ar, int *error)
{
	char buf[44];
	if (fread(buf, 44, 1, f) != 1) {
		*error = ARCHIVE_FILE_ERROR;
		return false;
	}

	fseek(f, 0, SEEK_END);
	ar->file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (strncmp(buf,    "AFAH",      4) ||
	    strncmp(buf+8,  "AlicArch", 8) ||
	    strncmp(buf+28, "INFO",      4) ||
	    LittleEndian_getDW((uint8_t*)buf, 4) != 0x1c) {
		*error = ARCHIVE_BAD_ARCHIVE_ERROR;
		return false;
	}

	ar->version = LittleEndian_getDW((uint8_t*)buf, 16);
	ar->unknown = LittleEndian_getDW((uint8_t*)buf, 20);
	ar->data_start = LittleEndian_getDW((uint8_t*)buf, 24);
	ar->compressed_size = LittleEndian_getDW((uint8_t*)buf, 32) - 16;
	ar->uncompressed_size = LittleEndian_getDW((uint8_t*)buf, 36);
	ar->nr_files = LittleEndian_getDW((uint8_t*)buf, 40);

	if (ar->data_start+8 >= ar->file_size) {
		*error = ARCHIVE_FILE_ERROR;
		return false;
	}

	fseek(f, ar->data_start, SEEK_SET);
	if (fread(buf, 8, 1, f) != 1) {
		*error = ARCHIVE_FILE_ERROR;
		return false;
	}
	fseek(f, 0, SEEK_SET);

	if (strncmp(buf, "DATA", 4)) {
		*error = ARCHIVE_BAD_ARCHIVE_ERROR;
		return false;
	}

	ar->data_size = LittleEndian_getDW((uint8_t*)buf, 4);
	if (ar->data_start + ar->data_size > ar->file_size) {
		*error = ARCHIVE_BAD_ARCHIVE_ERROR;
		return false;
	}

	return true;
}

static bool afa_read_entry(struct buffer *in, struct afa_archive *ar, struct afa_entry *entry, int *error)
{
	uint32_t name_len = buffer_read_int32(in);
	entry->name = buffer_read_pascal_string(in); // NOTE: length is padded
	entry->name->size = name_len; // fix length

	entry->unknown0 = buffer_read_int32(in);
	entry->unknown1 = buffer_read_int32(in);
	if (ar->version == 1)
		entry->unknown2 = buffer_read_int32(in);

	entry->off = buffer_read_int32(in);
	entry->size = buffer_read_int32(in);
	return true;
}

static bool afa_read_file_table(FILE *f, struct afa_archive *ar, int *error)
{
	uint8_t *buf = xmalloc(ar->compressed_size);
	uint8_t *table = xmalloc(ar->uncompressed_size);
	fseek(f, 44, SEEK_SET);
	if (fread(buf, ar->compressed_size, 1, f) != 1) {
		*error = ARCHIVE_FILE_ERROR;
		goto exit_err;
	}

	unsigned long uncompressed_size = ar->uncompressed_size;
	if (uncompress(table, &uncompressed_size, buf, ar->compressed_size) != Z_OK) {
		*error = ARCHIVE_BAD_ARCHIVE_ERROR;
		goto exit_err;
	}

	struct buffer r;
	buffer_init(&r, table, ar->uncompressed_size);
	ar->files = xcalloc(ar->nr_files, sizeof(struct afa_entry));
	for (uint32_t i = 0; i < ar->nr_files; i++) {
		if (!afa_read_entry(&r, ar, &ar->files[i], error)) {
			free(ar->files);
			goto exit_err;
		}
	}

	free(buf);
	free(table);
	return true;
exit_err:
	free(buf);
	free(table);
	return false;
}

struct afa_archive *afa_open(const char *file, int flags, int *error)
{
#ifdef _WIN32
	flags &= ~ARCHIVE_MMAP;
#endif
	FILE *fp;
	struct afa_archive *ar = xcalloc(1, sizeof(struct afa_archive));

	if (!(fp = fopen(file, "rb"))) {
		WARNING("fopen failed: %s", strerror(errno));
		*error = ARCHIVE_FILE_ERROR;
		goto exit_err;
	}
	if (!afa_read_header(fp, ar, error)) {
		WARNING("afa_read_header failed");
		fclose(fp);
		goto exit_err;
	}
	if (!afa_read_file_table(fp, ar, error)) {
		WARNING("afa_read_file_table failed");
		fclose(fp);
		goto exit_err;
	}
	if (fclose(fp)) {
		WARNING("fclose failed: %s", strerror(errno));
		*error = ARCHIVE_FILE_ERROR;
		goto exit_err;
	}

	if (flags & ARCHIVE_MMAP) {
		int fd = open(file, O_RDONLY);
		if (fd < 0) {
			WARNING("open failed: %s", strerror(errno));
			*error = ARCHIVE_FILE_ERROR;
			goto exit_err;
		}
		ar->mmap_ptr = mmap(0, ar->file_size, PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		if (ar->mmap_ptr == MAP_FAILED) {
			WARNING("mmap failed: %s", strerror(errno));
			*error = ARCHIVE_FILE_ERROR;
			goto exit_err;
		}
		ar->ar.mmapped = true;
	}
	ar->filename = strdup(file);
	ar->ar.ops = &afa_archive_ops;
	return ar;
exit_err:
	free(ar);
	return NULL;
}
