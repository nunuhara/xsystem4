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
#include <stdint.h>
#include <stdlib.h>
#include "system4.h"
#include "ald.h"
#include "cg.h"
#include "bmp.h"
#include "pms.h"
#include "qnt.h"
#include "vsp.h"
#include "graphics.h"

bool cg_exists(int no)
{
	return ald[ALDFILE_CG] && ald_data_exists(ald[ALDFILE_CG], no);
}

/*
 * Identify cg format
 *   data: pointer to compressed data
 *   return: cg type
 */
static enum cg_type check_cgformat(uint8_t *data)
{
	if (qnt_checkfmt(data)) {
		return ALCG_QNT;
	} else if (pms256_checkfmt(data)) {
		return ALCG_PMS8;
	} else if (pms64k_checkfmt(data)) {
		return ALCG_PMS16;
	} else if (bmp16m_checkfmt(data)) {
		return ALCG_BMP24;
	} else if (bmp256_checkfmt(data)) {
		return ALCG_BMP8;
	} else if (vsp_checkfmt(data)) {
		return ALCG_VSP;
	}
	WARNING("Unknown CG type");
	return ALCG_UNKNOWN;
}

/*
 * Modify pixel accoding to pallet bank (vsp only)
 *   pic   : pixel to be modifyied.
 *   bank  : pallet bank (use only MSB 4bit)
 *   width : image width
 *   height: image height
 */
//static void set_vspbank(uint8_t *pic, int bank, int width, int height) {
//	int pixels = width * height;
//
//	while (pixels--) {
//		*pic = (*pic & 0x0f) | (uint8_t)bank; pic++;
//	}
//}

/*
 * Free CG data
 *  cg: object to free
 */
void cg_free(struct cg *cg)
{
	if (cg->pic) free(cg->pic);
	if (cg->pal) free(cg->pal);
	if (cg->alpha) free(cg->alpha);
	free(cg);
}

/*
 * Load cg data from file or cache
 *  no: file no ( >= 0)
 *  return: cg object(extracted)
*/
struct cg *cg_load(int no)
{
	struct archive_data *dfile;
	struct cg *cg = NULL;
	int type, possibly_unused size = 0;

	// search in cache
	//if ((cg = (struct cg *)cache_foreach(cacheid, no)))
	//	return cg;

	// read from file
	//if (NULL == (dfile = ald_getdata(DRIFILE_CG, no))) return NULL;
	if (!(dfile = ald_get(ald[ALDFILE_CG], no)))
		return NULL;

	// update load cg counter
//	if (cg_loadCountVar != NULL) {
//		(*(cg_loadCountVar + no + 1))++;
//	}

	// check loaded cg format
	type = check_cgformat(dfile->data);

	// extract cg
	//  size is only pixel data size
	switch(type) {
	case ALCG_VSP:
		sys_message("extracting VSP");
		cg = vsp_extract(dfile->data);
		size = cg->width * cg->height;
		break;
	case ALCG_PMS8:
		sys_message("extracting PMS8");
		cg = pms256_extract(dfile->data);
		size = cg->width * cg->height;
		break;
	case ALCG_PMS16:
		sys_message("extracting PMS16");
		cg = pms64k_extract(dfile->data);
		size = (cg->width * cg->height) * sizeof(uint16_t);
		break;
	case ALCG_BMP8:
		sys_message("extracting BMP8");
		cg = bmp256_extract(dfile->data);
		size = cg->width * cg->height;
		break;
	case ALCG_BMP24:
		sys_message("extracting BMP24");
		cg = bmp16m_extract(dfile->data);
		size = (cg->width * cg->height) * sizeof(uint16_t);
		break;
	case ALCG_QNT:
		sys_message("extracting QNT");
		cg = qnt_extract(dfile->data);
		size = (cg->width * cg->height) * 3;
		break;
	default:
		break;
	}
	// insert to cache
	//if (cg)
	//	cache_insert(cacheid, no, cg, size, NULL);

	// load pallet if not extracted
	// XXXX うむ、こいつらどこで解放するんだ
	switch(type) {
	case ALCG_VSP:
		cg = vsp_getpal(dfile->data);
		break;
	case ALCG_PMS8:
		cg = pms_getpal(dfile->data);
		break;
	case ALCG_BMP8:
		cg = bmp_getpal(dfile->data);
		break;
	default:
		break;
	}

	// ok to free
	ald_free_data(dfile);

	return cg;
}

