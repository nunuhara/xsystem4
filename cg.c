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
#include <SDL.h>
#include "system4.h"
#include "ald.h"
#include "cg.h"
#include "qnt.h"
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
	}
	return ALCG_UNKNOWN;
}

bool cg_get_metrics(int no, struct cg_metrics *dst)
{
	struct archive_data *dfile;

	if (!(dfile = ald_get(ald[ALDFILE_CG], no)))
		return false;

	switch (check_cgformat(dfile->data)) {
	case ALCG_QNT:
		qnt_get_metrics(dfile->data, dst);
		break;
	default:
		WARNING("Unknown CG type (CG %d)", no);
		ald_free_data(dfile);
		return false;
	}
	ald_free_data(dfile);
	return true;
}

static void _cg_free(struct cg *cg)
{
	if (!cg)
		return;
	free(cg->pixels);
}

/*
 * Free CG data
 *  cg: object to free
 */
void cg_free(struct cg *cg)
{
	_cg_free(cg);
	free(cg);
}

/*
 * Load cg data from file or cache
 *  no: file no ( >= 0)
 *  return: cg object(extracted)
*/
struct cg *cg_load(int no)
{
	struct cg *cg;
	struct archive_data *dfile;
	int type;

	if (!(dfile = ald_get(ald[ALDFILE_CG], no))) {
		WARNING("Failed to load CG %d", no);
		return NULL;
	}
	cg = xcalloc(1, sizeof(struct cg));

	// check loaded cg format
	type = check_cgformat(dfile->data);

	// extract cg
	switch(type) {
	case ALCG_QNT:
		qnt_extract(dfile->data, cg);
		break;
	case ALCG_AJP:
		WARNING("Unimplemented CG type: AJP");
		break;
	case ALCG_PNG:
		WARNING("Unimplemented CG type: PNG");
		break;
	default:
		WARNING("Unknown CG type (CG %d)", no);
		break;
	}

	// ok to free
	ald_free_data(dfile);

	if (cg->pixels)
		return cg;
	free(cg);
	return NULL;
}
