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

#ifndef SYSTEM4_FNL_H
#define SYSTEM4_FNL_H

#include <stdint.h>

struct fnl {
	uint32_t filesize;
	uint32_t uk;
	uint32_t data_offset;
	uint32_t nr_fonts;
	struct fnl_font *fonts;
};

struct fnl_font {
	uint32_t nr_faces;
	struct fnl_font_face *faces;
};

struct fnl_font_face {
	uint32_t height;
	uint32_t uk;
	uint32_t nr_glyphs;
	struct fnl_glyph *glyphs;
};

struct fnl_glyph {
	uint16_t real_width;
	uint32_t data_pos;
	uint32_t data_compsize;
	// uncompressed data
	unsigned long data_size;
	uint8_t *data;
};

struct fnl *fnl_open(const char *path);
void fnl_free(struct fnl *fnl);

struct fnl_glyph *fnl_get_glyph(struct fnl_font_face *font, uint16_t code);

#endif /* SYSTEM4_FNL_H */
