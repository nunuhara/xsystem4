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
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/ajp.h"
#include "system4/dcf.h"
#include "system4/qnt.h"
#include "system4/webp.h"

/*
 * Identify cg format
 *   data: pointer to compressed data
 *   return: cg type
 */
enum cg_type cg_check_format(uint8_t *data)
{
	if (qnt_checkfmt(data)) {
		return ALCG_QNT;
	} else if (ajp_checkfmt(data)) {
		return ALCG_AJP;
	} else if (webp_checkfmt(data)) {
		return ALCG_WEBP;
	} else if (dcf_checkfmt(data)) {
		return ALCG_DCF;
	}
	return ALCG_UNKNOWN;
}

bool cg_get_metrics(struct archive *ar, int no, struct cg_metrics *dst)
{
	struct archive_data *dfile;

	if (!(dfile = archive_get(ar, no)))
		return false;

	switch (cg_check_format(dfile->data)) {
	case ALCG_QNT:
		qnt_get_metrics(dfile->data, dst);
		break;
	case ALCG_AJP:
		WARNING("AJP GetMetrics not implemented");
		archive_free_data(dfile);
		return false;
	case ALCG_WEBP:
		webp_get_metrics(dfile->data, dfile->size, dst);
		break;
	case ALCG_DCF:
		dcf_get_metrics(dfile->data, dfile->size, dst);
		break;
	default:
		WARNING("Unknown CG type (CG %d)", no);
		archive_free_data(dfile);
		return false;
	}
	archive_free_data(dfile);
	return true;
}

/*
 * Free CG data
 *  cg: object to free
 */
void cg_free(struct cg *cg)
{
	if (!cg)
		return;
	free(cg->pixels);
	free(cg);
}

struct cg *cg_load_data(struct archive_data *dfile)
{
	int type;
	struct cg *cg = xcalloc(1, sizeof(struct cg));

	// check loaded cg format
	type = cg_check_format(dfile->data);

	// extract cg
	switch(type) {
	case ALCG_QNT:
		qnt_extract(dfile->data, cg);
		break;
	case ALCG_AJP:
		ajp_extract(dfile->data, dfile->size, cg);
		break;
	case ALCG_PNG:
		WARNING("Unimplemented CG type: PNG");
		break;
	case ALCG_WEBP:
		webp_extract(dfile->data, dfile->size, cg, dfile->archive);
		break;
	case ALCG_DCF:
		dcf_extract(dfile->data, dfile->size, cg);
		break;
	default:
		WARNING("Unknown CG type");
		break;
	}

	if (cg->pixels)
		return cg;
	free(cg);
	return NULL;
}

/*
 * Load cg data from file or cache
 *  no: file no ( >= 0)
 *  return: cg object(extracted)
*/
struct cg *cg_load(struct archive *ar, int no)
{
	struct cg *cg;
	struct archive_data *dfile;

	if (!(dfile = archive_get(ar, no))) {
		WARNING("Failed to load CG %d", no);
		return NULL;
	}

	cg = cg_load_data(dfile);
	archive_free_data(dfile);
	return cg;
}
