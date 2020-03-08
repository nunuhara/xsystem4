/*
 * qnt.c  extract QNT cg
 *
 * Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
 *               1998-                           <masaki-c@is.aist-nara.ac.jp>
 *               2019 Nunuhara Cabbage           <nunuhara@haniwa.technology>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * @version 1.0     01/07/30 initial version
 * @version 1.1     01/08/10 fix alpha map extraction
 * @version 1.2     01/11/26 zlibの展開バッファが画像サイズよりも大きなものを
 *                           要求するときの workaround
 * @version 1.2     01/11/28 format version 1 に対応
 *                           *(top + 4) is  0 -> version 0
 *                           *(top + 4) is  1 -> version 1
 * @version 1.3     03/02/22 縦横奇数サイズ時にpixel/alphaともおかしかったのをfix
 *                           zlib展開バッファを縦横それぞれ1byteづつ多くとる
 *                           ようにした
 */

#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/cg.h"
#include "system4/qnt.h"

/*
  zlib の展開バッファで、幅×高さ×３に、さらにどれくらい余裕をとるか
   (リクルスで 1164バイトというのがあった)
*/
#define ZLIBBUF_MARGIN 5*1024

/*
 * Get information from header
 *
 *   b: raw data
 *
 *   return: acquired qnt information object
 */
static void extract_header(uint8_t *b, struct qnt_header *qnt)
{
	int rsv0;

	rsv0 = LittleEndian_getDW(b, 4);
	if (rsv0 == 0) {
		qnt->hdr_size = 48;
		qnt->x0  = LittleEndian_getDW(b, 8);
		qnt->y0  = LittleEndian_getDW(b, 12);
		qnt->width  = LittleEndian_getDW(b, 16);
		qnt->height = LittleEndian_getDW(b, 20);
		qnt->bpp    = LittleEndian_getDW(b, 24);
		qnt->rsv    = LittleEndian_getDW(b, 28);
		qnt->pixel_size = LittleEndian_getDW(b, 32);
		qnt->alpha_size = LittleEndian_getDW(b, 36);
	} else {
		qnt->hdr_size = LittleEndian_getDW(b, 8);
		qnt->x0     = LittleEndian_getDW(b, 12);
		qnt->y0     = LittleEndian_getDW(b, 16);
		qnt->width  = LittleEndian_getDW(b, 20);
		qnt->height = LittleEndian_getDW(b, 24);
		qnt->bpp    = LittleEndian_getDW(b, 28);
		qnt->rsv    = LittleEndian_getDW(b, 32);
		qnt->pixel_size = LittleEndian_getDW(b, 36);
		qnt->alpha_size = LittleEndian_getDW(b, 40);
	}
	if (qnt->bpp != 24)
		ERROR("Unsupported bits-per-pixel: %d", qnt->bpp);
}

/*
 * Do extract qnt pixel image
 *
 *   qnt: qnt header information
 *   pic: pixel to be stored
 *   b  : raw data (pointer to pixel)
 */
static void extract_pixel(struct qnt_header *qnt, uint8_t *pic, uint8_t *b)
{
	int i, j, x, y, w, h;
	unsigned long ucbuf = (qnt->width+1) * (qnt->height+1) * 3 + ZLIBBUF_MARGIN;
	uint8_t *raw = malloc(sizeof(uint8_t) * ucbuf);

	if (Z_OK != uncompress(raw, &ucbuf, b, qnt->pixel_size)) {
		WARNING("uncompress failed\n");
		free(raw);
		return;
	}

	w = qnt->width;
	h = qnt->height;

	j = 0;
	for (i = 2; i >= 0; i--) {
		for (y = 0; y < (h -1); y+=2) {
			for (x = 0; x < (w -1); x+=2) {
				pic[( y   *w+x)  *3 +i] = raw[j];
				pic[((y+1)*w+x)  *3 +i] = raw[j+1];
				pic[( y   *w+x+1)*3 +i] = raw[j+2];
				pic[((y+1)*w+x+1)*3 +i] = raw[j+3];
				j+=4;
			}
			if (x != w) {
				pic[( y   *w+x)*3 +i] = raw[j];
				pic[((y+1)*w+x)*3 +i] = raw[j+1];
				j+=4;
			}
		}
		if (y != h) {
			for (x = 0; x < (w -1); x+=2) {
				pic[(y*w+x  )*3+i] = raw[j];
				pic[(y*w+x+1)*3+i] = raw[j+2];
				j+=4;
			}
			if (x != w) {
				pic[( y   *w+x)*3 +i] = raw[j];
				j+=4;
			}
		}
	}

	if (w > 1) {
		for (x = 1; x < w; x++) {
			pic[x*3  ] = pic[(x-1)*3  ] - pic[x*3  ];
			pic[x*3+1] = pic[(x-1)*3+1] - pic[x*3+1];
			pic[x*3+2] = pic[(x-1)*3+2] - pic[x*3+2];
		}
	}

	if (h > 1) {
		for (y = 1; y < h; y++) {
			pic[(y*w)*3  ] = pic[((y-1)*w)*3  ] - pic[(y*w)*3  ];
			pic[(y*w)*3+1] = pic[((y-1)*w)*3+1] - pic[(y*w)*3+1];
			pic[(y*w)*3+2] = pic[((y-1)*w)*3+2] - pic[(y*w)*3+2];

			for (x = 1; x < w; x++) {
				int px, py;
				py = pic[((y-1)*w+x  )*3];
				px = pic[( y   *w+x-1)*3];
				pic[(y*w+x)*3] = ((py+px)>>1) - pic[(y*w+x)*3];
				py = pic[((y-1)*w+x  )*3+1];
				px = pic[( y   *w+x-1)*3+1];
				pic[(y*w+x)*3+1] = ((py+px)>>1) - pic[(y*w+x)*3+1];
				py = pic[((y-1)*w+x  )*3+2];
				px = pic[( y   *w+x-1)*3+2];
				pic[(y*w+x)*3+2] = ((py+px)>>1) - pic[(y*w+x)*3+2];
			}
		}
	}

	free(raw);
}

/*
 * Do extract qnt alpha image
 *
 *   qnt: qnt header information
 *   pic: pixel to be stored
 *   b  : raw data (pointer to alpha pixel)
 */
static void extract_alpha(struct qnt_header *qnt, uint8_t *pic, uint8_t *b)
{
	int i, x, y, w, h;
	unsigned long ucbuf = (qnt->width+1) * (qnt->height+1) + ZLIBBUF_MARGIN;
	uint8_t *raw = malloc(sizeof(uint8_t) * ucbuf);

	if (Z_OK != uncompress(raw, &ucbuf, b, qnt->alpha_size)) {
		WARNING("uncompress failed\n");
		free(raw);
		return;
	}

	w = qnt->width;
	h = qnt->height;

	i = 1;
	if (w > 1) {
		pic[0] = raw[0];
		for (x = 1; x < w; x++) {
			pic[x] = pic[x-1] - raw[i];
			i++;
		}
		if (w%2) i++;
	}

	if (h > 1) {
		for (y = 1; y < h; y++) {
			pic[y*w] = pic[(y-1) *w] - raw[i]; i++;
			for (x = 1; x < w; x++) {
				int pax, pay;
				pax = pic[ y   * w + x -1];
				pay = pic[(y-1)* w + x   ];
				pic[y*w+x] = ((pax+pay) >> 1) - raw[i];
				i++;
			}
			if (w%2) i++;
		}
	}

	free(raw);
}

/*
 * Check data is qnt format cg or not
 *
 *   data: raw data (pointer to data top)
 *
 *   return: true if data is qnt
 */
bool qnt_checkfmt(const uint8_t *data)
{
	if (data[0] != 'Q' || data[1] != 'N' || data[2] != 'T') return false;
	return true;
}

static void qnt_init_metrics(struct qnt_header *qnt, struct cg_metrics *dst)
{
	dst->w = qnt->width;
	dst->h = qnt->height;
	dst->bpp = qnt->bpp;
	dst->has_pixel = qnt->pixel_size > 0;
	dst->has_alpha = qnt->alpha_size > 0;
	dst->pixel_pitch = qnt->width * (qnt->bpp / 8);
	dst->alpha_pitch = 1;
}

bool qnt_get_metrics(uint8_t *data, struct cg_metrics *dst)
{
	struct qnt_header qnt;
	extract_header(data, &qnt);
	qnt_init_metrics(&qnt, dst);
	return true;
}

/*
 * Extract qnt header and pixel
 *
 *   data: raw data (pointer to data top)
 *
 *   return: extracted image data and information
*/
void qnt_extract(uint8_t *data, struct cg *cg)
{
	struct qnt_header qnt;
	extract_header(data, &qnt);
	qnt_init_metrics(&qnt, &cg->metrics);

	uint8_t *pixels = xcalloc(3, (qnt.width+10) * (qnt.height+10));
	extract_pixel(&qnt, pixels, data + qnt.hdr_size);

	cg->type = ALCG_QNT;

	// combine color/alpha data
	uint8_t *alpha = xmalloc((qnt.width+10) * (qnt.height+10));
	uint8_t *tmp = xmalloc((qnt.width+10) * (qnt.height+10) * 4);
	if (qnt.alpha_size) {
		extract_alpha(&qnt, alpha, data + qnt.hdr_size + qnt.pixel_size);
	} else {
		// FIXME: Some CGs don't display correctly unless we add an alpha channel here.
		//        Not sure why. It seems to affect some but not all alpha-less CGs.
		//        E.g. CG#90 (and similar) from the Rance 2 digest version.
		memset(alpha, 0xFF, (qnt.width+10)*(qnt.height+10));
	}
	for (int src_i = 0, dst_i = 0, p = 0; p < qnt.width * qnt.height; p++) {
		tmp[dst_i++] = pixels[src_i++];
		tmp[dst_i++] = pixels[src_i++];
		tmp[dst_i++] = pixels[src_i++];
		tmp[dst_i++] = alpha[p];
	}
	free(alpha);
	free(pixels);
	cg->pixels = tmp;
}
