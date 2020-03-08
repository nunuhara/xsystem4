/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * Based on code from xsystem35
 * Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
 *               1998-                           <masaki-c@is.aist-nara.ac.jp>
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
#include <stdbool.h>
#include <string.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/cg.h"
#include "system4/pms.h"

struct pms_header {
	int version;     // PMS data version
	int header_size; // size of header
	int bpp;         // PMS data depth, 8 or 16
	int bpps;        // shadow data depth, if exists
	int sf;          // sprite flag (not used?)
	int bf;          // palette bank
	int x;           // display location x
	int y;           // display location y
	int width;       // image width
	int height;      // image height
	int dp;          // data pointer
	int pp;          // pointer to palette or alpha
	int cp;          // pointer to comment
};

bool pms_checkfmt(const uint8_t *data)
{
	int x, y, w, h;

	if (data[0] != 'P' || data[1] != 'M')
		return false;

	x = LittleEndian_getDW(data, 16);
	y = LittleEndian_getDW(data, 20);
	w = LittleEndian_getDW(data, 24);
	h = LittleEndian_getDW(data, 28);

	if (x < 0 || y < 0 || w < 0 || h < 0)
		return false;

	return true;
}

bool pms8_checkfmt(const uint8_t *data)
{
	return pms_checkfmt(data) && data[6] == 8;
}

bool pms16_checkfmt(const uint8_t *data)
{
	return pms_checkfmt(data) && data[7] == 16;
}

static struct cg_palette *pms_get_palette(const uint8_t *b)
{
	struct cg_palette *pal = xmalloc(sizeof(struct cg_palette));
	for (int i = 0; i < 256; i++) {
		pal->red[i]   = *b++;
		pal->green[i] = *b++;
		pal->blue[i]  = *b++;
	}
	return pal;
}

/* Convert PMS8 image data to bitmap. */
static uint8_t *pms8_extract(struct pms_header *pms, const uint8_t *b)
{
	int n, c0, c1;
	const int scanline = pms->width;
	uint8_t *pic = xmalloc((pms->width+10)*(pms->height+10));

	// for each line...
	for (int y = 0; y < pms->height; y ++) {
		// for each pixel...
		for (int x = 0; x < pms->width; ) {
			int loc = y * scanline + x;
			c0 = *b++;
			// non-command byte: read 1 pixel into buffer
			if (c0 <= 0xf7) {
				pic[loc] = c0;
				x++;
			}
			// copy n+3 pixels from previous line
			else if (c0 == 0xff) {
				n = (*b++) + 3;
				x += n;
				memcpy(pic + loc, pic + loc - scanline, n);
			}
			// copy n+3 pixels from 2 lines previous
			else if (c0 == 0xfe) {
				n = (*b++) + 3;
				x += n;
				memcpy(pic + loc, pic + loc - scanline * 2, n);
			}
			// repeat 1 pixel n+4 times (1-byte RLE)
			else if (c0 == 0xfd) {
				n = (*b++) + 4;
				x += n;
				c0 = *b++;
				memset(pic + loc, c0, n);
			}
			// repeast a sequence of 2 pixels n+3 times (2-byte RLE)
			else if (c0 == 0xfc) {
				n = ((*b++) + 3)  * 2;
				x += n;
				c0 = *b++;
				c1 = *b++;
				for (int i = 0; i < n; i+=2) {
					pic[loc+i]   = c0;
					pic[loc+i+1] = c1;
				}
			}
			// not sure why this exists, probably padding
			else {
				pic[loc] = *b++;
				x++;
			}
		}
	}

	return pic;
}

/* Convert PMS16 image data to bitmap. */
static uint16_t *pms16_extract(struct pms_header *pms, const uint8_t *b)
{
	int n, c0, c1, pc0, pc1;
	const int scanline = pms->width;
	uint16_t *pic = xmalloc(sizeof(uint16_t) * (pms->width+10) * (pms->height+10));

	for (int y = 0; y < pms->height; y++) {
		for (int x = 0; x < pms->width;) {
			int loc = y * scanline + x;
			c0 = *b++;
			// non-command byte: read 1 pixel into buffer
			if (c0 <= 0xf7) {
				c1 = *b++;
				x++;
				pic[loc] = c0 | (c1 << 8);
			}
			// copy n+2 pixels from previous line into buffer
			else if (c0 == 0xff) {
				n = (*b++) + 2;
				x += n;
				for (int i = 0; i < n; i++) {
					pic[loc + i] = pic[loc + i - scanline];
				}
			}
			// copy n+2 pixels from 2 lines previous
			else if (c0 == 0xfe) {
				n = (*b++) + 2;
				x += n;
				for (int i = 0; i < n; i++) {
					pic[loc + i] = pic[loc + i - scanline * 2];
				}
			}
			// repeat a pixel n+3 times (1-byte RLE)
			else if (c0 == 0xfd) {
				n = (*b++) + 3;
				x += n;
				c0 = *b++;
				c1 = *b++;
				pc0 = c0 | (c1 << 8);
				for (int i = 0; i < n; i++) {
					pic[loc + i] = pc0;
				}
			}
			// repeat a sequence of 2 pixels n+2 times (2-byte RLE)
			else if (c0 == 0xfc) {
				n = ((*b++) + 2) * 2;
				x += n;
				c0 = *b++;
				c1 = *b++;
				pc0 = c0 | (c1 << 8);
				c0 = *b++;
				c1 = *b++;
				pc1 = c0 | (c1 << 8);
				for (int i = 0; i < n; i+=2) {
					pic[loc + i    ] = pc0;
					pic[loc + i + 1] = pc1;
				}
			}
			// copy 1 pixel from previous line (left diagonal)
			else if (c0 == 0xfb) {
				x++;
				pic[loc] = pic[loc - scanline - 1];
			}
			// copy 1 pixel from previous line (right diagonal)
			else if (c0 == 0xfa) {
				x++;
				pic[loc] = pic[loc - scanline + 1];
			}
			// this one's a bit tricky, but basically what this does is it
			// combines one byte with the next n bytes
			else if (c0 == 0xf9) {
				n = (*b++) + 1;
				x += n;
				c0 = *b++;
				c1 = *b++;
				pc0 = ((c0 & 0xe0)<<8) + ((c0 & 0x18)<<6) + ((c0 & 0x07)<<2);
				pc1 = ((c1 & 0xc0)<<5) + ((c1 & 0x3c)<<3) + (c1 & 0x03);
				pic[loc] = pc0 + pc1;
				for (int i = 1; i < n; i++) {
					c1 = *b++;
					pc1 = ((c1 & 0xc0)<<5) + ((c1 & 0x3c)<<3) + (c1 & 0x03);
					pic[loc + i] = pc0 | pc1;
				}
			}
			// not sure why this exists, probably padding
			else {
				c0 = *b++;
				c1 = *b++;
				x++;
				pic[loc] = c0 | (c1 << 8);
			}
		}
	}

	return pic;
}

static void pms_read_header(struct pms_header *hdr, const uint8_t *data)
{
	hdr->version = LittleEndian_getW(data, 2);
	hdr->header_size = LittleEndian_getW(data, 4);
	hdr->bpp = data[6];
	hdr->bpps = data[7];
	hdr->sf = data[8];
	hdr->bf = LittleEndian_getW(data, 10);
	hdr->x = LittleEndian_getDW(data, 16);
	hdr->y = LittleEndian_getDW(data, 20);
	hdr->width = LittleEndian_getDW(data, 24);
	hdr->height = LittleEndian_getDW(data, 28);
	hdr->dp = LittleEndian_getDW(data, 32);
	hdr->pp = LittleEndian_getDW(data, 36);
	hdr->cp = LittleEndian_getDW(data, 40);
}

static void pms_init_metrics(struct pms_header *pms, struct cg_metrics *dst)
{
	dst->x = pms->x;
	dst->y = pms->y;
	dst->w = pms->width;
	dst->h = pms->height;
	dst->bpp = pms->bpp;
	dst->has_pixel = pms->dp;
	dst->has_alpha = pms->pp;
	dst->pixel_pitch = pms->width * (pms->bpp / 8);
	dst->alpha_pitch = 1;
}

/* Load a PMS8 CG. */
static void pms8_load(const uint8_t *data, struct pms_header *pms, struct cg *cg)
{
	cg->pal = pms_get_palette(data + pms->pp);

	cg->type = ALCG_PMS8;
	cg->pixels = pms8_extract(pms, data + pms->dp);
	// TODO: convert to RGBA
}

static void pms16_load(const uint8_t *data, struct pms_header *pms, struct cg *cg)
{
	cg->type = ALCG_PMS16;
	cg->pixels = pms16_extract(pms, data + pms->dp);

	if (pms->pp) {
		// TODO: alpha channel
		//uint8_t *alpha = malloc((pms->width + 10) * (pms->height + 10));
		//pms8_extract(pms, alpha, data + pms->pp);
	}
	// TODO: convert to RGBA
}

void pms_extract(const uint8_t *data, size_t size, struct cg *cg)
{
	struct pms_header pms;
	pms_read_header(&pms, data);
	pms_init_metrics(&pms, &cg->metrics);

	if ((size_t)pms.dp > size)
		ERROR("PMS pixel offset out of bounds");
	if ((size_t)pms.pp > size)
		ERROR("PMS palette/alpha offset out of bounds");

	if (pms.bpp == 8)
		pms8_load(data, &pms, cg);
	else if (pms.bpp == 16)
		pms16_load(data, &pms, cg);
	else
		WARNING("Unsupported PMS bpp: %d", pms.bpp);
}

uint8_t *pms_extract_mask(const uint8_t *data, size_t size)
{
	struct pms_header pms;
	pms_read_header(&pms, data);

	if ((size_t)pms.dp > size)
		ERROR("PMS pixel offset out of bounds");
	if ((size_t)pms.pp > size)
		ERROR("PMS palette/alpha offset out of bounds");

	if (pms.bpp != 8)
		ERROR("PMS mask is not 8bpp");

	return pms8_extract(&pms, data + pms.dp);
}
