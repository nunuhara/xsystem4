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
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/ald.h"
#include "system4/cg.h"
#include "system4/webp.h"

bool webp_checkfmt(const uint8_t *data)
{
	if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
		return false;
	if (data[8] != 'W' || data[9] != 'E' || data[10] != 'B' || data[11] != 'P')
		return false;
	return true;
}

static void webp_init_metrics(struct cg_metrics *m)
{
	m->x = 0;
	m->y = 0;
	m->bpp = 24;
	m->has_pixel = true;
	m->has_alpha = true; // FIXME
	m->pixel_pitch = m->w * 3;
	m->alpha_pitch = 1;
}

void webp_get_metrics(uint8_t *data, size_t size, struct cg_metrics *m)
{
	WebPGetInfo(data, size, &m->w, &m->h);
	webp_init_metrics(m);
}

static int get_base_cg(uint8_t *data, size_t size)
{
	if (size < 12)
		return -1;
	if (data[size-12] != 'O' || data[size-11] != 'V' || data[size-10] != 'E' || data[size-9] != 'R')
		return -1;
	int uk = LittleEndian_getDW(data, size-8); // size?
	if (uk != 4)
		WARNING("WEBP: expected 0x4 preceding base CG number, got %d", uk);
	return LittleEndian_getDW(data, size-4);
}

void webp_extract(uint8_t *data, size_t size, struct cg *cg)
{
	cg->pixels = WebPDecodeRGBA(data, size, &cg->metrics.w, &cg->metrics.h);
	webp_init_metrics(&cg->metrics);
	cg->type = ALCG_WEBP;

	int base = get_base_cg(data, size);
	if (base < 0)
		return;

	// FIXME: possible infinite recursion on broken/malicious ALD
	struct cg *base_cg = cg_load(ald[ALDFILE_CG], base-1);
	if (base_cg->metrics.w != cg->metrics.w || base_cg->metrics.h != cg->metrics.h) {
		ERROR("webp base CG dimensions don't match: (%d,%d) / (%d,%d)",
		      base_cg->metrics.w, base_cg->metrics.h, cg->metrics.w, cg->metrics.h);
	}

	// mask alpha color
	uint8_t *pixels = cg->pixels;
	uint8_t *base_pixels = base_cg->pixels;
	for (int row = 0; row < cg->metrics.h; row++) {
		for (int x = 0; x < cg->metrics.w; x++) {
			int p = cg->metrics.w*row*4 + x*4;
			if (pixels[p] == 255 && pixels[p+1] == 0 && pixels[p+2] == 255) {
				pixels[p+0] = base_pixels[p+0];
				pixels[p+1] = base_pixels[p+1];
				pixels[p+2] = base_pixels[p+2];
				pixels[p+3] = base_pixels[p+3];
			}
		}
	}

	cg_free(base_cg);
}

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <webp/encode.h>

void webp_save(const char *path, uint8_t *pixels, int w, int h, bool alpha)
{
	size_t len;
	uint8_t *output;
	FILE *f = fopen(path, "wb");
	if (!f)
		ERROR("fopen failed: %s", strerror(errno));
	if (alpha) {
		len = WebPEncodeLosslessRGBA(pixels, w, h, w*4, &output);
	} else {
		len = WebPEncodeLosslessRGB(pixels, w, h, w*3, &output);
	}
	if (!fwrite(output, len, 1, f))
		ERROR("fwrite failed: %s", strerror(errno));
	fclose(f);
	WebPFree(output);
}
