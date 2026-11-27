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

// enable private VM interface
#define VM_PRIVATE

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "cJSON.h"

#include "system4.h"
#include "system4/file.h"
#include "system4/instructions.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modifier.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

struct interned_ain_type {
	enum ain_data_type leaf_data;
	int struct_type;
	int depth;
	struct ain_type type;
	struct interned_ain_type *next;
};

static struct interned_ain_type *interned_types = NULL;

static void unwrap_v12_array_type(const struct ain_type *type, enum ain_data_type *leaf_data, int *struct_type, int *depth)
{
	int d = 0;
	const struct ain_type *t = type;
	while (t && t->data == AIN_ARRAY && t->array_type) {
		d++;
		t = t->array_type;
	}
	*leaf_data = t ? t->data : AIN_VOID;
	*struct_type = t ? t->struc : -1;
	*depth = d;
}

static const struct ain_type *intern_v12_type(enum ain_data_type leaf_data, int struct_type, int depth)
{
	for (struct interned_ain_type *it = interned_types; it; it = it->next) {
		if (it->leaf_data == leaf_data &&
		    it->struct_type == struct_type &&
		    it->depth == depth) {
			return &it->type;
		}
	}
	const struct ain_type *inner = depth > 0
		? intern_v12_type(leaf_data, struct_type, depth - 1)
		: NULL;
	struct interned_ain_type *node = xcalloc(1, sizeof(struct interned_ain_type));
	node->leaf_data = leaf_data;
	node->struct_type = struct_type;
	node->depth = depth;
	node->type = (struct ain_type) {
		.data = depth > 0 ? AIN_ARRAY : leaf_data,
		.struc = depth > 0 ? -1 : struct_type,
		.rank = depth > 0 ? 1 : 0,
		.array_type = (struct ain_type *)inner,
	};
	node->next = interned_types;
	interned_types = node;
	return &node->type;
}

static struct ain_type make_v12_element_type(enum ain_data_type leaf_data, int struct_type, int depth)
{
	if (depth <= 0) {
		return (struct ain_type) {
			.data = leaf_data,
			.struc = struct_type,
			.rank = 0,
			.array_type = NULL,
		};
	}
	if (depth > 32)
		VM_ERROR("Invalid array element depth %d", depth);
	return *intern_v12_type(leaf_data, struct_type, depth);
}

static enum ain_data_type normalize_v12_array_leaf_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_ARRAY_TYPE:
	case AIN_REF_ARRAY_TYPE:
		return array_type(type);
	default:
		return type;
	}
}

static const char *array_leaf_type_name(enum ain_data_type leaf_data, int struct_type)
{
	switch (leaf_data) {
	case AIN_VOID:
	case AIN_INT:
	case AIN_REF_INT:
	case AIN_FLOAT:
	case AIN_REF_FLOAT:
	case AIN_STRING:
	case AIN_REF_STRING:
	case AIN_BOOL:
	case AIN_REF_BOOL:
	case AIN_LONG_INT:
	case AIN_REF_LONG_INT:
		return "";
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		return (struct_type >= 0 && struct_type < ain->nr_structures)
			? ain->structures[struct_type].name : "";
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_REF_ENUM:
		return (struct_type >= 0 && struct_type < ain->nr_enums)
			? ain->enums[struct_type].name : "";
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		return (struct_type >= 0 && struct_type < ain->nr_delegates)
			? ain->delegates[struct_type].name : "";
	case AIN_FUNC_TYPE:
	case AIN_REF_FUNC_TYPE:
		return (struct_type >= 0 && struct_type < ain->nr_function_types)
			? ain->function_types[struct_type].name : "";
	default:
		return (struct_type >= 0 && struct_type < ain->nr_structures)
			? ain->structures[struct_type].name : "";
	}
}

static bool v14_array_needs_x4_type(const struct ain_type *t)
{
	while (t && t->data == AIN_ARRAY && t->array_type)
		t = t->array_type;
	if (!t)
		return false;
	return t->array_type != NULL || t->data == AIN_WRAP || t->data == AIN_OPTION
		|| t->data == AIN_REF_ARRAY || t->data == AIN_REF_OPTION;
}

struct interned_x4_type {
	char *spec;
	struct ain_type type;
	struct interned_x4_type *next;
};

static struct interned_x4_type *interned_x4_types = NULL;

static void append_str(struct string **out, const char *s);

static void encode_x4_type_node(struct string **out, const struct ain_type *t)
{
	const char *sym = "";
	switch (t->data) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_REF_ENUM:
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
	case AIN_FUNC_TYPE:
	case AIN_REF_FUNC_TYPE:
		sym = array_leaf_type_name(t->data, t->struc);
		break;
	default:
		break;
	}
	size_t sym_len = strlen(sym);
	char hdr[64];
	snprintf(hdr, sizeof(hdr), "%d:%zu:", (int)t->data, sym_len);
	append_str(out, hdr);
	if (sym_len > 0)
		string_append_cstr(out, sym, sym_len);
	if (t->array_type) {
		append_str(out, ":1:");
		encode_x4_type_node(out, t->array_type);
	} else {
		append_str(out, ":0");
	}
}

static char *encode_x4_type(const struct ain_type *t)
{
	struct string *out = cstr_to_string("@x4t:");
	encode_x4_type_node(&out, t);
	char *res = xstrdup(out->text);
	free_string(out);
	return res;
}

static int resolve_array_leaf_symbol(enum ain_data_type leaf_data, struct rsave_symbol *sym);

static const struct ain_type *intern_x4_type_node(const char **p, int depth)
{
	if (depth > 16)
		return NULL;
	const char *start = *p;
	char *end = NULL;
	int data = (int)strtol(*p, &end, 10);
	if (!end || *end != ':')
		return NULL;
	*p = end + 1;
	size_t sym_len = (size_t)strtoul(*p, &end, 10);
	if (!end || *end != ':')
		return NULL;
	*p = end + 1;
	size_t rem = strlen(*p);
	if (rem < 2 || sym_len > rem - 2 || (*p)[sym_len] != ':')
		return NULL;
	char *sym_name = xmalloc(sym_len + 1);
	memcpy(sym_name, *p, sym_len);
	sym_name[sym_len] = '\0';
	*p += sym_len + 1;

	int has_child = (int)strtol(*p, &end, 10);
	if (!end) {
		free(sym_name);
		return NULL;
	}
	*p = end;
	const struct ain_type *child = NULL;
	if (has_child) {
		if (**p != ':') {
			free(sym_name);
			return NULL;
		}
		(*p)++;
		child = intern_x4_type_node(p, depth + 1);
		if (!child) {
			free(sym_name);
			return NULL;
		}
	}

	size_t spec_len = (size_t)(*p - start);
	for (struct interned_x4_type *it = interned_x4_types; it; it = it->next) {
		if (strlen(it->spec) == spec_len && !memcmp(it->spec, start, spec_len)) {
			free(sym_name);
			return &it->type;
		}
	}

	int struc = -1;
	if (sym_len > 0) {
		struct rsave_symbol rsym = { .name = sym_name, .id = -1 };
		struc = resolve_array_leaf_symbol((enum ain_data_type)data, &rsym);
	} else if (child) {
		struc = child->struc;
	}
	free(sym_name);

	struct interned_x4_type *node = xcalloc(1, sizeof(*node));
	node->spec = xmalloc(spec_len + 1);
	memcpy(node->spec, start, spec_len);
	node->spec[spec_len] = '\0';
	node->type = (struct ain_type) {
		.data = (enum ain_data_type)data,
		.struc = struc,
		.rank = child ? 1 : 0,
		.array_type = (struct ain_type *)child,
	};
	node->next = interned_x4_types;
	interned_x4_types = node;
	return &node->type;
}

static bool decode_x4_type(const char *s, struct ain_type *out)
{
	if (!s || strncmp(s, "@x4t:", 5) != 0)
		return false;
	const char *p = s + 5;
	const struct ain_type *t = intern_x4_type_node(&p, 0);
	if (!t)
		return false;
	*out = *t;
	return true;
}

static int resolve_array_leaf_symbol(enum ain_data_type leaf_data, struct rsave_symbol *sym)
{
	if (!sym->name)
		return sym->id;
	if (!sym->name[0])
		return -1;
	int id = -1;
	switch (leaf_data) {
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_REF_ENUM:
		id = save_find_enum(sym->name);
		break;
	case AIN_DELEGATE:
	case AIN_REF_DELEGATE:
		id = save_find_delegate(sym->name);
		break;
	case AIN_FUNC_TYPE:
	case AIN_REF_FUNC_TYPE:
		id = save_find_functype(sym->name);
		break;
	default:
		break;
	}
	if (id >= 0)
		return id;
	return save_find_struct(sym->name);
}

static void append_str(struct string **out, const char *s)
{
	string_append_cstr(out, s, strlen(s));
}

static void format_rsave_type(struct string **out, const struct ain_type *t)
{
	switch (t->data) {
	case AIN_VOID:            append_str(out, "void"); break;
	case AIN_INT:             append_str(out, "int"); break;
	case AIN_FLOAT:           append_str(out, "float"); break;
	case AIN_STRING:          append_str(out, "string"); break;
	case AIN_STRUCT:
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_structures) ? ain->structures[t->struc].name : "struct");
		break;
	case AIN_REF_INT:         append_str(out, "ref int"); break;
	case AIN_REF_FLOAT:       append_str(out, "ref float"); break;
	case AIN_REF_STRING:      append_str(out, "ref string"); break;
	case AIN_REF_STRUCT:
		append_str(out, "ref ");
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_structures) ? ain->structures[t->struc].name : "struct");
		break;
	case AIN_FUNC_TYPE:
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_function_types) ? ain->function_types[t->struc].name : "functype");
		break;
	case AIN_REF_FUNC_TYPE:
		append_str(out, "ref ");
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_function_types) ? ain->function_types[t->struc].name : "functype");
		break;
	case AIN_BOOL:            append_str(out, "bool"); break;
	case AIN_REF_BOOL:        append_str(out, "ref bool"); break;
	case AIN_LONG_INT:        append_str(out, "lint"); break;
	case AIN_REF_LONG_INT:    append_str(out, "ref lint"); break;
	case AIN_DELEGATE:
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_delegates) ? ain->delegates[t->struc].name : "delegate");
		break;
	case AIN_REF_DELEGATE:
		append_str(out, "ref ");
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_delegates) ? ain->delegates[t->struc].name : "delegate");
		break;
	case AIN_ARRAY:
		append_str(out, "array@");
		if (t->array_type)
			format_rsave_type(out, t->array_type);
		break;
	case AIN_REF_ARRAY:
		append_str(out, "ref array@");
		if (t->array_type)
			format_rsave_type(out, t->array_type);
		break;
	case AIN_WRAP:
		append_str(out, "wrap@");
		if (t->array_type)
			format_rsave_type(out, t->array_type);
		break;
	case AIN_OPTION:
		append_str(out, "option@");
		if (t->array_type)
			format_rsave_type(out, t->array_type);
		break;
	case AIN_REF_OPTION:
		append_str(out, "ref option@");
		if (t->array_type)
			format_rsave_type(out, t->array_type);
		break;
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_structures) ? ain->structures[t->struc].name : "interface");
		break;
	case AIN_ENUM:
	case AIN_ENUM2:
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_enums) ? ain->enums[t->struc].name : "enum");
		break;
	case AIN_REF_ENUM:
		append_str(out, "ref ");
		append_str(out, (t->struc >= 0 && t->struc < ain->nr_enums) ? ain->enums[t->struc].name : "enum");
		break;
	default:
		append_str(out, "int");
		break;
	}
}

static char *format_rsave_function_name(int fno)
{
	struct ain_function *f = &ain->functions[fno];
	if (ain->version < 11)
		return xstrdup(f->name);
	struct string *out = cstr_to_string(f->name);
	append_str(&out, "(");
	bool first = true;
	for (int i = 0; i < f->nr_args; i++) {
		if (f->vars[i].type.data == AIN_VOID)
			continue;
		if (!first)
			append_str(&out, ", ");
		first = false;
		format_rsave_type(&out, &f->vars[i].type);
	}
	append_str(&out, ")");
	char *res = xstrdup(out->text);
	free_string(out);
	return res;
}

static bool function_matches_criteria(int fno, const char *full_name, bool has_parens, int crc, bool check_crc, int nr_slots)
{
	if (fno < 0 || fno >= ain->nr_functions)
		return false;
	if (fno > 0 && ain->functions[fno].address == 0)
		return false;
	if (check_crc && ain->functions[fno].crc != crc)
		return false;
	if (nr_slots >= 0 && ain->functions[fno].nr_vars != nr_slots)
		return false;
	if (has_parens && ain->version >= 11) {
		char *formatted = format_rsave_function_name(fno);
		bool eq = save_symbols_equal(formatted, full_name);
		free(formatted);
		if (!eq)
			return false;
	}
	return true;
}

static char *extract_rsave_base_name(const char *name, bool *has_parens_out)
{
	*has_parens_out = false;
	size_t len = strlen(name);
	if (len >= 2 && name[len - 1] == ')') {
		int depth = 0;
		size_t open = len;
		for (size_t i = len; i > 0; i--) {
			if (name[i - 1] == ')') {
				depth++;
			} else if (name[i - 1] == '(') {
				depth--;
				if (depth == 0) {
					open = i - 1;
					break;
				}
			}
		}
		if (open < len) {
			*has_parens_out = true;
			char *base = xmalloc(open + 1);
			memcpy(base, name, open);
			base[open] = '\0';
			return base;
		}
	}
	return NULL;
}

static int resolve_rsave_function_ex(const char *name, int sym_id, int crc, bool check_crc, int nr_slots, bool allow_similarity)
{
	if (!name)
		return -1;
	int fno = ain_get_function(ain, (char *)name);
	if (fno >= 0 && (!check_crc || ain->functions[fno].crc == crc) && (nr_slots < 0 || ain->functions[fno].nr_vars == nr_slots))
		return fno;

	bool has_parens = false;
	char *base_alloc = extract_rsave_base_name(name, &has_parens);
	if (base_alloc) {
		size_t open = strlen(base_alloc);
		int base_fno = ain_get_function(ain, base_alloc);
		if (base_fno >= 0) {
			if (function_matches_criteria(base_fno, name, true, crc, check_crc, nr_slots)) {
				free(base_alloc);
				return base_fno;
			}
			char *overload = xmalloc(open + 16);
			for (int idx = 1; idx < 256; idx++) {
				snprintf(overload, open + 16, "%s#%d", base_alloc, idx);
				int cand = ain_get_function(ain, overload);
				if (cand < 0)
					break;
				if (function_matches_criteria(cand, name, true, crc, check_crc, nr_slots)) {
					free(overload);
					free(base_alloc);
					return cand;
				}
			}
			if (function_matches_criteria(base_fno, name, false, crc, check_crc, nr_slots)) {
				free(overload);
				free(base_alloc);
				return base_fno;
			}
			for (int idx = 1; idx < 256; idx++) {
				snprintf(overload, open + 16, "%s#%d", base_alloc, idx);
				int cand = ain_get_function(ain, overload);
				if (cand < 0)
					break;
				if (function_matches_criteria(cand, name, false, crc, check_crc, nr_slots)) {
					free(overload);
					free(base_alloc);
					return cand;
				}
			}
			free(overload);
			free(base_alloc);
			return base_fno;
		}
	}

	const char *base_name = base_alloc ? base_alloc : name;
	// Cross-encoding + Kanji-normalized lookup: check sym_id first for O(1) resolution.
	if (sym_id >= 0 && sym_id < ain->nr_functions && ain->functions[sym_id].name) {
		if (function_matches_criteria(sym_id, name, has_parens, crc, check_crc, nr_slots)
				&& save_symbols_equal(ain->functions[sym_id].name, base_name)) {
			free(base_alloc);
			return sym_id;
		}
	}
	int canon_hit = -1;
	for (int i = 0; i < ain->nr_functions; i++) {
		if (!ain->functions[i].name)
			continue;
		if (!function_matches_criteria(i, name, has_parens, crc, check_crc, nr_slots))
			continue;
		if (save_symbols_equal(ain->functions[i].name, base_name)) {
			canon_hit = i;
			break;
		}
	}
	if (canon_hit < 0 && has_parens) {
		for (int i = 0; i < ain->nr_functions; i++) {
			if (!ain->functions[i].name)
				continue;
			if (!function_matches_criteria(i, name, false, crc, check_crc, nr_slots))
				continue;
			if (save_symbols_equal(ain->functions[i].name, base_name)) {
				canon_hit = i;
				break;
			}
		}
	}
	if (canon_hit >= 0) {
		free(base_alloc);
		return canon_hit;
	}

	// Fallback when CRC is known (e.g. call stack record with localized term differences):
	// pick unique CRC match or unique best similarity score among matching CRC entries.
	if (allow_similarity && check_crc) {
		int best_fno = -1;
		int best_score = 0;
		int tie_count = 0;
		int crc_count = 0;
		int single_crc_fno = -1;
		for (int i = 0; i < ain->nr_functions; i++) {
			if (!function_matches_criteria(i, name, false, crc, true, nr_slots))
				continue;
			crc_count++;
			single_crc_fno = i;
			int score = save_symbol_similarity(ain->functions[i].name, base_name);
			if (best_fno < 0 || score > best_score) {
				best_fno = i;
				best_score = score;
				tie_count = 1;
			} else if (score == best_score) {
				tie_count++;
			}
		}
		if (crc_count == 1) {
			free(base_alloc);
			return single_crc_fno;
		}
		if (best_fno >= 0 && tie_count == 1) {
			free(base_alloc);
			return best_fno;
		}
	}

	free(base_alloc);
	return fno;
}

static int resolve_rsave_function(const char *name, int crc, bool check_crc, int nr_slots)
{
	return resolve_rsave_function_ex(name, -1, crc, check_crc, nr_slots, true);
}

/*
 * Save/load VM images as JSON.
 */

static const char * const page_type_strtab[] = {
	[GLOBAL_PAGE]   = "globals",
	[LOCAL_PAGE]    = "locals",
	[STRUCT_PAGE]   = "struct",
	[ARRAY_PAGE]    = "array",
	[DELEGATE_PAGE] = "delegate"
};

static enum page_type string_to_page_type(const char *str)
{
	if (!str)
		VM_ERROR("Invalid page type: NULL");
	if (!strcmp(str, "globals"))  return GLOBAL_PAGE;
	if (!strcmp(str, "locals"))   return LOCAL_PAGE;
	if (!strcmp(str, "struct"))   return STRUCT_PAGE;
	if (!strcmp(str, "array"))    return ARRAY_PAGE;
	if (!strcmp(str, "delegate")) return DELEGATE_PAGE;
	VM_ERROR("Invalid page type: %s", str);
}

static cJSON *value_to_json(union vm_value v)
{
	return cJSON_CreateNumber(v.i);
}

static int get_number(int i, void *data)
{
	return ((union vm_value*)data)[i].i;
}

static cJSON *resume_page_to_json(struct page *page)
{
	if (!page)
		return cJSON_CreateNull();
	if ((unsigned)page->type >= NR_PAGE_TYPES)
		ERROR("Invalid page type %d", page->type);

	cJSON *json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "type", page_type_strtab[page->type]);
	cJSON_AddNumberToObject(json, "subtype", page->index);
	if (page->type == LOCAL_PAGE) {
		cJSON_AddNumberToObject(json, "struct-page", page->local.struct_ptr);
		cJSON_AddNumberToObject(json, "parent-page", page->local.parent_page);
	} else if (page->type == ARRAY_PAGE) {
		cJSON_AddNumberToObject(json, "struct-type", page->array.struct_type);
		cJSON_AddNumberToObject(json, "rank", page->array.rank);
		if (page->a_type == AIN_ARRAY) {
			enum ain_data_type leaf_data;
			int struct_type, depth;
			unwrap_v12_array_type(&page->array.element_type, &leaf_data, &struct_type, &depth);
			leaf_data = normalize_v12_array_leaf_type(leaf_data);
			cJSON_AddNumberToObject(json, "element-type", leaf_data);
			cJSON_AddNumberToObject(json, "element-struct", struct_type);
			cJSON_AddNumberToObject(json, "element-depth", depth);
			if (ain->version >= 14) {
				char *spec = encode_x4_type(&page->array.element_type);
				cJSON_AddStringToObject(json, "v14-element-type", spec);
				free(spec);
			}
		} else if (ain->version >= 11) {
			enum ain_data_type leaf_data = normalize_v12_array_leaf_type(page->a_type);
			int struct_type = page->array.struct_type;
			int depth = page->array.rank > 0 ? page->array.rank - 1 : 0;
			cJSON_AddNumberToObject(json, "element-type", leaf_data);
			cJSON_AddNumberToObject(json, "element-struct", struct_type);
			cJSON_AddNumberToObject(json, "element-depth", depth);
		}
	} else if (page->type == DELEGATE_PAGE) {
		cJSON_AddNumberToObject(json, "cursor", page->delegate.cursor);
		cJSON_AddNumberToObject(json, "end", page->delegate.end);
	}

	cJSON *values = cJSON_CreateIntArray_cb(page->nr_vars, get_number, page->values);

	cJSON_AddItemToObject(json, "values", values);
	return json;
}

static cJSON *heap_item_to_json(int i, possibly_unused void *_)
{
	if (!heap[i].ref)
		return NULL;

	cJSON *item = cJSON_CreateArray();
	cJSON_AddItemToArray(item, cJSON_CreateNumber(i));
	cJSON_AddItemToArray(item, cJSON_CreateNumber(heap[i].ref));
	switch (heap[i].type) {
	case VM_PAGE:
		cJSON_AddItemToArray(item, resume_page_to_json(heap[i].page));
		break;
	case VM_STRING:
		cJSON_AddItemToArray(item, cJSON_CreateString(heap[i].s ? heap[i].s->text : ""));
		break;
	}
	if (ain->nr_delegates > 0) {
		cJSON_AddItemToArray(item, cJSON_CreateNumber(heap[i].seq));
	}
	return item;
}

static void gc_heap_delegates(void)
{
	for (size_t i = 0; i < heap_size; i++) {
		if (heap[i].ref > 0 && heap[i].type == VM_PAGE &&
		    heap[i].page && heap[i].page->type == DELEGATE_PAGE) {
			delegate_numof(heap[i].page);
		}
	}
}

static cJSON *heap_to_json(void)
{
	gc_heap_delegates();
	return cJSON_CreateArray_cb(heap_size, heap_item_to_json, NULL);
}

static cJSON *funcall_to_json(struct function_call *call)
{
	cJSON *json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "function", call->fno);
	cJSON_AddNumberToObject(json, "return-address", call->return_address);
	cJSON_AddNumberToObject(json, "local-page", call->page_slot);
	cJSON_AddNumberToObject(json, "struct-page", call->struct_page);
	cJSON_AddBoolToObject(json, "has-struct-page", call->has_struct_page);
	return json;
}

static cJSON *call_stack_to_json(void)
{
	cJSON *json = cJSON_CreateArray();
	for (int i = 0; i < call_stack_ptr; i++) {
		cJSON_AddItemToArray(json, funcall_to_json(&call_stack[i]));
	}
	return json;
}

static cJSON *stack_to_json(void)
{
	cJSON *json = cJSON_CreateArray();
	for (int i = 0; i < stack_ptr; i++) {
		cJSON_AddItemToArray(json, value_to_json(stack[i]));
	}
	return json;
}

static cJSON *vm_image_to_json(const char *key)
{
	cJSON *image = cJSON_CreateObject();
	cJSON_AddStringToObject(image, "key", key);
	cJSON_AddItemToObject(image, "heap", heap_to_json());
	cJSON_AddItemToObject(image, "call-stack", call_stack_to_json());
	cJSON_AddItemToObject(image, "stack", stack_to_json());
	cJSON_AddNumberToObject(image, "ip", instr_ptr);
	if (ain->nr_delegates > 0) {
		cJSON_AddNumberToObject(image, "next_seq", heap_next_seq);
	}
	return image;
}

static struct rsave_heap_frame *frame_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_frame *o = xcalloc(1, sizeof(struct rsave_heap_frame) + page->nr_vars * sizeof(int32_t));
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	struct ain_variable *vars;
	int nr_vars;
	if (page->type == GLOBAL_PAGE) {
		o->tag = RSAVE_GLOBALS;
		o->func.id = -1;
		nr_vars = ain->nr_globals;
		vars = ain->globals;
	} else {
		struct ain_function *f = &ain->functions[page->index];
		o->tag = RSAVE_LOCALS;
		o->func.name = format_rsave_function_name(page->index);
		nr_vars = f->nr_vars;
		vars = f->vars;
		o->struct_ptr = page->local.struct_ptr;
		o->parent_ptr = page->local.parent_page;
	}
	o->nr_types = nr_vars;
	o->types = xcalloc(o->nr_types, sizeof(int32_t));
	for (int i = 0; i < o->nr_types; i++)
		o->types[i] = vars[i].type.data;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_struct *struct_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_struct *o = xcalloc(1, sizeof(struct rsave_heap_struct) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_STRUCT;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	struct ain_struct *s = &ain->structures[page->index];
	o->ctor.name = xstrdup((s->constructor >= 0 && s->constructor < ain->nr_functions)
		? ain->functions[s->constructor].name : "");
	o->dtor.name = xstrdup((s->destructor >= 0 && s->destructor < ain->nr_functions)
		? ain->functions[s->destructor].name : "");
	o->uk = 0;
	if (s->constructor >= 0) {
		o->nr_ctor_flags = 1;
		o->ctor_flags = xmalloc(sizeof(int32_t));
		o->ctor_flags[0] = 1;
	}
	o->struct_type.name = xstrdup(s->name ? s->name : "");
	o->nr_types = s->nr_members;
	o->types = xcalloc(o->nr_types, sizeof(int32_t));
	for (int i = 0; i < o->nr_types; i++)
		o->types[i] = s->members[i].type.data;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_array *array_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_array *o = xcalloc(1, sizeof(struct rsave_heap_array) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_ARRAY;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	if (page->a_type == AIN_ARRAY) {
		enum ain_data_type leaf_data;
		int struct_type, depth;
		unwrap_v12_array_type(&page->array.element_type, &leaf_data, &struct_type, &depth);
		leaf_data = normalize_v12_array_leaf_type(leaf_data);
		o->rank_minus_1 = depth;
		o->data_type = leaf_data;
		if (ain->version >= 14 && v14_array_needs_x4_type(&page->array.element_type)) {
			o->struct_type.name = encode_x4_type(&page->array.element_type);
		} else {
			o->struct_type.name = xstrdup(array_leaf_type_name(leaf_data, struct_type));
		}
		o->root_rank = depth + 1;
	} else if (ain->version >= 11) {
		enum ain_data_type leaf_data = normalize_v12_array_leaf_type(page->a_type);
		int struct_type = page->array.struct_type;
		int depth = page->array.rank > 0 ? page->array.rank - 1 : 0;
		o->rank_minus_1 = depth;
		o->data_type = leaf_data;
		o->struct_type.name = xstrdup(array_leaf_type_name(leaf_data, struct_type));
		o->root_rank = depth + 1;
	} else {
		o->rank_minus_1 = page->array.rank - 1;
		o->data_type = page->index;
		if (page->array.struct_type >= 0 && page->array.struct_type < ain->nr_structures)
			o->struct_type.name = xstrdup(ain->structures[page->array.struct_type].name);
		else
			o->struct_type.name = xstrdup("");
		o->root_rank = page->array.rank;  // FIXME: this is incorrect for subarrays
	}
	o->is_not_empty = page->nr_vars ? 1 : 0;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_delegate *delegate_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_delegate *o = xcalloc(1, sizeof(struct rsave_heap_delegate) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_DELEGATE;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	o->cursor = page->delegate.cursor;
	o->end = page->delegate.end;
	if (ain->version >= 11) {
		o->nr_entries = page->nr_vars / 4;
		o->entries = xcalloc(o->nr_entries, sizeof(*o->entries));
		for (int i = 0; i < o->nr_entries; i++) {
			int fun = page->values[i * 4 + 1].i;
			o->entries[i].object = page->values[i * 4 + 0].i;
			o->entries[i].function.name = (fun >= 0 && fun < ain->nr_functions)
				? format_rsave_function_name(fun)
				: xstrdup("");
			o->entries[i].environment = page->values[i * 4 + 3].i;
		}
	}
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static void *heap_item_to_rsave(int i)
{
	if (!heap[i].ref)
		return rsave_null;
	if (heap[i].type == VM_STRING) {
		const char *txt = heap[i].s ? heap[i].s->text : "";
		int len = heap[i].s ? heap[i].s->size + 1 : 1;
		struct rsave_heap_string *s = xmalloc(sizeof(struct rsave_heap_string) + (size_t)len);
		s->tag = RSAVE_STRING;
		s->ref = heap[i].ref;
		s->seq = heap[i].seq;
		s->uk = 0;
		s->len = len;
		memcpy(s->text, txt, (size_t)len);
		return s;
	}
	struct page *page = heap[i].page;
	if (!page) {
		// Empty array.
		struct rsave_heap_array *o = xcalloc(1, sizeof(struct rsave_heap_array));
		o->tag = RSAVE_ARRAY;
		o->ref = heap[i].ref;
		o->seq = heap[i].seq;
		o->rank_minus_1 = -1;

		// FIXME: System40.exe populates them but we don't have the type
		// information of the array here.
		o->data_type = 0;
		o->struct_type.name = xstrdup("");

		o->root_rank = -1;
		o->is_not_empty = 0;
		o->nr_slots = 0;
		return o;
	}
	switch (page->type) {
	case GLOBAL_PAGE:
	case LOCAL_PAGE:
		return frame_page_to_rsave(page, i);
	case STRUCT_PAGE:
		return struct_page_to_rsave(page, i);
	case ARRAY_PAGE:
		return array_page_to_rsave(page, i);
	case DELEGATE_PAGE:
		return delegate_page_to_rsave(page, i);
	default:
		ERROR("unsupported type %d", page->type);
	}
}

static void save_heap_to_rsave(struct rsave *rs)
{
	gc_heap_delegates();
	rs->nr_heap_objs = heap_size;
	rs->heap = xcalloc(heap_size, sizeof(void*));
	for (size_t i = 0; i < heap_size; i++) {
		rs->heap[i] = heap_item_to_rsave(i);
	}
}

static void save_call_stack_to_rsave(struct rsave *rs)
{
	rs->nr_call_frames = call_stack_ptr + 1;
	rs->call_frames = xcalloc(rs->nr_call_frames, sizeof(struct rsave_call_frame));
	rs->call_frames[0].type = RSAVE_CALL_STACK_BOTTOM;
	rs->call_frames[0].local_ptr = -1;
	rs->nr_return_records = call_stack_ptr;
	rs->return_records = xcalloc(rs->nr_return_records + 1, sizeof(struct rsave_return_record));
	rs->return_records[0].return_addr = -1;

	struct ain_function *prev_func = NULL;
	for (int i = 0; i < call_stack_ptr; i++) {
		struct function_call *call = &call_stack[i];
		struct ain_function *func = &ain->functions[call->fno];
		if (call->struct_page >= 0 || (call->has_struct_page && func->struct_type >= 0)) {
			rs->call_frames[i + 1].type = RSAVE_METHOD_CALL;
		} else if (!strcmp(func->name, "main")) {
			rs->call_frames[i + 1].type = RSAVE_ENTRY_POINT;
		} else {
			rs->call_frames[i + 1].type = RSAVE_FUNCTION_CALL;
		}
		rs->call_frames[i + 1].local_ptr = call->page_slot;
		rs->call_frames[i + 1].struct_ptr = call->struct_page;
		if (i > 0 && prev_func) {
			rs->return_records[i].return_addr = call->return_address;
			rs->return_records[i].local_addr = call->return_address - prev_func->address;
		}
		rs->return_records[i + 1].caller_func = format_rsave_function_name(call->fno);
		rs->return_records[i + 1].crc = func->crc;
		prev_func = func;
	}
	int width = 0;
	if (instr_ptr + 2 <= ain->code_size) {
		int16_t op = get_opcode(instr_ptr);
		if (op >= 0 && op < NR_OPCODES)
			width = instruction_width(op);
	}
	rs->ip = rs->return_records[call_stack_ptr];
	rs->ip.return_addr = (int32_t)(instr_ptr + width);
	rs->ip.local_addr = prev_func ? (int32_t)(instr_ptr - prev_func->address + width) : -1;
}

static void save_stack_to_rsave(struct rsave *rs)
{
	rs->stack_size = stack_ptr;

	rs->stack = xcalloc(rs->stack_size, sizeof(int32_t));
	for (int i = 0; i < rs->stack_size; i++) {
		rs->stack[i] = stack[i].i;
	}
}

static void save_func_names_to_rsave(struct rsave *rs)
{
	rs->nr_func_names = ain->nr_functions;
	rs->func_names = xcalloc(rs->nr_func_names, sizeof(char *));
	for (int i = 0; i < ain->nr_functions; i++) {
		rs->func_names[i] = xstrdup(ain->functions[i].name ? ain->functions[i].name : "");
	}
}

static struct rsave *make_rsave(const char *key)
{
	struct rsave *save = xcalloc(1, sizeof(struct rsave));
	if (ain->version >= 11) {
		save->version = 14;
	} else if (ain->nr_delegates > 0) {
		save->version = 9;
	} else {
		// FIXME: This is not always correct. Pastel Chime Continue is AIN v4
		//        but uses RSM v7.
		save->version = ain->version <= 4 ? 6 : 7;
	}
	save->key = xstrdup(key ? key : "");
	return save;
}

static struct rsave *vm_image_to_rsave(const char *key)
{
	struct rsave *save = make_rsave(key);
	save->next_seq = heap_next_seq;
	save_heap_to_rsave(save);
	save_call_stack_to_rsave(save);
	save_stack_to_rsave(save);
	save_func_names_to_rsave(save);
	return save;
}

static int save_rsave_image(const char *key, const char *path)
{
	struct rsave *save = vm_image_to_rsave(key);
	if (!save)
		return 0;
	char *full_path = savedir_path(path);
	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(full_path, "wb", &tmp_path);
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(full_path), strerror(errno));
		free(full_path);
		rsave_free(save);
		return 0;
	}

	bool encrypt = !AIN_VERSION_GTE(ain, 6, 0);
	int compression_level = 1;
	enum savefile_error error = rsave_write(save, fp, encrypt, compression_level);
	rsave_free(save);
	if (error != SAVEFILE_SUCCESS) {
		WARNING("Failed to write save file: %s", savefile_strerror(error));
		atomic_save_abort(fp, tmp_path);
		free(full_path);
		return 0;
	}
	bool ok = atomic_save_commit(fp, full_path, tmp_path, false);
	free(full_path);
	return ok ? 1 : 0;
}

static int save_json_image(const char *key, const char *path)
{
	cJSON *image = vm_image_to_json(key);
	int r = save_json(path, image);
	cJSON_Delete(image);
	return r;
}

int vm_save_image(const char *key, const char *path)
{
	switch (config.save_format) {
	case SAVE_FORMAT_RSM:
		return save_rsave_image(key, path);
	case SAVE_FORMAT_JSON:
		return save_json_image(key, path);
	}
	return 0;
}

#define _invalid_save_data(file, func, line, fmt, ...)	\
	_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", file, func, line, ##__VA_ARGS__)
#define invalid_save_data(fmt, ...) \
	_invalid_save_data(__FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

static const char *json_strtype(int type)
{
	switch (type & 0xFF) {
	case cJSON_False:
	case cJSON_True:
		return "boolean";
	case cJSON_NULL:   return "null";
	case cJSON_Number: return "number";
	case cJSON_String: return "string";
	case cJSON_Array:  return "array";
	case cJSON_Object: return "object";
	}
	return "unknown-type";
}

static cJSON *_type_check(const char *file, const char *func, int line, int type, cJSON *json)
{
	if (!json)
		_invalid_save_data(file, func, line, "Expected %s but got NULL", json_strtype(type));
	if (!(json->type & type))
		_invalid_save_data(file, func, line, "Expected %s but value is of type %s", json_strtype(type), json_strtype(json->type));
	return json;
}

#define type_check(type, json) _type_check(__FILE__, __func__, __LINE__, type, json)

static void load_json_page(int slot, cJSON *json)
{
	// unpack
	cJSON *type    = type_check(cJSON_String, cJSON_GetObjectItem(json, "type"));
	cJSON *subtype = type_check(cJSON_Number, cJSON_GetObjectItem(json, "subtype"));
	cJSON *values  = type_check(cJSON_Array,  cJSON_GetObjectItem(json, "values"));

	enum page_type page_type = string_to_page_type(cJSON_GetStringValue(type));
	int nr_vals = cJSON_GetArraySize(values);
	if (nr_vals < 0 || nr_vals > 64 * 1024 * 1024)
		invalid_save_data("Invalid page size %d", nr_vals);

	// allocate page
	struct page *page = alloc_page(page_type, subtype->valueint, nr_vals);
	if (page_type == LOCAL_PAGE) {
		cJSON *struct_page = cJSON_GetObjectItem(json, "struct-page");
		cJSON *parent_page = cJSON_GetObjectItem(json, "parent-page");
		page->local.struct_ptr = struct_page ? type_check(cJSON_Number, struct_page)->valueint : -1;
		page->local.parent_page = parent_page ? type_check(cJSON_Number, parent_page)->valueint : -1;
	} else if (page_type == ARRAY_PAGE) {
		cJSON *_struct_type = type_check(cJSON_Number, cJSON_GetObjectItem(json, "struct-type"));
		cJSON *_rank        = type_check(cJSON_Number, cJSON_GetObjectItem(json, "rank"));
		page->array.struct_type = _struct_type->valueint;
		page->array.rank = _rank->valueint;
		if (ain->version >= 11) {
			cJSON *v14_spec = cJSON_GetObjectItem(json, "v14-element-type");
			if (ain->version >= 14 && v14_spec && cJSON_IsString(v14_spec) &&
			    decode_x4_type(cJSON_GetStringValue(v14_spec), &page->array.element_type)) {
				page->a_type = AIN_ARRAY;
				page->array.rank = 1;
				page->array.struct_type = page->array.element_type.struc;
			} else {
				cJSON *elem_type = cJSON_GetObjectItem(json, "element-type");
				cJSON *elem_struct = cJSON_GetObjectItem(json, "element-struct");
				cJSON *elem_depth = cJSON_GetObjectItem(json, "element-depth");
				enum ain_data_type dt = elem_type
					? normalize_v12_array_leaf_type(type_check(cJSON_Number, elem_type)->valueint)
					: normalize_v12_array_leaf_type(page->a_type);
				int st = elem_struct ? type_check(cJSON_Number, elem_struct)->valueint : page->array.struct_type;
				int depth = elem_depth
					? type_check(cJSON_Number, elem_depth)->valueint
					: (page->a_type == AIN_ARRAY ? 0 : (page->array.rank > 0 ? page->array.rank - 1 : 0));
				if (depth < 0 || depth > 32) {
					free_page(page);
					invalid_save_data("Invalid array element depth %d", depth);
				}
				page->a_type = AIN_ARRAY;
				page->array.rank = 1;
				page->array.element_type = make_v12_element_type(dt, st, depth);
			}
		}
	} else if (page_type == DELEGATE_PAGE) {
		cJSON *cursor = cJSON_GetObjectItem(json, "cursor");
		cJSON *end = cJSON_GetObjectItem(json, "end");
		page->delegate.cursor = cursor ? type_check(cJSON_Number, cursor)->valueint : 0;
		page->delegate.end = end ? type_check(cJSON_Number, end)->valueint : 0;
	}

	// init page variables
	int i = 0;
	cJSON *item;
	cJSON_ArrayForEach(item, values) {
		if (i >= page->nr_vars)
			break;
		type_check(cJSON_Number, item);
		page->values[i].i = item->valueint;
		i++;
	}

	heap[slot].type = VM_PAGE;
	heap_set_page(slot, page);
}

static void load_json_string(int slot, cJSON *json)
{
	const char *str = cJSON_GetStringValue(json);
	if (!str)
		str = "";
	heap[slot].s = make_string(str, strlen(str));
	heap[slot].type = VM_STRING;
}

static void delete_heap(void)
{
	// free heap
	for (size_t i = 0; i < heap_size; i++) {
		if (!heap[i].ref)
			continue;
		switch (heap[i].type) {
		case VM_PAGE:
			if (heap[i].page) {
				struct page *p = heap[i].page;
				heap_set_page(i, NULL);
				free_page(p);
			}
			break;
		case VM_STRING:
			free_string(heap[i].s);
			break;
		}
		heap[i].ref = 0;
		heap[i].seq = 0;
		heap[i].has_delegate_target = false;
	}

	heap_free_ptr = 0;
	for (size_t i = 0; i < heap_size; i++) {
		heap_free_stack[i] = i;
	}
}

static void alloc_heap_slot(int slot)
{
	if (slot < 0 || slot > 64 * 1024 * 1024)
		invalid_save_data("Invalid heap slot %d", slot);
	if ((size_t)slot >= heap_size) {
		size_t next_size = heap_size ? heap_size * 2 : 1024;
		while ((size_t)slot >= next_size) {
			if (next_size > SIZE_MAX / 2)
				invalid_save_data("Heap slot overflow %d", slot);
			next_size *= 2;
		}
		heap_grow(next_size);
	}
	if (heap[slot].ref != 0)
		invalid_save_data("Duplicate heap slot %d", slot);
}

static void rebuild_heap_free_stack(void)
{
	size_t free_idx = heap_size;
	for (size_t i = heap_size; i > 0; i--) {
		size_t slot = i - 1;
		if (heap[slot].ref == 0)
			heap_free_stack[--free_idx] = (int32_t)slot;
	}
	heap_free_ptr = free_idx;
}

static void load_json_heap(cJSON *json)
{
	delete_heap();

	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		type_check(cJSON_Array, item);
		if (cJSON_GetArraySize(item) < 3)
			invalid_save_data("Invalid heap data");

		int slot = type_check(cJSON_Number, cJSON_GetArrayItem(item, 0))->valueint;
		int ref  = type_check(cJSON_Number, cJSON_GetArrayItem(item, 1))->valueint;
		if (slot < 0 || ref <= 0)
			invalid_save_data("Invalid heap slot (%d) or refcount (%d)", slot, ref);
		cJSON *value = cJSON_GetArrayItem(item, 2);
		int seq = ain->nr_delegates > 0
			? type_check(cJSON_Number, cJSON_GetArrayItem(item, 3))->valueint
			: slot;

		alloc_heap_slot(slot);
		heap[slot].ref = ref;
		heap[slot].seq = seq;

		if (cJSON_IsString(value)) {
			load_json_string(slot, value);
		} else if (cJSON_IsObject(value)) {
			load_json_page(slot, value);
		} else if (cJSON_IsNull(value)) {
			heap[slot].type = VM_PAGE;
			heap[slot].page = NULL;
		} else {
			invalid_save_data("Invalid heap data");
		}
	}
	rebuild_heap_free_stack();
	heap_refresh_delegate_targets();
}

static int resolve_func_symbol(struct rsave_symbol *sym, int nr_slots)
{
	if (!sym->name)
		return sym->id;
	return resolve_rsave_function_ex(sym->name, sym->id, 0, false, nr_slots, false);
}

static int resolve_struct_symbol(struct rsave_symbol *sym)
{
	if (!sym->name)
		return sym->id;
	if (!sym->name[0])
		return -1;
	int id = ain_get_struct(ain, sym->name);
	if (id >= 0)
		return id;
	if (sym->id >= 0 && sym->id < ain->nr_structures && ain->structures[sym->id].name
			&& save_symbols_equal(ain->structures[sym->id].name, sym->name))
		return sym->id;
	return save_find_struct(sym->name);
}

static bool rsave_check_id_alignment(const struct rsave *save)
{
	if (!save || !ain)
		return false;
	if (save->nr_heap_objs > 0 && save->heap[0]
			&& *(enum rsave_heap_tag *)save->heap[0] == RSAVE_GLOBALS) {
		const struct rsave_heap_frame *gf = save->heap[0];
		if (gf->nr_slots != ain->nr_globals)
			return false;
	}
	int aligned = 0;
	int shifted = 0;
	for (int slot = 0; slot < save->nr_heap_objs; slot++) {
		enum rsave_heap_tag *tag = save->heap[slot];
		if (!tag)
			continue;
		if (*tag == RSAVE_STRUCT) {
			const struct rsave_heap_struct *s = save->heap[slot];
			if (s->struct_type.name && s->struct_type.name[0]) {
				int resolved = resolve_struct_symbol((struct rsave_symbol *)&s->struct_type);
				if (resolved >= 0) {
					if (resolved == s->struct_type.id)
						aligned++;
					else
						shifted++;
				}
			}
		} else if (*tag == RSAVE_LOCALS) {
			const struct rsave_heap_frame *f = save->heap[slot];
			if (f->func.name && f->func.name[0]) {
				int resolved = resolve_rsave_function_ex(f->func.name, f->func.id, 0, false, f->nr_slots, false);
				if (resolved < 0)
					resolved = resolve_rsave_function_ex(f->func.name, f->func.id, 0, false, -1, false);
				if (resolved >= 0) {
					if (resolved == f->func.id)
						aligned++;
					else
						shifted++;
				}
			}
		}
	}
	return aligned > 0 && shifted == 0;
}

static bool load_rsave_frame(int slot, struct rsave_heap_frame *f, int32_t version, bool id_aligned)
{
	if (f->ref <= 0 || f->nr_slots < 0 || f->nr_slots > 64 * 1024 * 1024 || f->nr_types < 0)
		invalid_save_data("Invalid call frame metadata");
	struct page *page;
	int nr_vars;
	struct ain_variable *vars;
	if (f->tag == RSAVE_GLOBALS) {
		nr_vars = ain->nr_globals;
		vars = ain->globals;
		page = alloc_page(GLOBAL_PAGE, 0, max(f->nr_slots, nr_vars));
	} else {
		int func = resolve_func_symbol(&f->func, f->nr_slots);
		if (func < 0)
			func = resolve_func_symbol(&f->func, -1);
		if (func < 0 && id_aligned && f->func.id >= 0 && f->func.id < ain->nr_functions) {
			int cand = f->func.id;
			if ((cand == 0 || ain->functions[cand].address != 0)
					&& ain->functions[cand].nr_vars == f->nr_slots
					&& ain->functions[cand].nr_vars >= f->nr_types) {
				bool types_ok = true;
				for (int i = 0; i < f->nr_types; i++) {
					if (f->types[i] != ain->functions[cand].vars[i].type.data) {
						types_ok = false;
						break;
					}
				}
				if (types_ok)
					func = cand;
			}
		}
		if (func < 0)
			VM_ERROR("Failed to load VM image: function '%s' not found",
				 display_sjis0(f->func.name ? f->func.name : ""));
		nr_vars = ain->functions[func].nr_vars;
		vars = ain->functions[func].vars;
		page = alloc_page(LOCAL_PAGE, func, max(f->nr_slots, nr_vars));
		page->local.struct_ptr = f->struct_ptr;
		page->local.parent_page = version == 14 ? f->parent_ptr : -1;
	}

	// type check
	if (nr_vars < f->nr_types) {
		free_page(page);
		invalid_save_data("call frame has too many variables");
	}
	for (int i = 0; i < f->nr_types; i++) {
		if (f->types[i] != vars[i].type.data) {
			free_page(page);
			invalid_save_data(
				"variable type mismatch. Expected %d, got %d",
				vars[i].type.data, f->types[i]);
		}
	}

	for (int i = 0; i < f->nr_slots; i++) {
		// TODO: update function pointers using rsave->func_names
		page->values[i].i = f->slots[i];
	}
	alloc_heap_slot(slot);
	heap[slot].ref = f->ref;
	heap[slot].seq = version == 14 ? slot + 1 : f->seq;
	heap[slot].type = VM_PAGE;
	heap_set_page(slot, page);
	return nr_vars > f->nr_slots;
}

static void load_rsave_string(int slot, struct rsave_heap_string *s, int32_t version, bool src_is_gbk)
{
	if (s->ref <= 0 || s->len < 0)
		invalid_save_data("Invalid string metadata in VM image");
	alloc_heap_slot(slot);
	heap[slot].ref = s->ref;
	heap[slot].seq = version == 14 ? slot + 1 : s->seq;
	heap[slot].type = VM_STRING;
	heap[slot].s = save_decode_vm_string(s->text, s->len > 0 ? (size_t)(s->len - 1) : 0, src_is_gbk);
}

static void load_rsave_array(int slot, struct rsave_heap_array *a, int32_t version)
{
	if (a->ref <= 0 || a->nr_slots < 0 || a->nr_slots > 64 * 1024 * 1024)
		invalid_save_data("Invalid array metadata in VM image");
	if (a->rank_minus_1 < 0) {
		alloc_heap_slot(slot);
		heap[slot].ref = a->ref;
		heap[slot].seq = version == 14 ? slot + 1 : a->seq;
		heap[slot].type = VM_PAGE;
		heap[slot].page = NULL;
		return;
	}
	if (a->rank_minus_1 > 32)
		invalid_save_data("Invalid array rank in VM image");
	enum ain_data_type data_type = a->data_type;
	if (ain->version >= 11)
		data_type = normalize_v12_array_leaf_type(data_type);
	struct page *page;
	struct ain_type decoded_v14;
	if (ain->version >= 14 && a->struct_type.name && decode_x4_type(a->struct_type.name, &decoded_v14)) {
		page = alloc_page(ARRAY_PAGE, AIN_ARRAY, a->nr_slots);
		page->array.struct_type = decoded_v14.struc;
		page->array.rank = 1;
		page->array.element_type = decoded_v14;
	} else if (ain->version >= 11) {
		int struct_type = resolve_array_leaf_symbol(data_type, &a->struct_type);
		page = alloc_page(ARRAY_PAGE, AIN_ARRAY, a->nr_slots);
		page->array.struct_type = struct_type;
		page->array.rank = 1;
		page->array.element_type = make_v12_element_type(data_type, struct_type, a->rank_minus_1);
	} else {
		int struct_type = resolve_array_leaf_symbol(data_type, &a->struct_type);
		page = alloc_page(ARRAY_PAGE, data_type, a->nr_slots);
		page->array.struct_type = struct_type;
		page->array.rank = a->rank_minus_1 + 1;
	}
	for (int i = 0; i < a->nr_slots; i++)
		page->values[i].i = a->slots[i];

	alloc_heap_slot(slot);
	heap[slot].ref = a->ref;
	heap[slot].seq = version == 14 ? slot + 1 : a->seq;
	heap[slot].type = VM_PAGE;
	heap_set_page(slot, page);
}

static bool load_rsave_struct(int slot, struct rsave_heap_struct *s, int32_t version, bool id_aligned)
{
	if (s->ref <= 0 || s->nr_slots < 0 || s->nr_slots > 64 * 1024 * 1024 || s->nr_types < 0)
		invalid_save_data("Invalid struct metadata in VM image");
	int struct_index = resolve_struct_symbol(&s->struct_type);
	if (struct_index < 0 && id_aligned && s->struct_type.id >= 0 && s->struct_type.id < ain->nr_structures) {
		struct ain_struct *cand = &ain->structures[s->struct_type.id];
		if (s->nr_types <= cand->nr_members) {
			bool types_ok = true;
			for (int i = 0; i < s->nr_types; i++) {
				if (s->types[i] != cand->members[i].type.data) {
					types_ok = false;
					break;
				}
			}
			if (types_ok)
				struct_index = s->struct_type.id;
		}
	}
	if (struct_index < 0 || struct_index >= ain->nr_structures) {
		invalid_save_data("Unknown struct '%s' in VM image",
			s->struct_type.name ? s->struct_type.name : "");
	}

	// type check
	struct ain_struct *as = &ain->structures[struct_index];
	if (as->nr_members < s->nr_types) {
		invalid_save_data("Struct %s has too many members", as->name);
	}
	for (int i = 0; i < s->nr_types; i++) {
		if (s->types[i] != as->members[i].type.data) {
			invalid_save_data(
				"%s.%s: member type mismatch. Expected %d, got %d",
				as->name, as->members[i].name,
				as->members[i].type.data, s->types[i]);
		}
	}

	struct page *page = alloc_page(STRUCT_PAGE, struct_index, max(s->nr_slots, as->nr_members));
	for (int i = 0; i < s->nr_slots; i++) {
		// TODO: update function pointers using rsave->func_names
		page->values[i].i = s->slots[i];
	}

	alloc_heap_slot(slot);
	heap[slot].ref = s->ref;
	heap[slot].seq = version == 14 ? slot + 1 : s->seq;
	heap[slot].type = VM_PAGE;
	heap_set_page(slot, page);
	return as->nr_members > s->nr_slots;
}

static bool delegate_target_matches_function(struct rsave *save, int obj, int fun)
{
	if (obj < 0 || obj >= save->nr_heap_objs || !save->heap[obj])
		return false;
	if (*(enum rsave_heap_tag *)save->heap[obj] != RSAVE_STRUCT)
		return false;
	if (fun < 0 || fun >= ain->nr_functions || !ain->functions[fun].name)
		return false;
	if (ain->version >= 14)
		return true;
	struct rsave_heap_struct *s = save->heap[obj];
	const char *fname = ain->functions[fun].name;
	const char *at = NULL;
	for (const char *p = fname; *p; p++) {
		if (SJIS_2BYTE(*p) && p[1]) {
			p++;
			continue;
		}
		if (*p == '@') {
			at = p;
			break;
		}
	}
	if (!at || at == fname)
		return true;
	size_t struct_len = (size_t)(at - fname);
	if (s->struct_type.name) {
		return save_symbol_prefix_equal(s->struct_type.name, fname, struct_len);
	}
	if (s->struct_type.id >= 0 && s->struct_type.id < ain->nr_structures) {
		const char *sname = ain->structures[s->struct_type.id].name;
		return save_symbol_prefix_equal(sname, fname, struct_len);
	}
	return true;
}

static void load_rsave_delegate(int slot, struct rsave_heap_delegate *d, struct rsave *save)
{
	if (d->ref <= 0)
		invalid_save_data("Invalid delegate refcount %d", d->ref);
	if (save->version == 14) {
		if (d->nr_entries < 0 || d->nr_entries > 16 * 1024 * 1024)
			invalid_save_data("Invalid delegate entry count %d", d->nr_entries);
		struct page *page = alloc_page(DELEGATE_PAGE, 0, d->nr_entries * 4);
		page->delegate.cursor = d->cursor;
		page->delegate.end = d->end;
		for (int i = 0; i < d->nr_entries; i++) {
			int obj = d->entries[i].object;
			const char *fname = d->entries[i].function.name;
			int fun = -1;
			int seq = -1;
			if (!fname || !fname[0]) {
				obj = -1;
			} else {
				fun = resolve_rsave_function(fname, 0, false, -1);
				if (fun < 0)
					VM_ERROR("Failed to load VM image: delegate function '%s' not found",
						 display_sjis0(fname));
				seq = 0;
				if (obj >= 0) {
					if (delegate_target_matches_function(save, obj, fun))
						seq = obj + 1;
					else
						seq = -1;
				}
			}
			page->values[i * 4 + 0].i = obj;
			page->values[i * 4 + 1].i = fun;
			page->values[i * 4 + 2].i = seq;
			page->values[i * 4 + 3].i = d->entries[i].environment;
		}
		alloc_heap_slot(slot);
		heap[slot].ref = d->ref;
		heap[slot].seq = slot + 1;
		heap[slot].type = VM_PAGE;
		heap_set_page(slot, page);
		return;
	}
	if (d->nr_slots < 0 || d->nr_slots > 64 * 1024 * 1024)
		invalid_save_data("Invalid delegate slot count %d", d->nr_slots);
	struct page *page = alloc_page(DELEGATE_PAGE, 0, d->nr_slots);
	for (int i = 0; i < d->nr_slots; i++)
		page->values[i].i = d->slots[i];

	alloc_heap_slot(slot);
	heap[slot].ref = d->ref;
	heap[slot].seq = d->seq;
	heap[slot].type = VM_PAGE;
	heap_set_page(slot, page);
}

static bool rsave_detect_is_gbk(const struct rsave *save, const char *expected_key)
{
	int native_votes = 0;
	int opp_votes = 0;
	if (expected_key && save && save->key) {
		bool ascii = true;
		for (const unsigned char *p = (const unsigned char *)save->key; *p; p++) {
			if (*p >= 0x80) {
				ascii = false;
				break;
			}
		}
		if (!ascii) {
			if (!strcmp(expected_key, save->key))
				native_votes += 40;
			else if (save_names_equal(expected_key, save->key))
				opp_votes += 40;
		}
	}

	int checked_syms = 0;
	int checked_strs = 0;
	int bias = 0;
	for (int slot = 0; save && slot < save->nr_heap_objs; slot++) {
		enum rsave_heap_tag *tag = save->heap[slot];
		if (!tag)
			continue;
		if (*tag == RSAVE_STRUCT && checked_syms < 64) {
			const struct rsave_heap_struct *s = save->heap[slot];
			const char *name = s->struct_type.name;
			if (name && name[0]) {
				bool has_non_ascii = save_text_gbk_bias(name) != 0;
				if (!has_non_ascii) {
					for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
						if (*p >= 0x80) { has_non_ascii = true; break; }
					}
				}
				if (has_non_ascii) {
					checked_syms++;
					if (ain_get_struct(ain, (char *)name) >= 0)
						native_votes += 5;
					else if (resolve_struct_symbol((struct rsave_symbol *)&s->struct_type) >= 0)
						opp_votes += 5;
				}
			}
		} else if (*tag == RSAVE_LOCALS && checked_syms < 64) {
			const struct rsave_heap_frame *f = save->heap[slot];
			const char *name = f->func.name;
			if (name && name[0]) {
				bool has_non_ascii = false;
				for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
					if (*p >= 0x80) { has_non_ascii = true; break; }
				}
				if (has_non_ascii) {
					checked_syms++;
					if (ain_get_function(ain, (char *)name) >= 0)
						native_votes += 5;
					else if (resolve_rsave_function_ex(name, f->func.id, 0, false, f->nr_slots, false) >= 0)
						opp_votes += 5;
				}
			}
		} else if (*tag == RSAVE_STRING && checked_strs < 64) {
			const struct rsave_heap_string *s = save->heap[slot];
			if (s->len > 1) {
				int b = save_text_gbk_bias(s->text);
				if (b) {
					bias += b;
					checked_strs++;
				}
			}
		}
	}
	for (int i = 0; save && i < save->nr_comments && checked_strs < 64; i++) {
		if (save->comments[i] && save->comments[i][0]) {
			int b = save_text_gbk_bias(save->comments[i]);
			if (b) {
				bias += b;
				checked_strs++;
			}
		}
	}

	if (opp_votes > native_votes)
		return !sys4_text_is_gbk;
	if (native_votes > opp_votes)
		return sys4_text_is_gbk;
	if (bias > 12)
		return true;
	if (bias < -12)
		return false;
	return sys4_text_is_gbk;
}

static void load_rsave_heap(struct rsave *save, const char *expected_key)
{
	delete_heap();
	bool need_pad_init = false;
	bool id_aligned = rsave_check_id_alignment(save);
	bool src_is_gbk = rsave_detect_is_gbk(save, expected_key);

	for (int slot = 0; slot < save->nr_heap_objs; slot++) {
		enum rsave_heap_tag *tag = save->heap[slot];
		if (!tag)
			continue;
		switch (*tag) {
		case RSAVE_GLOBALS:
		case RSAVE_LOCALS:   need_pad_init |= load_rsave_frame(slot, save->heap[slot], save->version, id_aligned);  break;
		case RSAVE_STRING:   load_rsave_string(slot, save->heap[slot], save->version, src_is_gbk); break;
		case RSAVE_ARRAY:    load_rsave_array(slot, save->heap[slot], save->version);  break;
		case RSAVE_STRUCT:   need_pad_init |= load_rsave_struct(slot, save->heap[slot], save->version, id_aligned); break;
		case RSAVE_DELEGATE: load_rsave_delegate(slot, save->heap[slot], save);        break;
		case RSAVE_NULL: break;
		}
	}
	rebuild_heap_free_stack();
	heap_next_seq = save->version == 14 ? save->nr_heap_objs + 1 : save->next_seq;
	if (unlikely(need_pad_init)) {
		for (int slot = 0; slot < save->nr_heap_objs; slot++) {
			enum rsave_heap_tag *tag = save->heap[slot];
			if (*tag == RSAVE_GLOBALS || *tag == RSAVE_LOCALS) {
				struct rsave_heap_frame *f = save->heap[slot];
				struct page *page = heap[slot].page;
				struct ain_variable *vars = f->tag == RSAVE_GLOBALS
					? ain->globals : ain->functions[page->index].vars;
				for (int i = f->nr_slots; i < page->nr_vars; i++) {
					if (ain->version >= 14) {
						int w = variable_init_typed(&page->values[i], &vars[i].type, false, false);
						i += w - 1;
					} else if (vars[i].type.data == AIN_STRUCT) {
						int st_slot = alloc_struct(vars[i].type.struc);
						page->values[i].i = st_slot;
					} else {
						page->values[i] = variable_initval_typed(&vars[i].type);
					}
				}
			} else if (*tag == RSAVE_STRUCT) {
				struct rsave_heap_struct *s = save->heap[slot];
				struct page *page = heap[slot].page;
				struct ain_struct *as = &ain->structures[page->index];
				for (int i = s->nr_slots; i < page->nr_vars; i++) {
					if (ain->version >= 14) {
						int w = variable_init_typed(&page->values[i], &as->members[i].type, false, false);
						i += w - 1;
					} else if (as->members[i].type.data == AIN_STRUCT) {
						int st_slot = alloc_struct(as->members[i].type.struc);
						page->values[i].i = st_slot;
					} else {
						page->values[i] = variable_initval_typed(&as->members[i].type);
					}
				}
			}
		}
	}
	heap_refresh_delegate_targets();
}

static bool infer_frame_has_struct_page(int32_t fno, int32_t struct_page)
{
	if (struct_page >= 0)
		return true;
	if (!ain || fno < 0 || fno >= ain->nr_functions)
		return true;
	if (fno == ain->main || fno == ain->alloc)
		return true;
	if (ain->functions[fno].struct_type >= 0 || ain->functions[fno].is_lambda)
		return true;
	return false;
}

static void load_json_call_stack(cJSON *json)
{
	call_stack_ptr = 0;
	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		if (call_stack_ptr >= (int32_t)(sizeof(call_stack) / sizeof(call_stack[0])))
			invalid_save_data("Call stack overflow");
		type_check(cJSON_Object, item);
		int32_t fno = type_check(cJSON_Number, cJSON_GetObjectItem(item, "function"))->valueint;
		if (fno < 0 || fno >= ain->nr_functions)
			invalid_save_data("Invalid function index %d", fno);
		int32_t return_address = type_check(cJSON_Number, cJSON_GetObjectItem(item, "return-address"))->valueint;
		int32_t struct_page = type_check(cJSON_Number, cJSON_GetObjectItem(item, "struct-page"))->valueint;
		cJSON *has_sp = cJSON_GetObjectItem(item, "has-struct-page");
		call_stack[call_stack_ptr++] = (struct function_call) {
			.fno             = fno,
			.call_address    = return_address,
			.return_address  = return_address,
			.page_slot       = type_check(cJSON_Number, cJSON_GetObjectItem(item, "local-page"))->valueint,
			.struct_page     = struct_page,
			.has_struct_page = has_sp ? cJSON_IsTrue(has_sp) : infer_frame_has_struct_page(fno, struct_page),
		};
	}
}

static void load_rsave_call_stack(struct rsave *save)
{
	call_stack_ptr = 0;

	if (save->nr_return_records <= 0 || save->nr_call_frames <= 0 || save->return_records[0].return_addr != -1)
		invalid_save_data("invalid resume save");
	struct rsave_return_record *rr = save->return_records;
	if (save->nr_return_records == save->nr_call_frames)
		rr++;
	else if (save->nr_return_records != save->nr_call_frames - 1)
		invalid_save_data("invalid resume save");

	int32_t return_address = -1;
	for (int i = 0; i < save->nr_call_frames; i++) {
		if (save->call_frames[i].type != RSAVE_CALL_STACK_BOTTOM) {
			if (call_stack_ptr >= (int32_t)(sizeof(call_stack) / sizeof(call_stack[0])))
				invalid_save_data("Call stack overflow");
			const char *cfunc = rr->caller_func ? rr->caller_func : "";
			int fno = resolve_rsave_function_ex(cfunc, -1, rr->crc, true, -1, false);
			if (fno < 0) {
				int32_t lslot = save->call_frames[i].local_ptr;
				if (lslot >= 0 && (size_t)lslot < heap_size && heap[lslot].type == VM_PAGE
						&& heap[lslot].page && heap[lslot].page->type == LOCAL_PAGE) {
					int lp_fno = heap[lslot].page->index;
					if (lp_fno >= 0 && lp_fno < ain->nr_functions
							&& (lp_fno == 0 || ain->functions[lp_fno].address != 0)
							&& ain->functions[lp_fno].crc == rr->crc)
						fno = lp_fno;
				}
			}
			if (fno < 0)
				fno = resolve_rsave_function(cfunc, rr->crc, true, -1);
			if (fno < 0)
				fno = resolve_rsave_function(cfunc, rr->crc, false, -1);
			if (fno < 0)
				VM_ERROR("Failed to load VM image: function '%s' not found", display_sjis0(cfunc));
			if (ain->functions[fno].crc != rr->crc)
				VM_ERROR("Failed to load VM image: CRC mismatch for function '%s'", display_sjis0(cfunc));
			int32_t struct_page = save->call_frames[i].struct_ptr;
			bool has_struct_page = save->call_frames[i].type != RSAVE_FUNCTION_CALL
				|| infer_frame_has_struct_page(fno, struct_page);
			call_stack[call_stack_ptr++] = (struct function_call) {
				.fno             = fno,
				.call_address    = return_address,
				.return_address  = return_address,
				.page_slot       = save->call_frames[i].local_ptr,
				.struct_page     = struct_page,
				.has_struct_page = has_struct_page,
			};
			// Calculate return address from the function address and offset
			// to make it robust to ain changes.
			return_address = ain->functions[fno].address + rr->local_addr;
		}
		if (++rr == save->return_records + save->nr_return_records)
			rr = &save->ip;
	}
	// RSM records instruction pointer after the CALLSYS/CALLHLL instruction,
	// whereas vm_execute advances instr_ptr by instructions[opcode].ip_inc after
	// returning from execute_instruction.
	int ip_inc = 0;
	if (instr_ptr + 2 <= ain->code_size) {
		int16_t op = get_opcode(instr_ptr);
		if (op >= 0 && op < NR_OPCODES)
			ip_inc = instructions[op].ip_inc;
	}
	if (return_address < ip_inc || (size_t)(return_address - ip_inc) > ain->code_size)
		invalid_save_data("Invalid resume instruction pointer %d", return_address);
	instr_ptr = (size_t)(return_address - ip_inc);
}

#define MAX_RESUME_VM_STACK_SIZE (1024 * 1024)

static void load_json_stack(cJSON *json)
{
	stack_ptr = 0;
	int count = cJSON_GetArraySize(json);
	if (count < 0 || count > MAX_RESUME_VM_STACK_SIZE)
		invalid_save_data("Invalid VM stack size %d", count);
	vm_stack_ensure((size_t)count);

	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		if (stack_ptr >= count)
			invalid_save_data("VM stack overflow");
		type_check(cJSON_Number, item);
		stack_push_value(vm_int(item->valueint));
	}
}

static void load_rsave_stack(struct rsave *save)
{
	stack_ptr = 0;
	if (save->stack_size < 0 || save->stack_size > MAX_RESUME_VM_STACK_SIZE)
		invalid_save_data("Invalid VM stack size %d", save->stack_size);
	if (save->stack_size > 0 && !save->stack)
		invalid_save_data("Missing VM stack data");
	vm_stack_ensure((size_t)save->stack_size);

	for (int i = 0; i < save->stack_size; i++) {
		stack_push_value(vm_int(save->stack[i]));
	}
}

static cJSON *read_json_image(const char *keyname, const char *path)
{
	char *full_path = savedir_path(path);
	char *save_file = file_read(full_path, NULL);
	free(full_path);
	if (!save_file)
		return NULL;

	cJSON *save = cJSON_Parse(save_file);
	free(save_file);
	if (!save || !cJSON_IsObject(save)) {
		cJSON_Delete(save);
		return NULL;
	}
	cJSON *key = cJSON_GetObjectItem(save, "key");
	if (!key || !cJSON_IsString(key)) {
		cJSON_Delete(save);
		invalid_save_data("Missing or invalid key");
	}
	if (!save_names_equal(keyname, cJSON_GetStringValue(key))) {
		cJSON_Delete(save);
		invalid_save_data("Key doesn't match");
	}

	return save;
}

static void load_json_image(const char *key, const char *path)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		VM_ERROR("Failed to read VM image: '%s'", display_sjis0(path));
	}
	cJSON *ip = type_check(cJSON_Number, cJSON_GetObjectItem(save, "ip"));
	if (ip->valueint < 0 || (size_t)ip->valueint > ain->code_size) {
		cJSON_Delete(save);
		invalid_save_data("Invalid instruction pointer %d", ip->valueint);
	}
	load_json_heap(type_check(cJSON_Array, cJSON_GetObjectItem(save, "heap")));
	load_json_call_stack(type_check(cJSON_Array, cJSON_GetObjectItem(save, "call-stack")));
	load_json_stack(type_check(cJSON_Array, cJSON_GetObjectItem(save, "stack")));
	instr_ptr = (size_t)ip->valueint;
	if (ain->nr_delegates > 0) {
		cJSON *next_seq = type_check(cJSON_Number, cJSON_GetObjectItem(save, "next_seq"));
		heap_next_seq = next_seq->valueint;
	} else {
		heap_next_seq = heap_size;
	}
	cJSON_Delete(save);
}

static enum savefile_error load_rsave_image(const char *key, const char *path)
{
	char *full_path = savedir_path(path);
	enum savefile_error error;
	struct rsave *save = rsave_read(full_path, RSAVE_READ_ALL, &error);
	free(full_path);
	if (error != SAVEFILE_SUCCESS)
		return error;
	if (!save_names_equal(key, save->key)) {
		rsave_free(save);
		invalid_save_data("Key doesn't match");
	}
	load_rsave_heap(save, key);
	load_rsave_call_stack(save);
	load_rsave_stack(save);
	rsave_free(save);
	return SAVEFILE_SUCCESS;
}

static void repair_evenicle_rance_bonus_skill_string(int slot, const char *text)
{
	if (!string_index_valid(slot) || !heap[slot].s)
		return;
	char *sjis = utf2cjk(text, 0, false);
	struct string *old = heap[slot].s;
	size_t len = strlen(sjis);
	bool matches = old->size == len && !memcmp(old->text, sjis, len);
	free(sjis);
	if (!matches)
		return;

	char *gbk = utf2cjk(text, 0, true);
	char *round_trip = cjk2utf_strict(gbk, true, NULL);
	if (round_trip && !strcmp(round_trip, text) && strcmp(old->text, gbk)) {
		struct string *replacement = make_string(gbk, strlen(gbk));
		heap_string_assign(slot, replacement);
		free_string(replacement);
	}
	free(round_trip);
	free(gbk);
}

static void repair_evenicle_rance_resume_skills(void)
{
	if (!game_evenicle_rance_cn || !sys4_text_is_gbk || sys4_source_is_utf8)
		return;
	int skill_type = ain_get_struct(ain, "S_SkillParam");
	if (skill_type < 0)
		return;
	enum { NUMBER = 2, NAME = 3, DESCRIPTION = 19, LONG_DESCRIPTION = 20, DISPLAY_NAME = 34 };
	const struct ain_struct *type = &ain->structures[skill_type];
	if (type->nr_members <= DISPLAY_NAME || type->members[NUMBER].type.data != AIN_INT)
		return;

	// Match the stored spellings of the two bonus skills, not the save's encoding.
	const struct {
		int number;
		const char *name;
		const char *description;
		const char *long_description;
	} bonus_skills[] = {
		{ 116, "ランスアタック", "5.0倍 全体 必殺技",
			"ランスの発明した、すごい物理攻撃技\n敵全体に５倍の大攻撃" },
		{ 128, "捕獲", "弱った女Ｍを捕獲",
			"女の子モンスターの体力が\n２０％以下の時、まれに捕まえる事が出来る" },
	};
	for (size_t i = 0; i < heap_size; i++) {
		if (!page_index_valid(i))
			continue;
		struct page *page = heap[i].page;
		if (!page || page->type != STRUCT_PAGE || page->index != skill_type || page->nr_vars <= DISPLAY_NAME)
			continue;
		for (size_t j = 0; j < sizeof(bonus_skills) / sizeof(bonus_skills[0]); j++) {
			if (page->values[NUMBER].i != bonus_skills[j].number)
				continue;
			const struct { int member; const char *text; } fields[] = {
				{ NAME, bonus_skills[j].name },
				{ DISPLAY_NAME, bonus_skills[j].name },
				{ DESCRIPTION, bonus_skills[j].description },
				{ LONG_DESCRIPTION, bonus_skills[j].long_description },
			};
			for (size_t k = 0; k < sizeof(fields) / sizeof(fields[0]); k++) {
				int member = fields[k].member;
				if (type->members[member].type.data == AIN_STRING)
					repair_evenicle_rance_bonus_skill_string(page->values[member].i, fields[k].text);
			}
			break;
		}
	}
}

void vm_load_image(const char *key, const char *path)
{
	dg_callbegin_pending = false;
	// First, try to read as a rsave.
	enum savefile_error error = load_rsave_image(key, path);
	switch (error) {
	case SAVEFILE_SUCCESS:
		break;
	case SAVEFILE_INVALID_SIGNATURE:
		// If not a System4 resume save file, read as a json.
		load_json_image(key, path);
		break;
	default:
		VM_ERROR("Failed to read VM image '%s': %s", display_sjis0(path), savefile_strerror(error));
	}
	repair_evenicle_rance_resume_skills();
	modifier_on_config_loaded();
	modifier_on_globals_loaded();
}

static struct page *load_json_image_comments(const char *key, const char *path, int *success)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		*success = 0;
		return NULL;
	}
	cJSON *comments = cJSON_GetObjectItem(save, "comments");
	if (comments)
		type_check(cJSON_Array, comments);
	if (!comments || cJSON_GetArraySize(comments) <= 0) {
		*success = 1;
		cJSON_Delete(save);
		return NULL;
	}

	// read comments from JSON array into VM array
	union vm_value dims = { .i = cJSON_GetArraySize(comments) };
	struct page *array = alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < dims.i; i++) {
		int slot = array->values[i].i;
		free_string(heap[slot].s);
		cJSON *jstr = type_check(cJSON_String, cJSON_GetArrayItem(comments, i));
		char *str = cJSON_GetStringValue(jstr);
		if (!str || !str[0]) {
			heap[slot].s = string_ref(&EMPTY_STRING);
		} else {
			heap[slot].s = make_string(str, strlen(str));
		}
	}

	*success = 1;
	cJSON_Delete(save);
	return array;
}

static struct page *load_rsave_image_comments(const char *key, const char *path, enum savefile_error *error)
{
	char *full_path = savedir_path(path);
	struct rsave *save = rsave_read(full_path, RSAVE_READ_COMMENTS, error);
	free(full_path);
	if (*error != SAVEFILE_SUCCESS)
		return NULL;
	if (!save_names_equal(key, save->key)) {
		rsave_free(save);
		invalid_save_data("Key doesn't match");
	}
	if (save->nr_comments <= 0) {
		rsave_free(save);
		return NULL;
	}
	union vm_value dims = { .i = save->nr_comments };
	struct page *array = alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	bool src_is_gbk = rsave_detect_is_gbk(save, key);
	for (int i = 0; i < dims.i; i++) {
		int slot = array->values[i].i;
		free_string(heap[slot].s);
		if (!save->comments[i] || !save->comments[i][0]) {
			heap[slot].s = string_ref(&EMPTY_STRING);
		} else {
			heap[slot].s = save_decode_vm_string(save->comments[i], strlen(save->comments[i]), src_is_gbk);
		}
	}
	rsave_free(save);
	return array;
}

struct page *vm_load_image_comments(const char *key, const char *path, int *success)
{
	// First, try to read as a rsave.
	enum savefile_error error;
	struct page *page = load_rsave_image_comments(key, path, &error);
	if (error == SAVEFILE_SUCCESS) {
		*success = 1;
		return page;
	}

	if (error == SAVEFILE_INVALID_SIGNATURE) {
		// If not a System4 resume save file, read as a json.
		return load_json_image_comments(key, path, success);
	}
	*success = 0;
	return NULL;
}

static int write_rsave_image_comments(const char *key, const char *path, struct page *comments)
{
	char *full_path = savedir_path(path);
	enum savefile_error error;
	struct rsave *save = rsave_read(full_path, RSAVE_READ_ALL, &error);
	switch (error) {
	case SAVEFILE_SUCCESS:
		if (!save_names_equal(key, save->key)) {
			free(full_path);
			rsave_free(save);
			invalid_save_data("Key doesn't match");
		}
		break;
	case SAVEFILE_FILE_ERROR:
		save = make_rsave(key);
		save->comments_only = true;
		break;
	default:
		free(full_path);
		return 0;
	}

	// Comments were added in RSM v7, so if the save is older, upgrade it.
	if (save->version <= 6) {
		save->version = 7;
	}

	for (int i = 0; i < save->nr_comments; i++) {
		free(save->comments[i]);
	}
	free(save->comments);
	save->nr_comments = comments ? comments->nr_vars : 0;
	save->comments = xcalloc(save->nr_comments, sizeof(char*));
	for (int i = 0; i < save->nr_comments; i++) {
		struct string *cs = string_index_valid(comments->values[i].i)
			? heap_get_string(comments->values[i].i) : &EMPTY_STRING;
		save->comments[i] = xstrdup(cs->text);
	}

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(full_path, "wb", &tmp_path);
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(full_path), strerror(errno));
		free(full_path);
		rsave_free(save);
		return 0;
	}

	bool encrypt = !AIN_VERSION_GTE(ain, 6, 0);
	int compression_level = 1;
	error = rsave_write(save, fp, encrypt, compression_level);
	rsave_free(save);
	if (error != SAVEFILE_SUCCESS) {
		WARNING("Failed to write save file: %s", savefile_strerror(error));
		atomic_save_abort(fp, tmp_path);
		free(full_path);
		return 0;
	}
	bool ok = atomic_save_commit(fp, full_path, tmp_path, false);
	free(full_path);
	return ok ? 1 : 0;
}

static int write_json_image_comments(const char *key, const char *path, struct page *comments)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		// create blank save
		save = cJSON_CreateObject();
		cJSON_AddStringToObject(save, "key", key);
	}
	cJSON_DeleteItemFromObject(save, "comments");

	int nr_comments = comments ? comments->nr_vars : 0;
	cJSON *array = cJSON_CreateArray();
	for (int i = 0; i < nr_comments; i++) {
		struct string *cs = string_index_valid(comments->values[i].i)
			? heap_get_string(comments->values[i].i) : &EMPTY_STRING;
		cJSON *s = cJSON_CreateString(cs->text);
		cJSON_AddItemToArray(array, s);
	}
	cJSON_AddItemToObject(save, "comments", array);

	int ok = save_json(path, save);
	cJSON_Delete(save);
	return ok;
}

int vm_write_image_comments(const char *key, const char *path, struct page *comments)
{
	switch (config.save_format) {
	case SAVE_FORMAT_RSM:
		return write_rsave_image_comments(key, path, comments);
	case SAVE_FORMAT_JSON:
		return write_json_image_comments(key, path, comments);
	}
	return 0;
}
