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
#include <turbojpeg.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/cg.h"

bool ajp_checkfmt(const uint8_t *data)
{
	if (data[0] != 'A' || data[1] != 'J' || data[2] != 'P' || data[3] != '\0')
		return false;
	return true;
}

static const uint8_t ajp_key[] = {
	0x5d, 0x91, 0xae, 0x87,
	0x4a, 0x56, 0x51, 0xcd,
	0x83, 0xec, 0x4c, 0x92,
	0xb5, 0xcb, 0x16, 0x34
};

static void ajp_decrypt(uint8_t *data, size_t size)
{
	for (size_t i = 0; i < 16 && i < size; i++) {
		data[i] ^= ajp_key[i];
	}
}

struct ajp_header {
	uint32_t width;
	uint32_t height;
	uint32_t jpeg_off;
	uint32_t jpeg_size;
	uint32_t mask_off;
	uint32_t mask_size;
};

static void ajp_extract_header(uint8_t *data, struct ajp_header *ajp)
{
	ajp->width     = LittleEndian_getDW(data, 12);
	ajp->height    = LittleEndian_getDW(data, 16);
	ajp->jpeg_off  = LittleEndian_getDW(data, 20);
	ajp->jpeg_size = LittleEndian_getDW(data, 24);
	ajp->mask_off  = LittleEndian_getDW(data, 28);
	ajp->mask_size = LittleEndian_getDW(data, 32);
}

static void ajp_init_metrics(struct ajp_header *ajp, struct cg_metrics *dst)
{
	dst->w = ajp->width;
	dst->h = ajp->height;
	dst->bpp = 24;
	dst->has_pixel = ajp->jpeg_size > 0;
	//dst->has_alpha = ajp->mask_size > 0;
	dst->has_alpha = false;
	dst->pixel_pitch = ajp->width * 3;
	dst->alpha_pitch = 1;
}

void ajp_extract(uint8_t *data, size_t size, struct cg *cg)
{
	uint8_t *buf = NULL, *jpeg_data = NULL, *mask_data = NULL;
	int width, height, subsamp;
	struct ajp_header ajp;
	ajp_extract_header(data, &ajp);
	ajp_init_metrics(&ajp, &cg->metrics);

	if (ajp.jpeg_off > size)
		ERROR("AJP JPEG offset invalid");
	if (ajp.jpeg_off + ajp.jpeg_size > size)
		ERROR("AJP JPEG size invalid");
	if (ajp.mask_off > size)
		ERROR("AJP mask offset invalid");
	if (ajp.mask_off + ajp.mask_size > size)
		ERROR("AJP mask size invalid");

	jpeg_data = xmalloc(ajp.jpeg_size);
	mask_data = xmalloc(ajp.mask_size);
	memcpy(jpeg_data, data + ajp.jpeg_off, ajp.jpeg_size);
	memcpy(mask_data, data + ajp.mask_off, ajp.mask_size);
	ajp_decrypt(jpeg_data, ajp.jpeg_size);
	ajp_decrypt(mask_data, ajp.mask_size);

	if (ajp.mask_size) {
		WARNING("AJP mask not implemented");
	}

	tjhandle decompressor = tjInitDecompress();
	tjDecompressHeader2(decompressor, jpeg_data, ajp.jpeg_size, &width, &height, &subsamp);
	if ((uint32_t)width != ajp.width)
		WARNING("AJP width doesn't match JPEG width (%d vs. %u)", width, ajp.width);
	if ((uint32_t)height != ajp.height)
		WARNING("AJP height doesn't match JPEG height (%d vs. %u)", height, ajp.height);

	buf = xmalloc(width * height * 3);
	if (tjDecompress2(decompressor, jpeg_data, ajp.jpeg_size, buf, width, 0, height, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
		WARNING("JPEG decompression failed: %s", tjGetErrorStr());
		free(buf);
		goto cleanup;
	}

	// XXX: add solid alpha mask
	uint8_t *tmp = xmalloc(width * height * 4);
	for (int i = 0; i < width * height; i++) {
		tmp[i*4+0] = buf[i*3+0];
		tmp[i*4+1] = buf[i*3+1];
		tmp[i*4+2] = buf[i*3+2];
		tmp[i*4+3] = 0xFF;
	}
	free(buf);
	buf = tmp;

	cg->type = ALCG_AJP;
	cg->pixels = buf;

cleanup:
	free(jpeg_data);
	free(mask_data);
	tjDestroy(decompressor);
}
