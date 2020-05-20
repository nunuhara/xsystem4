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
#include "little_endian.h"
#include "system4.h"
#include "system4/cg.h"
#include "system4/dcf.h"
#include "system4/qnt.h"

static const uint8_t *dcf_get_qnt(const uint8_t *data)
{
	if (data[0] != 'd' && data[1] != 'c' && data[2] != 'f' && data[3] != 0x20)
		return NULL;

	int h2 = 8 + LittleEndian_getDW(data, 4);
	if (data[h2] != 'd' || data[h2+1] != 'f' || data[h2+2] != 'd' || data[h2+3] != 'l')
		return NULL;

	int h3 = h2 + 8 + LittleEndian_getDW(data, h2+4);
	if (data[h3] != 'd' || data[h3+1] != 'c' || data[h3+2] != 'g' || data[h3+3] != 'd')
		return NULL;

	if (data[h3+8] != 'Q' || data[h3+9] != 'N' || data[h3+10] != 'T' || data[h3+11] != '\0')
		return NULL;

	return data + h3 + 8;
}

bool dcf_checkfmt(const uint8_t *data)
{
	return !strncmp((const char*)data, "dcf\x20", 4);
}

void dcf_get_metrics(const uint8_t *data, possibly_unused size_t size, struct cg_metrics *m)
{
	if (!(data = dcf_get_qnt(data)))
		return;
	qnt_get_metrics(data, m);
}

void dcf_extract(const uint8_t *data, possibly_unused size_t size, struct cg *cg)
{
	if (!(data = dcf_get_qnt(data)))
		return;
	qnt_extract(data, cg);
}
