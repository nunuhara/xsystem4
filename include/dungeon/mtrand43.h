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

#ifndef SYSTEM4_MTRAND43_H
#define SYSTEM4_MTRAND43_H

#include <stddef.h>
#include <stdint.h>

#define MTRAND43_STATE_SIZE 4

// Mersenne Twister engine with N=4, M=3
struct mtrand43 {
	uint32_t st[MTRAND43_STATE_SIZE];
	int i;
};

void mtrand43_init(struct mtrand43 *mt, uint32_t seed);
uint32_t mtrand43_genrand(struct mtrand43 *mt);
float mtrand43_genfloat(struct mtrand43 *mt);

#endif /* SYSTEM4_MTRAND43_H */
