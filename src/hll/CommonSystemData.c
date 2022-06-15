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

#include <stdint.h>

#include "system4.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "hll.h"

static struct string *save_path = NULL;

union csd_datum {
	void *p;
	intptr_t i;
	float f;
	struct string *s;
};

static struct hash_table *csd_table(void)
{
	static struct hash_table *table = NULL;
	if (!table) {
		table = ht_create(64);
	}
	return table;
}

static void csd_put_datum(struct string *name, union csd_datum datum)
{
	struct ht_slot *slot = ht_put(csd_table(), name->text, NULL);
	slot->value = datum.p;
}

static bool csd_get_datum(struct string *name, union csd_datum *out)
{
	return _ht_get(csd_table(), name->text, &out->p);
}

static void CommonSystemData_SetFullPathSaveFileName(struct string *filename)
{
	if (save_path)
		free_string(save_path);
	save_path = string_dup(filename);
	NOTICE("SetFullPathSaveFileName(%s)", filename->text);
}

HLL_WARN_UNIMPLEMENTED(false, bool, CommonSystemData, LoadAtStartup);
//static bool CommonSystemData_LoadAtStartup(void);

static bool CommonSystemData_SetDataInt(struct string *name, int value)
{
	csd_put_datum(name, (union csd_datum) { .i = value });
	return true;
}

static bool CommonSystemData_SetDataFloat(struct string *name, float value)
{
	csd_put_datum(name, (union csd_datum) { .f = value });
	return true;
}

static bool CommonSystemData_SetDataString(struct string *name, struct string *value)
{
	union csd_datum datum = { .s = string_dup(value) };
	struct ht_slot *slot = ht_put(csd_table(), name->text, NULL);
	if (slot->value)
		free_string(slot->value);
	slot->value = datum.p;
	return true;
}

static bool CommonSystemData_SetDataBool(struct string *name, bool value)
{
	csd_put_datum(name, (union csd_datum) { .i = value });
	return true;
}

static bool CommonSystemData_GetDataInt(struct string *name, int *value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		*value = datum.i;
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataFloat(struct string *name, float *value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		*value = datum.f;
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataString(struct string *name, struct string **value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		if (*value)
			free_string(*value);
		*value = string_dup(datum.s);
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataBool(struct string *name, bool *value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		*value = datum.i;
		return true;
	}
	return false;
}

HLL_LIBRARY(CommonSystemData,
	    HLL_EXPORT(SetFullPathSaveFileName, CommonSystemData_SetFullPathSaveFileName),
	    HLL_EXPORT(LoadAtStartup, CommonSystemData_LoadAtStartup),
	    HLL_EXPORT(SetDataInt, CommonSystemData_SetDataInt),
	    HLL_EXPORT(SetDataFloat, CommonSystemData_SetDataFloat),
	    HLL_EXPORT(SetDataString, CommonSystemData_SetDataString),
	    HLL_EXPORT(SetDataBool, CommonSystemData_SetDataBool),
	    HLL_EXPORT(GetDataInt, CommonSystemData_GetDataInt),
	    HLL_EXPORT(GetDataFloat, CommonSystemData_GetDataFloat),
	    HLL_EXPORT(GetDataString, CommonSystemData_GetDataString),
	    HLL_EXPORT(GetDataBool, CommonSystemData_GetDataBool));
