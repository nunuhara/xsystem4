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

#ifndef SYSTEM4_DTX_H
#define SYSTEM4_DTX_H

#include <stdint.h>
#include <stddef.h>

struct cg;

enum dtx_texture_type {
	DTX_WALL = 0,
	DTX_FLOOR = 1,
	DTX_CEILING = 2,
	DTX_STAIRS = 3,
	DTX_DOOR = 4,
	DTX_SKYBOX = 5,
};

struct dtx_entry {
	uint32_t size;
	uint8_t *data;
};

struct dtx {
	uint32_t version;  // 0: Rance IV, 1: GALZOO Island
	int nr_rows;
	int nr_columns;
	struct dtx_entry *entries;
};

struct dtx *dtx_parse(uint8_t *data, size_t size);
void dtx_free(struct dtx *dtx);
struct cg *dtx_create_cg(struct dtx *dtx, int type, int index);

#endif /* SYSTEM4_DTX_H */
