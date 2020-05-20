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

#ifndef SYSTEM4_DCF_H
#define SYSTEM4_DCF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cg;
struct cg_metrics;

bool dcf_checkfmt(const uint8_t *data);
void dcf_extract(const uint8_t *data, size_t size, struct cg *cg);
void dcf_get_metrics(const uint8_t *data, size_t size, struct cg_metrics *m);

#endif /* SYSTEM4_DCF_H */
