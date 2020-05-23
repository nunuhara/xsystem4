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
#include <string.h>
#include <zlib.h>
#include "little_endian.h"
#include "file.h"
#include "system4.h"
#include "system4/archive.h"
#include "system4/buffer.h"
#include "system4/cg.h"
#include "system4/ajp.h"

enum flat_data_type {
	FLAG_CG  = 2,
	FLAT_ZLIB = 5,
};

struct flat_entry {
	size_t off;
	size_t size;
	int type;
};

struct flat_data {
	struct archive_data super;
	struct flat_entry *entry;
	bool allocated;
};

struct flat_archive {
	struct archive ar;
	bool allocated;
	size_t libl_off;
	size_t talt_off;
	size_t filesize;
	unsigned nr_libl_files;
	struct flat_entry *libl_files;
	unsigned nr_talt_files;
	struct flat_entry *talt_files;
	uint8_t *data;
};

static const char *get_file_extension(int type, const char *data)
{
	switch (type) {
	case FLAG_CG:
		if (!strncmp(data, "AJP", 4))
			return ".ajp";
		if (!strncmp(data, "QNT", 4))
			return ".qnt";
		return ".img";
	case FLAT_ZLIB:
		return ".z.dat";
	default:
		return ".dat";
	}
}

static void flat_free_data(struct archive_data *data)
{
	struct flat_data *flat = (struct flat_data*)data;
	if (flat->allocated)
		free(data->data);
	free(data->name);
	free(data);
}

static void flat_free(struct archive *_ar)
{
	struct flat_archive *ar = (struct flat_archive*)_ar;
	if (ar->allocated)
		free(ar->data);
	free(ar->libl_files);
	free(ar->talt_files);
	free(ar);
}

static struct flat_entry *flat_get_entry(struct flat_archive *ar, unsigned no)
{
	if (no < ar->nr_libl_files)
		return &ar->libl_files[no];

	no -= ar->nr_libl_files;
	if (no < ar->nr_talt_files)
		return &ar->talt_files[no];

	return NULL;
}

static struct archive_data *flat_get(struct archive *_ar, int no)
{
	struct flat_archive *ar = (struct flat_archive*)_ar;
	struct flat_entry *e = flat_get_entry(ar, no);
	struct flat_data *data = xcalloc(1, sizeof(struct flat_data));

	data->super.data = ar->data + e->off;
	data->super.size = e->size;
	data->super.name = xmalloc(256);
	snprintf(data->super.name, 256, "%d%s", no, get_file_extension(e->type, (const char*)ar->data+e->off));
	data->super.no = no;
	data->super.archive = _ar;
	data->entry = e;
	return &data->super;
}

static bool flat_load_file(possibly_unused struct archive_data *data)
{
	struct flat_archive *ar = (struct flat_archive*)data->archive;
	struct flat_data *flatdata = (struct flat_data*)data;
	struct flat_entry *e = flatdata->entry;

	// inflate zlib compressed data
	if (e->type == FLAT_ZLIB && ar->data[e->off+4] == 0x78) {
		unsigned long size = LittleEndian_getDW(ar->data, e->off);
		uint8_t *out = xmalloc(size);
		if (uncompress(out, &size, ar->data + e->off + 4, e->size - 4) != Z_OK) {
			WARNING("uncompress failed");
			free(out);
			return false;
		}
		data->data = out;
		data->size = size;
		flatdata->allocated = true;
	}

	return true;
}

static void flat_for_each(struct archive *_ar, void (*iter)(struct archive_data *data, void *user), void *user)
{
	struct flat_archive *ar = (struct flat_archive*)_ar;
	for (unsigned i = 0; i < ar->nr_libl_files + ar->nr_talt_files; i++) {
		struct archive_data *data = flat_get(_ar, i);
		if (!data)
			continue;
		iter(data, user);
		flat_free_data(data);
	}
}

static struct archive_ops flat_archive_ops = {
	.get = flat_get,
	.load_file = flat_load_file,
	.for_each = flat_for_each,
	.free_data = flat_free_data,
	.free = flat_free,
};

static bool is_image(const char *data)
{
	if (!strncmp(data, "AJP", 4))
		return true;
	if (!strncmp(data, "QNT", 4))
		return true;
	return false;
}

static void read_libl(uint8_t *data, size_t size, struct flat_archive *ar)
{
	struct buffer r;
	buffer_init(&r, data, size);

	buffer_skip(&r, 8);
	ar->nr_libl_files = buffer_read_int32(&r);
	ar->libl_files = xcalloc(ar->nr_libl_files, sizeof(struct flat_entry));
	for (unsigned i = 0; i < ar->nr_libl_files; i++) {
		int len = buffer_read_int32(&r);
		buffer_skip(&r, len);
		buffer_align(&r, 4);

		ar->libl_files[i].type = buffer_read_int32(&r);
		ar->libl_files[i].size = buffer_read_int32(&r);
		ar->libl_files[i].off = ar->libl_off + r.index;

		if (ar->libl_files[i].type == FLAG_CG && !is_image(buffer_strdata(&r))) {
			// XXX: special case: CG usually (not always!) has extra int32
			if (is_image(buffer_strdata(&r)+4)) {
				ar->libl_files[i].off += 4;
				ar->libl_files[i].size -= 4;
				buffer_skip(&r, 4);
			} else {
				WARNING("Couldn't read CG data in LIBL section");
			}
		}

		buffer_skip(&r, ar->libl_files[i].size);
		buffer_align(&r, 4);
	}

	if (r.index != size)
		WARNING("Junk at end of LIBL section");
}

static void read_talt(uint8_t *data, size_t size, struct flat_archive *ar)
{
	if (size < 16)
		return;

	struct buffer r;
	buffer_init(&r, data, size);

	buffer_skip(&r, 8);
	ar->nr_talt_files = buffer_read_int32(&r);
	ar->talt_files = xcalloc(ar->nr_talt_files, sizeof(struct flat_entry));
	for (unsigned i = 0; i < ar->nr_talt_files; i++) {
		ar->talt_files[i].size = buffer_read_int32(&r);
		ar->talt_files[i].off = ar->talt_off + r.index;

		if (strncmp(buffer_strdata(&r), "AJP", 4))
			ERROR("NOT AN AJP FILE");

		buffer_skip(&r, ar->talt_files[i].size);
		buffer_align(&r, 4);

		unsigned nr_meta = buffer_read_int32(&r);
		for (unsigned j = 0; j < nr_meta; j++) {
			unsigned size = buffer_read_int32(&r);
			buffer_skip(&r, size);
			buffer_align(&r, 4);
			buffer_read_int32(&r);
			buffer_read_int32(&r);
			buffer_read_int32(&r);
			buffer_read_int32(&r);
		}
	}

	if (r.index != size)
		WARNING("Junk at end of TALT section");
}

struct archive *flat_open(uint8_t *data, size_t size, int *error)
{
	struct flat_archive *ar = NULL;
	size_t off, libl_off, talt_off;
	struct buffer r;
	buffer_init(&r, data, size);

	if (!strncmp(buffer_strdata(&r), "ELNA", 4)) {
		buffer_seek(&r, 8);
	}

	// FLAT section
	if (strncmp(buffer_strdata(&r), "FLAT", 4))
		goto exit_err;
	buffer_skip(&r, 4);
	off = buffer_read_int32(&r);
	buffer_skip(&r, off);

	if (!strncmp(buffer_strdata(&r), "TMNL", 4)) {
		buffer_skip(&r, 4);
		off = buffer_read_int32(&r);
		buffer_skip(&r, off);
	}

	// MTLC section
	if (strncmp(buffer_strdata(&r), "MTLC", 4))
		goto exit_err;
	buffer_skip(&r, 4);
	off = buffer_read_int32(&r);
	buffer_skip(&r, off);

	// LIBL section
	if (strncmp(buffer_strdata(&r), "LIBL", 4))
		goto exit_err;
	libl_off = r.index;
	buffer_skip(&r, 4);
	off = buffer_read_int32(&r);
	buffer_skip(&r, off);

	// TALT section
	talt_off = r.index;
	if (r.index < size) {
		if (strncmp(buffer_strdata(&r), "TALT", 4))
			goto exit_err;
		buffer_skip(&r, 4);
		off = buffer_read_int32(&r);
	}

	if (r.index + off < size)
		WARNING("Junk at end of FLAT file? %uB/%uB", (unsigned)r.index+off, (unsigned)size);
	else if (r.index + off > size)
		WARNING("FLAT file truncated? %uB/%uB", (unsigned)size, (unsigned)r.index+off);

	ar = xcalloc(1, sizeof(struct flat_archive));
	ar->libl_off = libl_off;
	ar->talt_off = talt_off;
	ar->filesize = size;
	ar->data = data;
	read_libl(data+libl_off, talt_off - libl_off, ar);
	read_talt(data+talt_off, size - talt_off, ar);

	ar->ar.ops = &flat_archive_ops;
	return &ar->ar;
exit_err:
	*error = ARCHIVE_BAD_ARCHIVE_ERROR;
	free(ar);
	return NULL;
}

struct archive *flat_open_file(const char *path, possibly_unused int flags, int *error)
{
	size_t size;
	uint8_t *data = file_read(path, &size);
	if (!data)
		return NULL;

	struct flat_archive *ar = (struct flat_archive*)flat_open(data, size, error);
	if (!ar) {
		free(data);
		return NULL;
	}

	ar->allocated = true;
	return (struct archive*)ar;
}
