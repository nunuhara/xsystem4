/*
 * bmp.h  extract 8/24 bit BMP cg
 *
 * Copyright (C) 1999 TAJIRI,Yasuhiro  <tajiri@wizard.elec.waseda.ac.jp>
 * rewrited      2000 Masaki Chikam    <masaki-c@is.aist-nara.ac.jp>
 *               2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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
 * @version 1.1     00/09/17 rewrite for changeing interface
 *
*/
/* $Id: bmp.h,v 1.1 2000/09/20 10:33:14 chikama Exp $ */

#ifndef SYSTEM4_BMP_H
#define SYSTEM4_BMP_H

#include <stdbool.h>
#include <stdint.h>
#include "cg.h"

enum bmp_type {
	BMP_OS2 = 1,
	BMP_WIN = 2
};

struct bmp_header {
	int bmpSize;         // header + data size?
	int bmpXW;           // image width
	int bmpYW;           // image height
	int bmpBpp;          // image depth
	int bmpCp;           // pointer to comment ?
	int bmpImgSize;      // image size
	int bmpDp;           // pointer to data
	int bmpPp;           // pointer to pixel
	enum bmp_type bmpTp; // bmp type
};

bool bmp256_checkfmt(uint8_t *data);
struct cg *bmp256_extract(uint8_t *data);
bool bmp16m_checkfmt(uint8_t *data);
struct cg *bmp16m_extract(uint8_t *data);
struct cg *bmp_getpal(uint8_t *data);

#endif /* SYSTEM_BMP_H */
