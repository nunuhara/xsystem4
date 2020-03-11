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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "system4.h"
#include "system4/ini.h"
#include "system4/string.h"
#include "ini_parser.tab.h"

extern FILE *yini_in;

struct ini_entry *ini_parse(const char *path, size_t *nr_entries)
{
	if (!(yini_in = fopen(path, "rb")))
		ERROR("failed to open '%s': %s", path, strerror(errno));
	yini_parse();

	kvec_t(struct ini_entry) entries;
	kv_init(entries);

	// TODO: combine _INI_LIST_ENTRY entries
	for (size_t i = 0; i < kv_size(*yini_entries); i++) {
		kv_push(struct ini_entry, entries, *kv_A(*yini_entries, i));
		free(kv_A(*yini_entries, i));
	}
	kv_destroy(*yini_entries);
	free(yini_entries);

	*nr_entries = kv_size(entries);
	return kv_data(entries);
}

struct ini_entry *ini_make_entry(struct string *name, struct ini_value value)
{
	struct ini_entry *entry = xmalloc(sizeof(struct ini_entry));
	entry->name = name;
	entry->value = value;
	return entry;
}

static void ini_free_value(struct ini_value *value)
{
	switch (value->type) {
	case INI_STRING:
		free_string(value->s);
		break;
	case INI_LIST:
		for (size_t i = 0; i < value->list_size; i++) {
			ini_free_value(&value->list[i]);
		}
		free(value->list);
		break;
	case _INI_LIST_ENTRY:
		ini_free_value(value->_list_value);
		free(value->_list_value);
		break;
	default: break;
	}
}

void ini_free_entry(struct ini_entry *entry)
{
	free_string(entry->name);
	ini_free_value(&entry->value);
}
