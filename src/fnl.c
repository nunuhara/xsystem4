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

#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>
#include "system4.h"
#include "system4/buffer.h"
#include "system4/fnl.h"
#include "system4/utfsjis.h"
#include "file.h"
#include "little_endian.h"

static unsigned int sjis_code_to_index(uint16_t code)
{
	// one byte
	if (code < 0x20)
		return 0;
	if (code < 0x7f)
		return code - 0x20;
	if (code < 0xa1)
		return 0;
	if (code < 0xe0)
		return code - 0x42;

	// two byte
	uint8_t fst = code >> 8;
	uint8_t snd = code & 0xFF;

	if (fst < 0x81)
		return 0;
	if (snd < 0x40 || snd == 0x7f || snd > 0xfc)
		return 0;

	return code - (0x80a2 + (68 * (fst - 0x81)) + (snd > 0x7f ? 1 : 0));
}

struct fnl_glyph *fnl_get_glyph(struct fnl_font_face *font, uint16_t code)
{
	unsigned int index = sjis_code_to_index(code);

	if (index >= font->nr_glyphs)
		index = 0;
	if (!font->glyphs[index].data_size)
		index = 0;

	return &font->glyphs[index];
}

static void fnl_read_glyph(struct buffer *r, uint32_t height, struct fnl_glyph *dst)
{
	dst->real_width = buffer_read_u16(r);
	dst->data_pos = buffer_read_int32(r);
	dst->data_compsize = buffer_read_int32(r);

	if (!dst->data_pos) {
		dst->data_size = 0;
		dst->data = NULL;
		return;
	}

	// uncompress...
	dst->data_size = height * height * 4; // FIXME: determine real bound
	dst->data = xmalloc(dst->data_size);
	int rv = uncompress(dst->data, &dst->data_size, r->buf + dst->data_pos, dst->data_compsize);
	if (rv != Z_OK) {
		if (rv == Z_BUF_ERROR)
			ERROR("uncompress failed: Z_BUF_ERROR");
		else if (rv == Z_MEM_ERROR)
			ERROR("uncompress failed: Z_MEM_ERROR");
		else if (rv == Z_DATA_ERROR)
			ERROR("uncompress failed: Z_DATA_ERROR");
	}
}

static void fnl_read_font_face(struct buffer *r, struct fnl_font_face *dst)
{
	dst->height    = buffer_read_int32(r);
	dst->uk        = buffer_read_int32(r);
	dst->nr_glyphs = buffer_read_int32(r);

	dst->glyphs = xcalloc(dst->nr_glyphs, sizeof(struct fnl_glyph));
	for (size_t i = 0; i < dst->nr_glyphs; i++) {
		fnl_read_glyph(r, dst->height, &dst->glyphs[i]);
	}
}

static void fnl_read_font(struct buffer *r, struct fnl_font *dst)
{
	dst->nr_faces = buffer_read_int32(r);

	dst->faces = xcalloc(dst->nr_faces, sizeof(struct fnl_font_face));
	for (size_t i = 0; i < dst->nr_faces; i++) {
		fnl_read_font_face(r, &dst->faces[i]);
	}
}

struct fnl *fnl_open(const char *path)
{
	size_t filesize;
	struct fnl *fnl = xcalloc(1, sizeof(struct fnl));
	uint8_t *data = file_read(path, &filesize);

	if (data[0] != 'F' || data[1] != 'N' || data[2] != 'A' || data[3] != '\0')
		goto err;

	struct buffer r;
	buffer_init(&r, data, filesize);
	buffer_skip(&r, 4);
	fnl->uk = buffer_read_int32(&r);
	if (fnl->uk != 0)
		WARNING("Unexpected value for fnl->uk: %d", fnl->uk);

	fnl->filesize = buffer_read_int32(&r);
	fnl->data_offset = buffer_read_int32(&r);
	fnl->nr_fonts = buffer_read_int32(&r);

	fnl->fonts = xcalloc(fnl->nr_fonts, sizeof(struct fnl_font));
	for (size_t i = 0; i < fnl->nr_fonts; i++) {
		fnl_read_font(&r, &fnl->fonts[i]);
	}
	free(data);
	return fnl;
err:
	free(fnl);
	free(data);
	return NULL;
}

void fnl_free(struct fnl *fnl)
{
	for (size_t font = 0; font < fnl->nr_fonts; font++) {
		for (size_t face = 0; face < fnl->fonts[font].nr_faces; face++) {
			for (size_t glyph = 0; glyph < fnl->fonts[font].faces[face].nr_glyphs; glyph++) {
				free(fnl->fonts[font].faces[face].glyphs[glyph].data);
			}
			free(fnl->fonts[font].faces[face].glyphs);
		}
		free(fnl->fonts[font].faces);
	}
	free(fnl->fonts);
	free(fnl);
}
