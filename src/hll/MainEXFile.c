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
#include "system4/ain.h"
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

const struct ex *main_ex_get_file(void)
{
	return ex;
}

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
			id = set_indices(&val->list->items[i].value, id);
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
			map_handles(&val->list->items[i].value);
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

static void rebuild_handles(void)
{
	int id = 1;
	for (unsigned i = 0; i < ex->nr_blocks; i++) {
		id = set_indices(&ex->blocks[i].val, id);
	}

	nr_handles = id;
	free(handles);
	handles = xcalloc(nr_handles, sizeof(struct ex_value*));
	for (unsigned i = 0; i < ex->nr_blocks; i++) {
		map_handles(&ex->blocks[i].val);
	}
}

static void MainEXFile_ModuleInit(void)
{
	// load .ex file
	if (!config.ex_path) {
		WARNING("No .ex file found; initializing empty MainEXFile");
		ex = xcalloc(1, sizeof(struct ex));
		rebuild_handles();
		return;
	}
	if (!(ex = ex_read_file(config.ex_path)))
		ERROR("Failed to load .ex file: %s", display_utf0(config.ex_path));

	rebuild_handles();
}

static bool MainEXFile_AddEXText(struct string *file_path)
{
	if (!ex || !file_path || !file_path->text[0])
		return false;
	char *path = gamedir_path_icase(file_path->text);
	if (!path)
		path = gamedir_path(file_path->text);
	struct ex *add_ex = ex_read_file(path);
	free(path);
	if (!add_ex)
		return false;
	ex_append(ex, add_ex);
	ex_free(add_ex);
	rebuild_handles();
	return true;
}

static void MainEXFile_ModuleFini(void)
{
	ex_free(ex);
	ex = NULL;
	free(handles);
	handles = NULL;
	nr_handles = 0;
}

HLL_WARN_UNIMPLEMENTED(false, bool, MainEXFile, ReloadDebugEXFile, void);

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
	if (handles[handle]->type != EX_TABLE)
		return NULL;
	return handles[handle]->t;
}

static struct ex_list *handle_to_list(int handle)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return NULL;
	if (handles[handle]->type != EX_LIST)
		return NULL;
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
	return t ? t->nr_rows : 0;
}

static int MainEXFile_Col(int handle)
{
	if (handle > 0 && (unsigned)handle < nr_handles &&
	    handles[handle]->type == EX_LIST)
		return handles[handle]->list->nr_items;
	struct ex_table *t = handle_to_table(handle);
	return t ? t->nr_columns : 0;
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
		if (handles[handle]->type != 0)
			WARNING("Value is not an integer");
		return 0;
	}

	*data = handles[handle]->i;
	return 1;
}

static bool ex_value_to_float(struct ex_value *v, float *data)
{
	if (!v || !data)
		return 0;
	if (v->type == EX_FLOAT) {
		*data = v->f;
		return 1;
	}
	if (v->type == EX_INT) {
		*data = (float)v->i;
		return 1;
	}
	if (v->type != 0)
		WARNING("Value is not a float");
	return 0;
}

static bool MainEXFile_Float(int handle, float *data)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return 0;
	return ex_value_to_float(handles[handle], data);
}

static bool MainEXFile_String(int handle, struct string **data)
{
	if (handle <= 0 || (unsigned)handle >= nr_handles)
		return 0;
	if (handles[handle]->type != EX_STRING) {
		if (handles[handle]->type != 0)
			WARNING("Value is not a string");
		return 0;
	}

	if (*data)
		free_string(*data);
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
		if (v->type != 0)
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

	return ex_value_to_float(ex_list_get(list, index), data);
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
		if (v->type != 0)
			WARNING("Value is not a string");
		return 0;
	}

	if (*data)
		free_string(*data);
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
		if (v->type != 0)
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

	return ex_value_to_float(ex_table_get(t, row, col), data);
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
		if (v->type != 0)
			WARNING("Value is not a string");
		return 0;
	}

	if (*data)
		free_string(*data);
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

static struct string *MainEXFile_ChildName(struct string *tree_path, bool leaf, int target, int *count_out)
{
	int count = 0;
	struct string *found = NULL;
	if (!ex || !tree_path) {
		if (count_out)
			*count_out = 0;
		return NULL;
	}
	if (!tree_path->size) {
		for (unsigned i = 0; i < ex->nr_blocks; i++) {
			if ((ex->blocks[i].val.type != EX_TREE) != leaf)
				continue;
			if (count++ == target)
				found = ex->blocks[i].name;
		}
		if (count_out)
			*count_out = count;
		return found;
	}
	struct ex_value *v = ex_get(ex, tree_path->text);
	if (!v || v->type != EX_TREE || v->tree->is_leaf) {
		if (count_out)
			*count_out = 0;
		return NULL;
	}
	for (unsigned i = 0; i < v->tree->nr_children; i++) {
		if (v->tree->children[i].is_leaf != leaf)
			continue;
		if (count++ == target)
			found = leaf ? v->tree->children[i].leaf.name : v->tree->children[i].name;
	}
	if (count_out)
		*count_out = count;
	return found;
}

static int MainEXFile_GetNodeNameCount(struct string *tree_path)
{
	int count = 0;
	MainEXFile_ChildName(tree_path, false, -1, &count);
	return count;
}

static int MainEXFile_GetEXNameCount(struct string *tree_path)
{
	int count = 0;
	MainEXFile_ChildName(tree_path, true, -1, &count);
	return count;
}

static bool MainEXFile_GetNodeName(struct string *tree_path, int index, struct string **node_name)
{
	if (index < 0)
		return false;
	struct string *s = MainEXFile_ChildName(tree_path, false, index, NULL);
	if (!s)
		return false;
	if (*node_name)
		free_string(*node_name);
	*node_name = string_ref(s);
	return true;
}

static bool MainEXFile_GetEXName(struct string *tree_path, int index, struct string **ex_name)
{
	if (index < 0)
		return false;
	struct string *s = MainEXFile_ChildName(tree_path, true, index, NULL);
	if (!s)
		return false;
	if (*ex_name)
		free_string(*ex_name);
	*ex_name = string_ref(s);
	return true;
}

static int MainEXFile_NameHandle(struct string *name)
{
	return name ? MainEXFile_Handle(name) : 0;
}

static int MainEXFile_Row_name(struct string *n) { return MainEXFile_Row(MainEXFile_NameHandle(n)); }
static int MainEXFile_Col_name(struct string *n) { return MainEXFile_Col(MainEXFile_NameHandle(n)); }
static int MainEXFile_Type_name(struct string *n) { return MainEXFile_Type(MainEXFile_NameHandle(n)); }
static int MainEXFile_AType_name(struct string *n, int idx) { return MainEXFile_AType(MainEXFile_NameHandle(n), idx); }
static int MainEXFile_A2Type_name(struct string *n, int r, int c) { return MainEXFile_A2Type(MainEXFile_NameHandle(n), r, c); }
static int MainEXFile_IA2Type_name(struct string *n, int k, struct string *f) { return MainEXFile_IA2Type(MainEXFile_NameHandle(n), k, f); }
static int MainEXFile_SA2Type_name(struct string *n, struct string *k, struct string *f) { return MainEXFile_SA2Type(MainEXFile_NameHandle(n), k, f); }
static int MainEXFile_RA2Type_name(struct string *n, int r, struct string *f) { return MainEXFile_RA2Type(MainEXFile_NameHandle(n), r, f); }
static bool MainEXFile_Exists_name(struct string *n) { return MainEXFile_Exists(MainEXFile_NameHandle(n)); }
static bool MainEXFile_AExists_name(struct string *n, int idx) { return MainEXFile_AExists(MainEXFile_NameHandle(n), idx); }
static bool MainEXFile_A2Exists_name(struct string *n, int r, int c) { return MainEXFile_A2Exists(MainEXFile_NameHandle(n), r, c); }
static bool MainEXFile_IA2Exists_name(struct string *n, int k, struct string *f) { return MainEXFile_IA2Exists(MainEXFile_NameHandle(n), k, f); }
static bool MainEXFile_SA2Exists_name(struct string *n, struct string *k, struct string *f) { return MainEXFile_SA2Exists(MainEXFile_NameHandle(n), k, f); }
static bool MainEXFile_RA2Exists_name(struct string *n, int r, struct string *f) { return MainEXFile_RA2Exists(MainEXFile_NameHandle(n), r, f); }
static bool MainEXFile_Int_name(struct string *n, int *d) { return MainEXFile_Int(MainEXFile_NameHandle(n), d); }
static bool MainEXFile_Float_name(struct string *n, float *d) { return MainEXFile_Float(MainEXFile_NameHandle(n), d); }
static bool MainEXFile_String_name(struct string *n, struct string **d) { return MainEXFile_String(MainEXFile_NameHandle(n), d); }
static bool MainEXFile_AInt_name(struct string *n, int idx, int *d) { return MainEXFile_AInt(MainEXFile_NameHandle(n), idx, d); }
static bool MainEXFile_AFloat_name(struct string *n, int idx, float *d) { return MainEXFile_AFloat(MainEXFile_NameHandle(n), idx, d); }
static bool MainEXFile_AString_name(struct string *n, int idx, struct string **d) { return MainEXFile_AString(MainEXFile_NameHandle(n), idx, d); }
static bool MainEXFile_A2Int_name(struct string *n, int r, int c, int *d) { return MainEXFile_A2Int(MainEXFile_NameHandle(n), r, c, d); }
static bool MainEXFile_A2Float_name(struct string *n, int r, int c, float *d) { return MainEXFile_A2Float(MainEXFile_NameHandle(n), r, c, d); }
static bool MainEXFile_A2String_name(struct string *n, int r, int c, struct string **d) { return MainEXFile_A2String(MainEXFile_NameHandle(n), r, c, d); }
static int MainEXFile_GetRowAtIntKey_name(struct string *n, int k) { return MainEXFile_GetRowAtIntKey(MainEXFile_NameHandle(n), k); }
static int MainEXFile_GetRowAtStringKey_name(struct string *n, struct string *k) { return MainEXFile_GetRowAtStringKey(MainEXFile_NameHandle(n), k); }
static bool MainEXFile_IA2Int_name(struct string *n, int k, struct string *f, int *d) { return MainEXFile_IA2Int(MainEXFile_NameHandle(n), k, f, d); }
static bool MainEXFile_IA2Float_name(struct string *n, int k, struct string *f, float *d) { return MainEXFile_IA2Float(MainEXFile_NameHandle(n), k, f, d); }
static bool MainEXFile_IA2String_name(struct string *n, int k, struct string *f, struct string **d) { return MainEXFile_IA2String(MainEXFile_NameHandle(n), k, f, d); }
static bool MainEXFile_SA2Int_name(struct string *n, struct string *k, struct string *f, int *d) { return MainEXFile_SA2Int(MainEXFile_NameHandle(n), k, f, d); }
static bool MainEXFile_SA2Float_name(struct string *n, struct string *k, struct string *f, float *d) { return MainEXFile_SA2Float(MainEXFile_NameHandle(n), k, f, d); }
static bool MainEXFile_SA2String_name(struct string *n, struct string *k, struct string *f, struct string **d) { return MainEXFile_SA2String(MainEXFile_NameHandle(n), k, f, d); }
static bool MainEXFile_RA2Int_name(struct string *n, int r, struct string *f, int *d) { return MainEXFile_RA2Int(MainEXFile_NameHandle(n), r, f, d); }
static bool MainEXFile_RA2Float_name(struct string *n, int r, struct string *f, float *d) { return MainEXFile_RA2Float(MainEXFile_NameHandle(n), r, f, d); }
static bool MainEXFile_RA2String_name(struct string *n, int r, struct string *f, struct string **d) { return MainEXFile_RA2String(MainEXFile_NameHandle(n), r, f, d); }
static int MainEXFile_GetColAtFormatName_name(struct string *n, struct string *f)
{
	struct ex_table *t = handle_to_table(MainEXFile_NameHandle(n));
	return (t && f) ? ex_col_from_name(t, f->text) : -1;
}

static void MainEXFile_PreLink(void);
void mainex_v12_prelink(struct static_library *library);

HLL_LIBRARY(MainEXFile,
	    HLL_EXPORT(_PreLink, MainEXFile_PreLink),
	    HLL_EXPORT(_ModuleInit, MainEXFile_ModuleInit),
	    HLL_EXPORT(_ModuleFini, MainEXFile_ModuleFini),
	    HLL_EXPORT(ReloadDebugEXFile, MainEXFile_ReloadDebugEXFile),
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
	    HLL_EXPORT(GetEXName, MainEXFile_GetEXName),
	    HLL_TODO_EXPORT(AddEXReader, NULL),
	    HLL_TODO_EXPORT(EraseEXReader, NULL),
	    HLL_TODO_EXPORT(AddEX, NULL),
	    HLL_EXPORT(AddEXText, MainEXFile_AddEXText),
	    HLL_TODO_EXPORT(Save, NULL),
	    HLL_TODO_EXPORT(Load, NULL),
	    HLL_TODO_EXPORT(GetColAtFormatName, NULL),
	    HLL_TODO_EXPORT(GetNodeNameList, NULL),
	    HLL_TODO_EXPORT(GetEXNameList, NULL),
	    HLL_TODO_EXPORT(GetFormatNameList, NULL));

static void MainEXFile_PreLink(void)
{
	int libno = ain_get_library(ain, "MainEXFile");
	if (libno < 0)
		return;
	int fno = ain_get_library_function(ain, libno, "Row");
	if (ain->version >= 12 || ain_get_library_function(ain, libno, "AddEXReader") >= 0
			|| (fno >= 0 && ain->libraries[libno].functions[fno].nr_arguments == 2
				&& ain->libraries[libno].functions[fno].arguments[0].type.data == AIN_STRING)) {
		mainex_v12_prelink(&lib_MainEXFile);
		return;
	}
	if (fno >= 0 && ain->libraries[libno].functions[fno].nr_arguments == 1 &&
	    ain->libraries[libno].functions[fno].arguments[0].type.data == AIN_STRING) {
		const struct static_hll_function functions[] = {
			HLL_EXPORT(Row, MainEXFile_Row_name),
			HLL_EXPORT(Col, MainEXFile_Col_name),
			HLL_EXPORT(Type, MainEXFile_Type_name),
			HLL_EXPORT(AType, MainEXFile_AType_name),
			HLL_EXPORT(A2Type, MainEXFile_A2Type_name),
			HLL_EXPORT(IA2Type, MainEXFile_IA2Type_name),
			HLL_EXPORT(SA2Type, MainEXFile_SA2Type_name),
			HLL_EXPORT(RA2Type, MainEXFile_RA2Type_name),
			HLL_EXPORT(Exists, MainEXFile_Exists_name),
			HLL_EXPORT(AExists, MainEXFile_AExists_name),
			HLL_EXPORT(A2Exists, MainEXFile_A2Exists_name),
			HLL_EXPORT(IA2Exists, MainEXFile_IA2Exists_name),
			HLL_EXPORT(SA2Exists, MainEXFile_SA2Exists_name),
			HLL_EXPORT(RA2Exists, MainEXFile_RA2Exists_name),
			HLL_EXPORT(Int, MainEXFile_Int_name),
			HLL_EXPORT(Float, MainEXFile_Float_name),
			HLL_EXPORT(String, MainEXFile_String_name),
			HLL_EXPORT(AInt, MainEXFile_AInt_name),
			HLL_EXPORT(AFloat, MainEXFile_AFloat_name),
			HLL_EXPORT(AString, MainEXFile_AString_name),
			HLL_EXPORT(A2Int, MainEXFile_A2Int_name),
			HLL_EXPORT(A2Float, MainEXFile_A2Float_name),
			HLL_EXPORT(A2String, MainEXFile_A2String_name),
			HLL_EXPORT(GetRowAtIntKey, MainEXFile_GetRowAtIntKey_name),
			HLL_EXPORT(GetRowAtStringKey, MainEXFile_GetRowAtStringKey_name),
			HLL_EXPORT(GetColAtFormatName, MainEXFile_GetColAtFormatName_name),
			HLL_EXPORT(IA2Int, MainEXFile_IA2Int_name),
			HLL_EXPORT(IA2Float, MainEXFile_IA2Float_name),
			HLL_EXPORT(IA2String, MainEXFile_IA2String_name),
			HLL_EXPORT(SA2Int, MainEXFile_SA2Int_name),
			HLL_EXPORT(SA2Float, MainEXFile_SA2Float_name),
			HLL_EXPORT(SA2String, MainEXFile_SA2String_name),
			HLL_EXPORT(RA2Int, MainEXFile_RA2Int_name),
			HLL_EXPORT(RA2Float, MainEXFile_RA2Float_name),
			HLL_EXPORT(RA2String, MainEXFile_RA2String_name),
		};
		for (unsigned i = 0; i < sizeof(functions) / sizeof(*functions); i++)
			static_library_replace(&lib_MainEXFile, functions[i].name, functions[i].fun);
	}
}
