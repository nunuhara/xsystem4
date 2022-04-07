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

#include <string.h>

#include "system4/ex.h"
#include "system4/string.h"

#include "xsystem4.h"
#include "hll.h"

#include "system4/utfsjis.h"

// TODO: Later versions of this library have a different interface, but use the
//       same function names. Will need to handle this somehow...
//       Probably best to have separate implementations and choose the correct
//       one at startup.

static struct ex *ex;
static struct ex_value **handles = NULL;
static unsigned nr_handles = 0;

static int set_indices(struct ex_value *val, int id)
{
	val->id = id++;

	switch (val->type) {
	case EX_INT:
	case EX_FLOAT:
	case EX_STRING:
		break;
	case EX_TABLE:
		for (unsigned row = 0; row < val->t->nr_rows; row++) {
			for (unsigned col = 0; col < val->t->nr_columns; col++) {
				id = set_indices(&val->t->rows[row][col], id);
			}
		}
		break;
	case EX_LIST:
		for (unsigned i = 0; i < val->list->nr_items; i++) {
			id = set_indices(&val->list->items->value, id);
		}
		break;
	case EX_TREE:
		if (val->tree->is_leaf) {
			id = set_indices(&val->tree->leaf.value, id);
		} else {
			for (unsigned i = 0; i < val->tree->nr_children; i++) {
				id = set_indices(&val->tree->_children[i], id);
			}
		}
		break;
	}

	return id;
}

static void map_handles(struct ex_value *val)
{
	handles[val->id] = val;

	switch (val->type) {
	case EX_INT:
	case EX_FLOAT:
	case EX_STRING:
		break;
	case EX_TABLE:
		for (unsigned row = 0; row < val->t->nr_rows; row++) {
			for (unsigned col = 0; col < val->t->nr_columns; col++) {
				map_handles(&val->t->rows[row][col]);
			}
		}
		break;
	case EX_LIST:
		for (unsigned i = 0; i < val->list->nr_items; i++) {
			map_handles(&val->list->items->value);
		}
		break;
	case EX_TREE:
		if (val->tree->is_leaf) {
			map_handles(&val->tree->leaf.value);
		} else {
			for (unsigned i = 0; i < val->tree->nr_children; i++) {
				map_handles(&val->tree->_children[i]);
			}
		}
		break;
	}
}

static void MainEXFile_ModuleInit(void)
{
	// load .ex file
	if (!config.ex_path || !(ex = ex_read_file(config.ex_path)))
		ERROR("Failed to load .ex file: %s", display_utf0(config.ex_path));

	// assign IDs to each ex_value
	int id = 1;
	for (unsigned i = 0; i <  ex->nr_blocks; i++) {
		id = set_indices(&ex->blocks[i].val, id);
	}

	// create index mapping IDs to ex_values
	nr_handles = id;
	handles = xcalloc(nr_handles, sizeof(struct ex_value*));
	for (unsigned i = 0; i < ex->nr_blocks; i++) {
		map_handles(&ex->blocks[i].val);
	}
}

static void MainEXFile_ModuleFini(void)
{
	ex_free(ex);
	ex = NULL;
	free(handles);
	handles = NULL;
	nr_handles = 0;
}

//static bool MainEXFile_ReloadDebugEXFile(void);

/*
 * Get handle for top-level value.
 */
static int MainEXFile_Handle(struct string *name)
{
	struct ex_value *v = ex_get(ex, name->text);
	return v ? v->id : 0;
}

static struct ex_table *handle_to_table(int handle)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return NULL;
	if (handles[handle]->type != EX_TABLE) {
		WARNING("Value is not a table");
		return NULL;
	}
	return handles[handle]->t;
}

static struct ex_list *handle_to_list(int handle)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return NULL;
	if (handles[handle]->type != EX_LIST) {
		WARNING("Value is not a list");
		return NULL;
	}
	return handles[handle]->list;
}

static int MainEXFile_AHandle(int handle, int index)
{
	struct ex_list *list = handle_to_list(handle);
	if (!list)
		return 0;
	struct ex_value *v = ex_list_get(list, index);
	return v ? v->id : 0;
}

static int MainEXFile_A2Handle(int handle, int row, int col)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	struct ex_value *v = ex_table_get(t, row, col);
	return v ? v->id : 0;
}

static int MainEXFile_IA2Handle(int handle, int key, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	int row = ex_row_at_int_key(t, key);
	if (row < 0)
		return 0;
	int col = ex_col_from_name(t, format_name->text);
	if (col < 0)
		return 0;
	return t->rows[row][col].id;
}

static int MainEXFile_SA2Handle(int handle, struct string *key, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	int row = ex_row_at_string_key(t, key->text);
	if (row < 0)
		return 0;
	int col = ex_col_from_name(t, format_name->text);
	if (col < 0)
		return 0;
	return t->rows[row][col].id;
}

static int MainEXFile_RA2Handle(int handle, int row, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	if (row < 0 || (unsigned)row >= t->nr_rows)
		return 0;
	int col = ex_col_from_name(t, format_name->text);
	if (col < 0)
		return 0;
	return t->rows[row][col].id;
}

static int MainEXFile_Row(int handle)
{
	struct ex_table *t = handle_to_table(handle);
	return t ? t->nr_rows : 0; // FIXME: should this be nr_columns?
}

static int MainEXFile_Col(int handle)
{
	struct ex_table *t = handle_to_table(handle);
	return t ? t->nr_columns : 0; // FIXME: should this be nr_rows?
}

static int MainEXFile_Type(int handle)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return 0;
	return handles[handle]->type;
}

static int MainEXFile_AType(int handle, int index)
{
	struct ex_list *list = handle_to_list(handle);
	if (!list)
		return 0;
	struct ex_value *v = ex_list_get(list, index);
	return v ? v->type : 0;
}

static int MainEXFile_A2Type(int handle, int row, int col)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	struct ex_value *v = ex_table_get(t, row, col);
	return v ? v->type : 0;
}

static int MainEXFile_IA2Type(int handle, int key, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	int row = ex_row_at_int_key(t, key);
	if (row < 0)
		return 0;
	int col = ex_col_from_name(t, format_name->text);
	if (col < 0)
		return 0;
	return t->rows[row][col].type;
}

static int MainEXFile_SA2Type(int handle, struct string *key, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	int row = ex_row_at_string_key(t, key->text);
	if (row < 0)
		return 0;
	int col = ex_col_from_name(t, format_name->text);
	if (col < 0)
		return 0;
	return t->rows[row][col].type;
}

static int MainEXFile_RA2Type(int handle, int row, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	if (row < 0 || (unsigned)row >= t->nr_rows)
		return 0;
	int col = ex_col_from_name(t, format_name->text);
	if (col < 0)
		return 0;
	return t->rows[row][col].type;
}

static bool MainEXFile_Exists(int handle)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return false;
	return true;
}

static bool MainEXFile_AExists(int handle, int index)
{
	struct ex_list *list = handle_to_list(handle);
	if (!list)
		return false;
	return !!ex_list_get(list, index);
}

static bool MainEXFile_A2Exists(int handle, int row, int col)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return false;
	return !!ex_table_get(t, row, col);
}

static bool MainEXFile_IA2Exists(int handle, int key, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	int row = ex_row_at_int_key(t, key);
	if (row < 0)
		return 0;
	return ex_col_from_name(t, format_name->text) >= 0;
}

static bool MainEXFile_SA2Exists(int handle, struct string *key, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	int row = ex_row_at_string_key(t, key->text);
	if (row < 0)
		return 0;
	return ex_col_from_name(t, format_name->text) >= 0;
}

static bool MainEXFile_RA2Exists(int handle, int row, struct string *format_name)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;
	if (row < 0 || (unsigned)row >= t->nr_rows)
		return 0;
	return ex_col_from_name(t, format_name->text) >= 0;
}

static bool MainEXFile_Int(int handle, int *data)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return 0;
	if (handles[handle]->type != EX_INT) {
		WARNING("Value is not an integer");
		return 0;
	}

	*data = handles[handle]->i;
	return 1;
}

static bool MainEXFile_Float(int handle, float *data)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return 0;
	if (handles[handle]->type != EX_FLOAT) {
		WARNING("Value is not a float");
		return 0;
	}

	*data = handles[handle]->f;
	return 1;
}

static bool MainEXFile_String(int handle, struct string **data)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return 0;
	if (handles[handle]->type != EX_STRING) {
		WARNING("Value is not a string");
		return 0;
	}

	*data = string_ref(handles[handle]->s);
	return 1;
}

static bool MainEXFile_AInt(int handle, int index, int *data)
{
	struct ex_list *list = handle_to_list(handle);
	if (!list)
		return 0;

	struct ex_value *v = ex_list_get(list, index);
	if (!v)
		return 0;
	if (v->type != EX_INT) {
		WARNING("Value is not an integer");
		return 0;
	}

	*data = v->i;
	return 1;
}

static bool MainEXFile_AFloat(int handle, int index, float *data)
{
	struct ex_list *list = handle_to_list(handle);
	if (!list)
		return 0;

	struct ex_value *v = ex_list_get(list, index);
	if (!v)
		return 0;
	if (v->type != EX_FLOAT) {
		WARNING("Value is not a float");
		return 0;
	}

	*data = v->f;
	return 1;
}

static bool MainEXFile_AString(int handle, int index, struct string **data)
{
	struct ex_list *list = handle_to_list(handle);
	if (!list)
		return 0;

	struct ex_value *v = ex_list_get(list, index);
	if (!v)
		return 0;
	if (v->type != EX_STRING) {
		WARNING("Value is not a string");
		return 0;
	}

	*data = string_ref(v->s);
	return 1;
}

static bool MainEXFile_A2Int(int handle, int row, int col, int *data)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;

	struct ex_value *v = ex_table_get(t, row, col);
	if (!v)
		return 0;
	if (v->type != EX_INT) {
		WARNING("Value is not an integer");
		return 0;
	}

	*data = v->i;
	return 1;
}

static bool MainEXFile_A2Float(int handle, int row, int col, float *data)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;

	struct ex_value *v = ex_table_get(t, row, col);
	if (!v)
		return 0;
	if (v->type != EX_FLOAT) {
		WARNING("Value is not a float");
		return 0;
	}

	*data = v->f;
	return 1;
}

static bool MainEXFile_A2String(int handle, int row, int col, struct string **data)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return 0;

	struct ex_value *v = ex_table_get(t, row, col);
	if (!v)
		return 0;
	if (v->type != EX_STRING) {
		WARNING("Value is not a string");
		return 0;
	}

	*data = string_ref(v->s);
	return 1;
}

static int MainEXFile_GetRowAtIntKey(int handle, int key)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return -1;
	return ex_row_at_int_key(t, key);
}

static int MainEXFile_GetRowAtStringKey(int handle, struct string *key)
{
	struct ex_table *t = handle_to_table(handle);
	if (!t)
		return -1;
	return ex_row_at_string_key(t, key->text);
}

static bool MainEXFile_IA2Int(int handle, int key, struct string *format_name, int *data)
{
	int v_handle = MainEXFile_IA2Handle(handle, key, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_Int(v_handle, data);
}

static bool MainEXFile_IA2Float(int handle, int key, struct string *format_name, float *data)
{
	int v_handle = MainEXFile_IA2Handle(handle, key, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_Float(v_handle, data);
}

static bool MainEXFile_IA2String(int handle, int key, struct string *format_name, struct string **data)
{
	int v_handle = MainEXFile_IA2Handle(handle, key, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_String(v_handle, data);
}

static bool MainEXFile_SA2Int(int handle, struct string *key, struct string *format_name, int *data)
{
	int v_handle = MainEXFile_SA2Handle(handle, key, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_Int(v_handle, data);
}

static bool MainEXFile_SA2Float(int handle, struct string *key, struct string *format_name, float *data)
{
	int v_handle = MainEXFile_SA2Handle(handle, key, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_Float(v_handle, data);
}

static bool MainEXFile_SA2String(int handle, struct string *key, struct string *format_name, struct string **data)
{
	int v_handle = MainEXFile_SA2Handle(handle, key, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_String(v_handle, data);
}

static bool MainEXFile_RA2Int(int handle, int row, struct string *format_name, int *data)
{
	int v_handle = MainEXFile_RA2Handle(handle, row, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_Int(v_handle, data);
}

static bool MainEXFile_RA2Float(int handle, int row, struct string *format_name, float *data)
{
	int v_handle = MainEXFile_RA2Handle(handle, row, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_Float(v_handle, data);
}

static bool MainEXFile_RA2String(int handle, int row, struct string *format_name, struct string **data)
{
	int v_handle = MainEXFile_RA2Handle(handle, row, format_name);
	if (v_handle <= 0)
		return false;
	return MainEXFile_String(v_handle, data);
}

static int MainEXFile_GetNodeNameCount(struct string *tree_path)
{
	struct ex_value *v = ex_get(ex, tree_path->text);
	if (!v || v->type != EX_TREE || v->tree->is_leaf)
		return 0;

	int count = 0;
	for (unsigned i = 0; i < v->tree->nr_children; i++) {
		if (!v->tree->children[i].is_leaf)
			count++;
	}
	return count;
}

static int MainEXFile_GetEXNameCount(struct string *tree_path)
{
	struct ex_value *v = ex_get(ex, tree_path->text);
	if (!v || v->type != EX_TREE || v->tree->is_leaf)
		return 0;

	int count = 0;
	for (unsigned i = 0; i < v->tree->nr_children; i++) {
		if (v->tree->children[i].is_leaf)
			count++;
	}
	return count;
}

static bool MainEXFile_GetNodeName(struct string *tree_path, int index, struct string **node_name)
{
	struct ex_value *v = ex_get(ex, tree_path->text);
	if (!v || v->type != EX_TREE || v->tree->is_leaf)
		return false;
	if (index < 0 || (unsigned)index >= v->tree->nr_children)
		return false;

	for (unsigned i = 0, count = 0; i < v->tree->nr_children; i++) {
		if (v->tree->children[i].is_leaf)
			continue;
		if (count == (unsigned)index) {
			*node_name = string_ref(v->tree->children[i].name);
			return true;
		}
		count++;
	}

	return false;
}

static bool MainEXFile_GetEXName(struct string *tree_path, int index, struct string **ex_name)
{
	struct ex_value *v = ex_get(ex, tree_path->text);
	if (!v || v->type != EX_TREE || v->tree->is_leaf)
		return false;
	if (index < 0 || (unsigned)index >= v->tree->nr_children)
		return false;

	for (unsigned i = 0, count = 0; i < v->tree->nr_children; i++) {
		if (!v->tree->children[i].is_leaf)
			continue;
		if (count == (unsigned)index) {
			*ex_name = string_ref(v->tree->children[i].leaf.name);
			return true;
		}
		count++;
	}

	return false;
}

HLL_LIBRARY(MainEXFile,
	    HLL_EXPORT(_ModuleInit, MainEXFile_ModuleInit),
	    HLL_EXPORT(_ModuleFini, MainEXFile_ModuleFini),
	    HLL_TODO_EXPORT(ReloadDebugEXFile, MainEXFile_ReloadDebugEXFile),
	    HLL_EXPORT(Handle, MainEXFile_Handle),
	    HLL_EXPORT(AHandle, MainEXFile_AHandle),
	    HLL_EXPORT(A2Handle, MainEXFile_A2Handle),
	    HLL_EXPORT(IA2Handle, MainEXFile_IA2Handle),
	    HLL_EXPORT(SA2Handle, MainEXFile_SA2Handle),
	    HLL_EXPORT(RA2Handle, MainEXFile_RA2Handle),
	    HLL_EXPORT(Row, MainEXFile_Row),
	    HLL_EXPORT(Col, MainEXFile_Col),
	    HLL_EXPORT(Type, MainEXFile_Type),
	    HLL_EXPORT(AType, MainEXFile_AType),
	    HLL_EXPORT(A2Type, MainEXFile_A2Type),
	    HLL_EXPORT(IA2Type, MainEXFile_IA2Type),
	    HLL_EXPORT(SA2Type, MainEXFile_SA2Type),
	    HLL_EXPORT(RA2Type, MainEXFile_RA2Type),
	    HLL_EXPORT(Exists, MainEXFile_Exists),
	    HLL_EXPORT(AExists, MainEXFile_AExists),
	    HLL_EXPORT(A2Exists, MainEXFile_A2Exists),
	    HLL_EXPORT(IA2Exists, MainEXFile_IA2Exists),
	    HLL_EXPORT(SA2Exists, MainEXFile_SA2Exists),
	    HLL_EXPORT(RA2Exists, MainEXFile_RA2Exists),
	    HLL_EXPORT(Int, MainEXFile_Int),
	    HLL_EXPORT(Float, MainEXFile_Float),
	    HLL_EXPORT(String, MainEXFile_String),
	    HLL_EXPORT(AInt, MainEXFile_AInt),
	    HLL_EXPORT(AFloat, MainEXFile_AFloat),
	    HLL_EXPORT(AString, MainEXFile_AString),
	    HLL_EXPORT(A2Int, MainEXFile_A2Int),
	    HLL_EXPORT(A2Float, MainEXFile_A2Float),
	    HLL_EXPORT(A2String, MainEXFile_A2String),
	    HLL_EXPORT(GetRowAtIntKey, MainEXFile_GetRowAtIntKey),
	    HLL_EXPORT(GetRowAtStringKey, MainEXFile_GetRowAtStringKey),
	    HLL_EXPORT(IA2Int, MainEXFile_IA2Int),
	    HLL_EXPORT(IA2Float, MainEXFile_IA2Float),
	    HLL_EXPORT(IA2String, MainEXFile_IA2String),
	    HLL_EXPORT(SA2Int, MainEXFile_SA2Int),
	    HLL_EXPORT(SA2Float, MainEXFile_SA2Float),
	    HLL_EXPORT(SA2String, MainEXFile_SA2String),
	    HLL_EXPORT(RA2Int, MainEXFile_RA2Int),
	    HLL_EXPORT(RA2Float, MainEXFile_RA2Float),
	    HLL_EXPORT(RA2String, MainEXFile_RA2String),
	    HLL_EXPORT(GetNodeNameCount, MainEXFile_GetNodeNameCount),
	    HLL_EXPORT(GetEXNameCount, MainEXFile_GetEXNameCount),
	    HLL_EXPORT(GetNodeName, MainEXFile_GetNodeName),
	    HLL_EXPORT(GetEXName, MainEXFile_GetEXName));
