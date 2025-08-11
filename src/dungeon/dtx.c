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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/buffer.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/little_endian.h"

#include "xsystem4.h"
#include "dungeon/dtx.h"
#include "dungeon/mtrand43.h"

#define DTX_VERSION_DTL 0xffffffff

struct dtx *dtx_parse(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (strncmp(buffer_strdata(&r), "DTEX", 4)) {
		WARNING("dtx_parse: not a DTX file");
		return NULL;
	}
	buffer_skip(&r, 4);

	struct dtx *dtx = xcalloc(1, sizeof(struct dtx));
	dtx->version = buffer_read_int32(&r);
	dtx->nr_rows = buffer_read_int32(&r);
	dtx->nr_columns = buffer_read_int32(&r);
	dtx->entries = xcalloc(dtx->nr_rows * dtx->nr_columns, sizeof(struct dtx_entry));
	for (int i = 0; i < dtx->nr_rows; i++) {
		int nr_items = buffer_read_int32(&r);
		if (nr_items != dtx->nr_columns) {
			WARNING("dtx_parse: unexpected number of items");
			dtx_free(dtx);
			return NULL;
		}
		for (int j = 0; j < nr_items; j++) {
			uint32_t size = buffer_read_int32(&r);
			if (!size)
				continue;
			uint8_t *data = xmalloc(size);
			buffer_read_bytes(&r, data, size);
			dtx->entries[i * nr_items + j].size = size;
			dtx->entries[i * nr_items + j].data = data;
		}
	}
	return dtx;
}

void dtx_free(struct dtx *dtx)
{
	int nr_entries = dtx->nr_rows * dtx->nr_columns;
	for (int i = 0; i < nr_entries; i++) {
		if (dtx->entries[i].data)
			free(dtx->entries[i].data);
	}
	free(dtx->entries);
	free(dtx);
}

struct cg *dtx_create_cg(struct dtx *dtx, enum dtx_texture_type type, int index)
{
	int row;
	switch (dtx->version) {
	case 0:
		switch (type) {
		case DTX_WALL:     row = 0; break;
		case DTX_FLOOR:    row = 1; break;
		case DTX_CEILING:  row = 2; break;
		case DTX_STAIRS:   row = 3; break;
		case DTX_DOOR:     row = 4; break;
		case DTX_SKYBOX:   row = 5; break;
		case DTX_LIGHTMAP: row = 6; break;
		default: return NULL;
		}
		break;
	case 1:
		switch (type) {
		case DTX_WALL:     row = 0; break;
		case DTX_FLOOR:    row = 1; break;
		case DTX_CEILING:  row = 2; break;
		case DTX_STAIRS:   row = 3; break;
		case DTX_DOOR:     row = 4; break;
		case DTX_SKYBOX:   row = 5; break;
		case DTX_LIGHTMAP: row = 7; break;
		default: return NULL;
		}
		break;
	case DTX_VERSION_DTL:
		switch (type) {
		case DTX_WALL:     row = 0; break;
		case DTX_FLOOR:    row = 1; break;
		case DTX_CEILING:  row = 2; break;
		case DTX_LIGHTMAP: row = 3; break;
		default: return NULL;
		}
		break;
	}
	if (row >= dtx->nr_rows || index < 0 || index >= dtx->nr_columns)
		return NULL;
	struct dtx_entry *entry = &dtx->entries[row * dtx->nr_columns + index];
	if (!entry->data)
		return NULL;
	return cg_load_buffer(entry->data, entry->size);
}

struct dtx *dtx_load_from_dtl(const char *path, uint32_t seed)
{
	uint8_t *index_buf = NULL;
	struct dtx *dtx = NULL;
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp) {
		WARNING("Failed to open DTL file '%s': %s", display_utf0(path), strerror(errno));
		goto err;
	}
	char header[12];
	if (fread(header, sizeof(header), 1, fp) != 1 || strcmp(header, "DTL"))
		goto err;
	uint32_t version = LittleEndian_getDW((uint8_t*)header, 4);
	uint32_t index_size = LittleEndian_getDW((uint8_t*)header, 8);
	if (version != 0 || index_size <= 1 || index_size > 1000)
		goto err;
	index_buf = xmalloc(index_size * 4);
	if (fread(index_buf, index_size * 4, 1, fp) != 1)
		goto err;

	// Randomly select an index based on the seed.
	struct mtrand43 mt;
	mtrand43_init(&mt, seed);
	for (;;) {
		int index = mtrand43_genrand(&mt) % (index_size - 1) + 1;
		uint32_t offset = LittleEndian_getDW(index_buf, index * 4);
		if (offset) {
			if (fseek(fp, offset, SEEK_SET))
				goto err;
			break;
		}
	}

	dtx = xcalloc(1, sizeof(struct dtx));
	dtx->version = DTX_VERSION_DTL;
	dtx->nr_rows = 4;
	dtx->nr_columns = 46;  // number of lightmap textures
	dtx->entries = xcalloc(dtx->nr_rows * dtx->nr_columns, sizeof(struct dtx_entry));
	uint8_t buf[4];
	for (int row = 0; row < dtx->nr_rows; row++) {
		if (fread(buf, sizeof(buf), 1, fp) != 1)
			goto err;
		uint32_t nr_items = LittleEndian_getDW(buf, 0);
		for (int col = 0; col < nr_items; col++) {
			if (fread(buf, sizeof(buf), 1, fp) != 1)
				goto err;
			uint32_t texture_size = LittleEndian_getDW(buf, 0);
			if (col >= dtx->nr_columns) {
				// skip the rest of the row
				if (fseek(fp, texture_size, SEEK_CUR))
					goto err;
				continue;
			}
			struct dtx_entry *entry = &dtx->entries[row * dtx->nr_columns + col];
			entry->size = texture_size;
			entry->data = xmalloc(texture_size);
			if (fread(entry->data, texture_size, 1, fp) != 1)
				goto err;
		}
	}
	free(index_buf);
	fclose(fp);
	return dtx;

err:
	if (dtx)
		dtx_free(dtx);
	if (index_buf)
		free(index_buf);
	if (fp)
		fclose(fp);
	return NULL;
}
