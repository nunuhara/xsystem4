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

#define VM_PRIVATE

#include <stdlib.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "xsystem4.h"

#define HLL_MAX_ARGS 64

struct hll_function {
	void *fun;
	ffi_cif cif;
	unsigned int nr_args;
	ffi_type **args;
	ffi_type *return_type;
	bool warned_unimplemented;
};

static struct hll_function **libraries = NULL;
static volatile int current_hll_libno = -1;
static volatile int current_hll_fno = -1;
static volatile int last_hll_libno = -1;
static volatile int last_hll_fno = -1;

void vm_get_last_hll_call(const char **lib_name, const char **func_name)
{
	int libno = current_hll_libno >= 0 ? current_hll_libno : last_hll_libno;
	int fno = current_hll_libno >= 0 ? current_hll_fno : last_hll_fno;
	if (ain && libno >= 0 && libno < ain->nr_libraries) {
		*lib_name = ain->libraries[libno].name;
		if (fno >= 0 && fno < ain->libraries[libno].nr_functions) {
			*func_name = ain->libraries[libno].functions[fno].name;
			return;
		}
		*func_name = NULL;
		return;
	}
	*lib_name = NULL;
	*func_name = NULL;
}

bool library_exists(int libno)
{
	if (!ain || libno < 0 || libno >= ain->nr_libraries)
		return false;
	if (v12_builtin_library(ain->libraries[libno].name))
		return true;
	return libraries && libraries[libno];
}

bool library_function_exists(int libno, int fno)
{
	if (!ain || libno < 0 || libno >= ain->nr_libraries ||
	    fno < 0 || fno >= ain->libraries[libno].nr_functions)
		return false;
	if (v12_builtin_library(ain->libraries[libno].name))
		return true;
	return libraries && libraries[libno] && libraries[libno][fno].fun;
}

/*
 * Primitive HLL function tracing facility.
 */
//#define TRACE_HLL
#ifdef TRACE_HLL
#include "system4/string.h"

struct traced_library {
	const char *name;
	bool (*should_trace)(struct ain_hll_function *f, union vm_value **args);
};

static bool chipmunk_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"SYSTEM_SetReadMessageSkipping",
		"Update",
		"KeepPreviousView",
		"Sleep",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

#include "parts.h"
#include "parts/parts_internal.h"
static bool gui_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Parts_UpdateMotionTime",
		"UpdateInputState",
		"UpdateComponent",
		"UpdateParts",
		"GetMessageType",
		"ReleaseMessage",
		"Parts_SetPartsPixelDecide",
		"Parts_GetPartsShow",
		"Parts_IsCursorIn",
		"Parts_GetPartsAlpha",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	if (!strcmp(f->name, "Parts_SetPartsCG")) {
		static char *u = NULL;
		if (!u)
			u = utf2sjis("システム／ボタン／メニュー／通常", 0);
		struct string ***strs = (void*)args;
		struct string *s = *strs[1];
		return strcmp(s->text, u);
	}
	if (!strcmp(f->name, "Parts_SetPos")) {
		if (args[1]->i < 0 || args[2]->i < 0)
			return false;
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsY(args[0]->i) != args[2]->i;
	}
	if (!strcmp(f->name, "Parts_SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetShow"))
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetPartsCGSurfaceArea")) {
		return false;
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		Rectangle *r = &p->sprites[args[5]->i].common.surface_area;
		return args[1]->i != r->x || args[2]->i != r->y
			|| args[3]->i != r->w || args[4]->i != r->h;
	}
	if (!strcmp(f->name, "Parts_SetAddColor")) {
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		SDL_Color *c = &p->local.add_color;
		return args[1]->i != c->r || args[2]->i != c->g || args[3]->i != c->b;
	}
	if (!strcmp(f->name, "Parts_GetPartsX"))
		return false;
	return true;
}

static bool parts_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	if (!strcmp(f->name, "Update"))
		return false;
	if (!strcmp(f->name, "SetAddColor"))
		return false;
	if (!strcmp(f->name, "SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsCGSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsConstructionSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsMagX"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsMagY"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsOriginPosMode"))
		return PE_GetPartsOriginPosMode(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsRotateZ"))
		return args[1]->f > 0.001 || args[1]->f < -0.001;
	if (!strcmp(f->name, "SetPos"))
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsX(args[0]->i) != args[2]->i;
	if (!strcmp(f->name, "SetShow")) {
		if (args[0]->i == 0)
			return false; // ???
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	}
	if (!strcmp(f->name, "SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	return true;
}

static bool sact_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Key_IsDown",
		"Mouse_ClearWheel",
		"Mouse_GetPos",
		"Mouse_GetWheel",
		"SP_GetWidth",
		"SP_GetHeight",
		"SP_ExistAlpha",
		"Update",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

struct traced_library traced_libraries[] = {
	{ "ChipmunkSpriteEngine", chipmunk_should_trace },
	{ "GUIEngine", gui_engine_should_trace },
	{ "PartsEngine", parts_engine_should_trace },
	{ "SACTDX", sact_should_trace },
};

static void print_hll_trace(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	sys_message("(%s) ", display_sjis0(ain->functions[call_stack[call_stack_ptr-1].fno].name));
	sys_message("%s.%s(", lib->name, f->name);
	union vm_value **args = _args;
	for (int i = 0; i < f->nr_arguments; i++) {
		if (i > 0) {
			sys_message(", ");
		}
		sys_message("%s=", display_sjis0(f->arguments[i].name));
		switch (f->arguments[i].type.data) {
		case AIN_INT:
		case AIN_LONG_INT:
			sys_message("%d", args[i]->i);
			break;
		case AIN_FLOAT:
			sys_message("%f", args[i]->f);
			break;
		case AIN_STRING: {
			struct string ***strs = _args;
			struct string *s = *strs[i];
			sys_message("\"%s\"", display_sjis0(s->text));
			break;
		}
		case AIN_BOOL:
			sys_message("%s", args[i]->i ? "true" : "false");
			break;
		case AIN_STRUCT:
			sys_message("<struct>");
			break;
		case AIN_ARRAY_TYPE:
			sys_message("<array>");
			break;
		case AIN_REF_TYPE:
			sys_message("<ref>");
			break;
		default:
			sys_message("<%d>", f->arguments[i].type.data);
			break;
		}
	}
	sys_message(")");

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);

	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_INT:
		sys_message(" -> %d", r->i);
		break;
	case AIN_FLOAT:
		sys_message(" -> %f", r->f);
		break;
	case AIN_STRING:
		sys_message(" -> \"%s\"", display_sjis0(((struct string*)r->ref)->text));
		break;
	case AIN_BOOL:
		sys_message(" -> %s", r->i ? "true" : "false");
		break;
	default:
		sys_message(" -> <%d>", f->return_type.data);
		break;
	}
	for (int i = 0; i < f->nr_arguments; i++) {
		union vm_value ***args = _args;
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
			sys_message(" (%s=%d)", display_sjis0(f->arguments[i].name), (*args[i])->i);
			break;
		case AIN_REF_FLOAT:
			sys_message(" (%s=%f)", display_sjis0(f->arguments[i].name), (*args[i])->f);
			break;
		default:
			break;
		}
	}
	sys_message("\n");
}

static void trace_hll_call(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	for (unsigned i = 0; i < sizeof(traced_libraries)/sizeof(*traced_libraries); i++) {
		struct traced_library *l = &traced_libraries[i];
		if (!strcmp(lib->name, l->name)) {
			if (!l->should_trace || l->should_trace(f, _args)) {
				print_hll_trace(lib, f, fun, r, _args);
				return;
			}
			break;
		}
	}

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);
}
#endif /* TRACE_HLL */

void hll_call(int libno, int fno)
{
	if (unlikely(!ain || libno < 0 || libno >= ain->nr_libraries ||
	             fno < 0 || fno >= ain->libraries[libno].nr_functions)) {
		VM_ERROR("Invalid HLL call index: %d.%d", libno, fno);
	}
	int prev_hll_libno = current_hll_libno;
	int prev_hll_fno = current_hll_fno;
	current_hll_libno = libno;
	current_hll_fno = fno;
	last_hll_libno = libno;
	last_hll_fno = fno;
	const char *test_crash = getenv("XSYSTEM4_TEST_CRASH");
	if (test_crash && *test_crash) {
		if (!strcmp(test_crash, "vm_error")) {
			VM_ERROR("Synthetic VM_ERROR crash test in %s.%s", ain->libraries[libno].name, ain->libraries[libno].functions[fno].name);
		} else if (!strcmp(test_crash, "sigsegv")) {
			volatile int *bad_ptr = (volatile int *)0xdead0000ULL;
			*bad_ptr = 42;
		}
	}

	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];
	if (unlikely(f->nr_arguments < 0 || f->nr_arguments > HLL_MAX_ARGS)) {
		VM_ERROR("Too many arguments to HLL function %s.%s: %d",
			ain->libraries[libno].name, f->name, f->nr_arguments);
	}
	struct hll_function *fun = (libraries && libraries[libno]) ? &libraries[libno][fno] : NULL;
	bool unimplemented = !fun || !fun->fun;

	void *args[HLL_MAX_ARGS];
	void *ptrs[HLL_MAX_ARGS];
	// A native function may call back into AIN. Its popped arguments must
	// survive reuse (or reallocation) of the VM stack until finalization.
	union vm_value argument_values[HLL_MAX_ARGS];
	uint64_t dummy_vals[HLL_MAX_ARGS];
	// Copy reference arguments to the stack to protect against heap
	// reallocation during HLL calls.
	void *heap_ptrs[HLL_MAX_ARGS];
	int heap_slots[HLL_MAX_ARGS];

	for (int i = 0; i < f->nr_arguments; i++) {
		heap_slots[i] = -1;
		heap_ptrs[i] = NULL;
		dummy_vals[i] = 0;
	}

	for (int i = f->nr_arguments - 1; i >= 0; i--) {
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
		case AIN_REF_ENUM: {
			// need to create pointer for immediate ref types
			stack_ptr -= 2;
			int pageno = stack[stack_ptr].i;
			int varno  = stack[stack_ptr+1].i;
			if (page_index_valid(pageno) && heap[pageno].page &&
			    varno >= 0 && varno < heap[pageno].page->nr_vars) {
				ptrs[i] = &heap[pageno].page->values[varno];
			} else {
				ptrs[i] = &dummy_vals[i];
			}
			args[i] = &ptrs[i];
			break;
		}
		case AIN_STRING: {
			stack_ptr--;
			int s_slot = stack[stack_ptr].i;
			if (string_index_valid(s_slot) && heap[s_slot].s) {
				args[i] = &heap[s_slot].s;
			} else {
				stack[stack_ptr].i = -1;
				heap_ptrs[i] = &EMPTY_STRING;
				args[i] = &heap_ptrs[i];
			}
			break;
		}
		case AIN_REF_STRING: {
			stack_ptr--;
			int s_slot = stack[stack_ptr].i;
			heap_slots[i] = s_slot;
			heap_ptrs[i] = string_index_valid(s_slot) ? heap[s_slot].s : NULL;
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_STRUCT:
		case AIN_DELEGATE:
		case AIN_ARRAY_TYPE:
		case AIN_ARRAY: {
			stack_ptr--;
			int p_slot = stack[stack_ptr].i;
			if (page_index_valid(p_slot)) {
				args[i] = &heap[p_slot].page;
			} else {
				stack[stack_ptr].i = -1;
				heap_ptrs[i] = NULL;
				args[i] = &heap_ptrs[i];
			}
			break;
		}
		case AIN_REF_STRUCT:
		case AIN_REF_DELEGATE:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY: {
			stack_ptr--;
			int p_slot = stack[stack_ptr].i;
			heap_slots[i] = p_slot;
			heap_ptrs[i] = page_index_valid(p_slot) ? heap[p_slot].page : NULL;
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_WRAP: {
			enum ain_data_type inner = f->arguments[i].type.array_type
				? f->arguments[i].type.array_type->data : AIN_INT;
			switch (inner) {
			case AIN_STRING: {
				stack_ptr--;
				int s_slot = stack[stack_ptr].i;
				heap_slots[i] = s_slot;
				heap_ptrs[i] = string_index_valid(s_slot) ? heap[s_slot].s : NULL;
				ptrs[i] = &heap_ptrs[i];
				args[i] = &ptrs[i];
				break;
			}
			case AIN_STRUCT:
			case AIN_ARRAY:
			case AIN_DELEGATE:
			case AIN_ARRAY_TYPE: {
				stack_ptr--;
				int p_slot = stack[stack_ptr].i;
				heap_slots[i] = p_slot;
				heap_ptrs[i] = page_index_valid(p_slot) ? heap[p_slot].page : NULL;
				ptrs[i] = &heap_ptrs[i];
				args[i] = &ptrs[i];
				break;
			}
			default: {
				stack_ptr -= 2;
				int pageno = stack[stack_ptr].i;
				int varno  = stack[stack_ptr+1].i;
				if (page_index_valid(pageno) && heap[pageno].page &&
				    varno >= 0 && varno < heap[pageno].page->nr_vars) {
					ptrs[i] = &heap[pageno].page->values[varno];
				} else {
					ptrs[i] = &dummy_vals[i];
				}
				args[i] = &ptrs[i];
				break;
			}
			}
			break;
		}
		default:
			stack_ptr--;
			args[i] = &argument_values[i];
			break;
		}
		argument_values[i] = stack[stack_ptr];
	}

	union vm_value r = { .i = 0 };
	if (unimplemented) {
		bool should_warn = false;
		if (fun) {
			if (!fun->warned_unimplemented) {
				fun->warned_unimplemented = true;
				should_warn = true;
			}
		} else {
			static uint64_t warned_null_pairs[128];
			static size_t nr_warned_null_pairs = 0;
			uint64_t key = ((uint64_t)(uint32_t)libno << 32) | (uint32_t)fno;
			bool seen = false;
			for (size_t i = 0; i < nr_warned_null_pairs; i++) {
				if (warned_null_pairs[i] == key) {
					seen = true;
					break;
				}
			}
			if (!seen && nr_warned_null_pairs < sizeof(warned_null_pairs) / sizeof(warned_null_pairs[0])) {
				warned_null_pairs[nr_warned_null_pairs++] = key;
				should_warn = true;
			}
		}
		if (should_warn) {
			WARNING("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);
		}
		switch (f->return_type.data) {
		case AIN_STRING:
			r.ref = string_ref(&EMPTY_STRING);
			break;
		case AIN_ARRAY:
			r.ref = (ain->version >= 11 && f->return_type.array_type)
				? v12_array_new(f->return_type.array_type, 0, false) : NULL;
			break;
		case AIN_ARRAY_TYPE:
			r.ref = NULL;
			break;
		case AIN_STRUCT:
			r.i = alloc_struct(f->return_type.struc);
			break;
		case AIN_WRAP:
		case AIN_REF_TYPE:
			r.i = -1;
			break;
		case AIN_FLOAT:
			r.f = 0.0f;
			break;
		default:
			r.i = 0;
			break;
		}
	} else {
#ifdef TRACE_HLL
		trace_hll_call(&ain->libraries[libno], f, fun, &r, args);
#else
		ffi_call(&fun->cif, (void*)fun->fun, &r, args);
#endif
	}


	for (int i = 0; i < f->nr_arguments; i++) {
		// XXX: We don't increase the ref count when passing ref arguments to HLL
		//      functions, so we need to avoid decreasing it via variable_fini
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
		case AIN_REF_ENUM:
			break;
		case AIN_REF_STRING:
			if (string_index_valid(heap_slots[i])) {
				heap[heap_slots[i]].s = heap_ptrs[i] ? heap_ptrs[i] : string_ref(&EMPTY_STRING);
			} else if (heap_ptrs[i]) {
				free_string(heap_ptrs[i]);
			}
			break;
		case AIN_REF_STRUCT:
		case AIN_REF_DELEGATE:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:
			if (page_index_valid(heap_slots[i])) {
				heap_set_page(heap_slots[i], heap_ptrs[i]);
			} else if (heap_ptrs[i]) {
				delete_page_vars(heap_ptrs[i]);
				free_page(heap_ptrs[i]);
			}
			break;
		case AIN_WRAP: {
			enum ain_data_type inner = f->arguments[i].type.array_type
				? f->arguments[i].type.array_type->data : AIN_INT;
			switch (inner) {
			case AIN_STRING:
				if (string_index_valid(heap_slots[i])) {
					heap[heap_slots[i]].s = heap_ptrs[i] ? heap_ptrs[i] : string_ref(&EMPTY_STRING);
				} else if (heap_ptrs[i]) {
					free_string(heap_ptrs[i]);
				}
				break;
			case AIN_STRUCT:
			case AIN_ARRAY:
			case AIN_DELEGATE:
			case AIN_ARRAY_TYPE:
				if (page_index_valid(heap_slots[i])) {
					heap_set_page(heap_slots[i], heap_ptrs[i]);
				} else if (heap_ptrs[i]) {
					delete_page_vars(heap_ptrs[i]);
					free_page(heap_ptrs[i]);
				}
				break;
			default:
				break;
			}
			break;
		}
		case AIN_REF_FUNC_TYPE:
			break;
		case AIN_ARRAY_TYPE:
			// Sys41VM doesn't make a copy when passing an array by value.
			if (ain->version <= 1)
				break;
			// fallthrough
		default:
			variable_fini(argument_values[i], f->arguments[i].type.data, false);
			break;
		}
	}

	int slot;
	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_STRING:
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = r.ref ? r.ref : string_ref(&EMPTY_STRING);
		stack_push(slot);
		break;
	case AIN_ARRAY:
		stack_push(heap_alloc_page(r.ref));
		break;
	case AIN_BOOL: {
		uint8_t b = 0;
		memcpy(&b, &r, sizeof(b));
		stack_push(b != 0);
		break;
	}
	default:
		stack_push(r);
		break;
	}
	current_hll_libno = prev_hll_libno;
	current_hll_fno = prev_hll_fno;
}

extern struct static_library lib_ACXLoader;
extern struct static_library lib_ACXLoaderP2;
extern struct static_library lib_ADVSYS;
extern struct static_library lib_AFAFactory;
extern struct static_library lib_AliceLogo;
extern struct static_library lib_AliceLogo2;
extern struct static_library lib_AliceLogo3;
extern struct static_library lib_AliceLogo4;
extern struct static_library lib_AliceLogo5;
extern struct static_library lib_AnteaterADVEngine;
extern struct static_library lib_AnteaterADVLogList;
extern struct static_library lib_Array;
extern struct static_library lib_BanMisc;
extern struct static_library lib_Bitarray;
extern struct static_library lib_CalcTable;
extern struct static_library lib_CGManager;
extern struct static_library lib_ChipmunkSpriteEngine;
extern struct static_library lib_ChrLoader;
extern struct static_library lib_CommonSystemData;
extern struct static_library lib_Confirm;
extern struct static_library lib_Confirm2;
extern struct static_library lib_Confirm3;
extern struct static_library lib_CrayfishLogViewer;
extern struct static_library lib_Cursor;
extern struct static_library lib_DALKDemo;
extern struct static_library lib_DALKEDemo;
extern struct static_library lib_Data;
extern struct static_library lib_DataFile;
extern struct static_library lib_Dialog;
extern struct static_library lib_Discord;
extern struct static_library lib_DrawDungeon;
extern struct static_library lib_DrawDungeon2;
extern struct static_library lib_DrawDungeon14;
extern struct static_library lib_DrawEffect;
extern struct static_library lib_DrawField;
extern struct static_library lib_DrawGraph;
extern struct static_library lib_DrawMovie;
extern struct static_library lib_DrawMovie2;
extern struct static_library lib_DrawMovie3;
extern struct static_library lib_DrawNumeral;
extern struct static_library lib_DrawPluginManager;
extern struct static_library lib_DrawRain;
extern struct static_library lib_DrawRipple;
extern struct static_library lib_DrawSimpleText;
extern struct static_library lib_DrawSnow;
extern struct static_library lib_File;
extern struct static_library lib_File2;
extern struct static_library lib_ADVEngine;
extern struct static_library lib_FileDialog;
extern struct static_library lib_FileOperation;
extern struct static_library lib_Clipboard;
extern struct static_library lib_EXWriter;
extern struct static_library lib_TextFile;
extern struct static_library lib_TextFileWriter;
extern struct static_library lib_FillAngle;
extern struct static_library lib_GoatGUIEngine;
extern struct static_library lib_Gpx2Plus;
extern struct static_library lib_GUIEngine;
extern struct static_library lib_HashMap;
extern struct static_library lib_HTTPDownloader;
extern struct static_library lib_IbisInputEngine;
extern struct static_library lib_InputDevice;
extern struct static_library lib_InputString;
extern struct static_library lib_InstallInfo;
extern struct static_library lib_KiwiSoundEngine;
extern struct static_library lib_KiwiSoundEngine_v12;
extern struct static_library lib_LoadCG;
extern struct static_library lib_MADLoader;
extern struct static_library lib_MainEXFile;
extern struct static_library lib_MainSurface;
extern struct static_library lib_MamanyoDemo;
extern struct static_library lib_MamanyoSDemo;
extern struct static_library lib_MarmotModelEngine;
extern struct static_library lib_Math;
extern struct static_library lib_MapLoader;
extern struct static_library lib_MenuMsg;
extern struct static_library lib_MonsterInfo;
extern struct static_library lib_MsgLogManager;
extern struct static_library lib_MsgLogViewer;
extern struct static_library lib_MsgSkip;
extern struct static_library lib_MusicSystem;
extern struct static_library lib_NewFont;
extern struct static_library lib_OutputLog;
extern struct static_library lib_P3MapSprite;
extern struct static_library lib_P3SquareSprite;
extern struct static_library lib_PassRegister;
extern struct static_library lib_PastelChime2;
extern struct static_library lib_PartsEngine;
extern struct static_library lib_PCInfo;
extern struct static_library lib_PixelRestore;
extern struct static_library lib_PlayDemo;
extern struct static_library lib_PlayMovie;
extern struct static_library lib_ReignEngine;
extern struct static_library lib_SACT2;
extern struct static_library lib_SACTDX;
extern struct static_library lib_SealEngine;
extern struct static_library lib_SengokuRanceFont;
extern struct static_library lib_Sound2ex;
extern struct static_library lib_SoundFilePlayer;
extern struct static_library lib_StoatSpriteEngine;
extern struct static_library lib_StretchHelper;
extern struct static_library lib_SystemService;
extern struct static_library lib_Sys43VM;
extern struct static_library lib_SystemServiceEx;
extern struct static_library lib_TapirEngine;
extern struct static_library lib_TextSurfaceManager;
extern struct static_library lib_Timer;
extern struct static_library lib_Toushin3Loader;
extern struct static_library lib_vmAnime;
extern struct static_library lib_vmArray;
extern struct static_library lib_vmCG;
extern struct static_library lib_vmChrLoader;
extern struct static_library lib_vmCursor;
extern struct static_library lib_vmDalkGaiden;
extern struct static_library lib_vmData;
extern struct static_library lib_vmDialog;
extern struct static_library lib_vmDrawGauge;
extern struct static_library lib_vmDrawMsg;
extern struct static_library lib_vmDrawNumber;
extern struct static_library lib_vmFile;
extern struct static_library lib_vmGraph;
extern struct static_library lib_vmGraphQuake;
extern struct static_library lib_vmKey;
extern struct static_library lib_vmMapLoader;
extern struct static_library lib_vmMsgLog;
extern struct static_library lib_vmMsgSkip;
extern struct static_library lib_vmMusic;
extern struct static_library lib_vmSound;
extern struct static_library lib_vmSprite;
extern struct static_library lib_vmString;
extern struct static_library lib_vmSurface;
extern struct static_library lib_vmSystem;
extern struct static_library lib_vmTimer;
extern struct static_library lib_ValueEncryption;
extern struct static_library lib_VSFile;

static struct static_library *static_libraries[] = {
	&lib_ACXLoader,
	&lib_ACXLoaderP2,
	&lib_ADVSYS,
	&lib_AFAFactory,
	&lib_AliceLogo,
	&lib_AliceLogo2,
	&lib_AliceLogo3,
	&lib_AliceLogo4,
	&lib_AliceLogo5,
	&lib_AnteaterADVEngine,
	&lib_AnteaterADVLogList,
	&lib_Array,
	&lib_BanMisc,
	&lib_Bitarray,
	&lib_CalcTable,
	&lib_CGManager,
	&lib_ChipmunkSpriteEngine,
	&lib_ChrLoader,
	&lib_CommonSystemData,
	&lib_Confirm,
	&lib_Confirm2,
	&lib_Confirm3,
	&lib_CrayfishLogViewer,
	&lib_Cursor,
	&lib_DALKDemo,
	&lib_DALKEDemo,
	&lib_Data,
	&lib_DataFile,
	&lib_Dialog,
	&lib_Discord,
	&lib_DrawDungeon,
	&lib_DrawDungeon2,
	&lib_DrawDungeon14,
	&lib_DrawEffect,
	&lib_DrawField,
	&lib_DrawGraph,
	&lib_DrawMovie,
	&lib_DrawMovie2,
	&lib_DrawMovie3,
	&lib_DrawNumeral,
	&lib_DrawPluginManager,
	&lib_DrawRain,
	&lib_DrawRipple,
	&lib_DrawSimpleText,
	&lib_DrawSnow,
	&lib_File,
	&lib_File2,
	&lib_ADVEngine,
	&lib_FileDialog,
	&lib_FileOperation,
	&lib_Clipboard,
	&lib_EXWriter,
	&lib_TextFile,
	&lib_TextFileWriter,
	&lib_FillAngle,
	&lib_GoatGUIEngine,
	&lib_Gpx2Plus,
	&lib_GUIEngine,
	&lib_HashMap,
	&lib_HTTPDownloader,
	&lib_IbisInputEngine,
	&lib_InputDevice,
	&lib_InputString,
	&lib_InstallInfo,
	&lib_KiwiSoundEngine,
	&lib_LoadCG,
	&lib_MADLoader,
	&lib_MainEXFile,
	&lib_MainSurface,
	&lib_MamanyoDemo,
	&lib_MamanyoSDemo,
	&lib_MarmotModelEngine,
	&lib_Math,
	&lib_MapLoader,
	&lib_MenuMsg,
	&lib_MonsterInfo,
	&lib_MsgLogManager,
	&lib_MsgLogViewer,
	&lib_MsgSkip,
	&lib_MusicSystem,
	&lib_NewFont,
	&lib_OutputLog,
	&lib_P3MapSprite,
	&lib_P3SquareSprite,
	&lib_PassRegister,
	&lib_PastelChime2,
	&lib_PartsEngine,
	&lib_PCInfo,
	&lib_PixelRestore,
	&lib_PlayDemo,
	&lib_PlayMovie,
	&lib_ReignEngine,
	&lib_SACT2,
	&lib_SACTDX,
	&lib_SealEngine,
	&lib_SengokuRanceFont,
	&lib_Sound2ex,
	&lib_SoundFilePlayer,
	&lib_StoatSpriteEngine,
	&lib_StretchHelper,
	&lib_SystemService,
	&lib_Sys43VM,
	&lib_SystemServiceEx,
	&lib_TapirEngine,
	&lib_TextSurfaceManager,
	&lib_Timer,
	&lib_Toushin3Loader,
	&lib_vmAnime,
	&lib_vmArray,
	&lib_vmCG,
	&lib_vmChrLoader,
	&lib_vmCursor,
	&lib_vmDalkGaiden,
	&lib_vmData,
	&lib_vmDialog,
	&lib_vmDrawGauge,
	&lib_vmDrawMsg,
	&lib_vmDrawNumber,
	&lib_vmFile,
	&lib_vmGraph,
	&lib_vmGraphQuake,
	&lib_vmKey,
	&lib_vmMapLoader,
	&lib_vmMsgLog,
	&lib_vmMsgSkip,
	&lib_vmMusic,
	&lib_vmSound,
	&lib_vmSprite,
	&lib_vmString,
	&lib_vmSurface,
	&lib_vmSystem,
	&lib_vmTimer,
	&lib_ValueEncryption,
	&lib_VSFile,
	NULL
};

static struct static_library *find_static_library(const char *name)
{
	if (!strcmp(name, "KiwiSoundEngine")) {
		if (ain->version >= 12)
			return &lib_KiwiSoundEngine_v12;
		int libno = ain_get_library(ain, "KiwiSoundEngine");
		if (libno >= 0 && ain_get_library_function(ain, libno, "Prepare") >= 0)
			return &lib_KiwiSoundEngine_v12;
	}
	for (int j = 0; static_libraries[j]; j++)
		if (!strcmp(name, static_libraries[j]->name)) return static_libraries[j];
	return NULL;
}

static ffi_type *ain_to_ffi_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_VOID:
		return &ffi_type_void;
	case AIN_INT:
	case AIN_BOOL:
	case AIN_ENUM:
	case AIN_ENUM2:
		return &ffi_type_sint32;
	case AIN_LONG_INT:
		return &ffi_type_sint64;
	case AIN_FLOAT:
		return &ffi_type_float;
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_FUNC_TYPE:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY:
	case AIN_REF_TYPE:
	case AIN_WRAP:
	case AIN_IMAIN_SYSTEM: // ???
		return &ffi_type_pointer;
	default:
		ERROR("Unhandled type in HLL function: %d", type);
	}
}

static void link_static_library_function(struct hll_function *dst, struct ain_hll_function *src, void *funcptr)
{
	dst->fun = funcptr;
	dst->nr_args = src->nr_arguments;
	dst->args = xcalloc(dst->nr_args, sizeof(ffi_type*));

	for (unsigned int i = 0; i < dst->nr_args; i++) {
		dst->args[i] = ain_to_ffi_type(src->arguments[i].type.data);
	}
	dst->return_type = ain_to_ffi_type(src->return_type.data);

	if (ffi_prep_cif(&dst->cif, FFI_DEFAULT_ABI, dst->nr_args, dst->return_type, dst->args) != FFI_OK)
		ERROR("Failed to link HLL function");
}

/*
 * "Link" a library that has been compiled into the xsystem4 executable.
 */
static struct hll_function *link_static_library(struct ain_library *ainlib, struct static_library *lib)
{
	struct hll_function *dst = xcalloc(ainlib->nr_functions, sizeof(struct hll_function));

	for (int i = 0; i < ainlib->nr_functions; i++) {
		if (ainlib->functions[i].nr_arguments < 0 || ainlib->functions[i].nr_arguments > HLL_MAX_ARGS)
			ERROR("Too many arguments to library function: %s", ainlib->functions[i].name);
		for (int j = 0; lib->functions[j].name; j++) {
			if (!strcmp(ainlib->functions[i].name, lib->functions[j].name)) {
				struct static_hll_function *entry = &lib->functions[j];
				void *function = entry->resolve ? entry->resolve(&ainlib->functions[i]) : entry->fun;
				if (function)
					link_static_library_function(&dst[i], &ainlib->functions[i], function);
				break;
			}
		}
	}

	return dst;
}

static void library_run(struct static_library *lib, const char *name)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
			((void(*)(void))lib->functions[i].fun)();
			break;
		}
	}
}

static void library_run_all(const char *name)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		// These signatures contain generic and callback types handled directly
		// by the VM, not by the C FFI.
		if (v12_builtin_library(ain->libraries[i].name))
			continue;
		struct static_library *library = find_static_library(ain->libraries[i].name);
		if (library) library_run(library, name);
	}
}

static void link_libraries(void)
{
	if (libraries)
		return;

	libraries = xcalloc(ain->nr_libraries, sizeof(struct hll_function*));

	for (int i = 0; i < ain->nr_libraries; i++) {
		if (v12_builtin_library(ain->libraries[i].name))
			continue;
		struct static_library *library = find_static_library(ain->libraries[i].name);
		if (library) libraries[i] = link_static_library(&ain->libraries[i], library);
		if (!libraries[i])
			WARNING("Unimplemented library: %s", ain->libraries[i].name);
	}
}

bool libraries_initialized = false;

#if defined(ALICEHUB_IOS)
struct hll_replacement {
	struct static_hll_function *entry;
	struct static_hll_function original;
	struct hll_replacement *next;
};
static struct hll_replacement *replacements;

void unlink_libraries(void)
{
	if (libraries) {
		for (int i = 0; i < ain->nr_libraries; i++) {
			if (!libraries[i])
				continue;
			for (int j = 0; j < ain->libraries[i].nr_functions; j++)
				free(libraries[i][j].args);
			free(libraries[i]);
		}
		free(libraries);
		libraries = NULL;
	}
	while (replacements) {
		struct hll_replacement *replacement = replacements;
		replacements = replacement->next;
		*replacement->entry = replacement->original;
		free(replacement);
	}
}
#endif

void link_libraries_for_audit(void)
{
	library_run_all("_PreLink");
	link_libraries();
}

void init_libraries(void)
{
	library_run_all("_PreLink");
	link_libraries();
	library_run_all("_ModuleInit");
	libraries_initialized = true;
}

void exit_libraries(void)
{
	if (libraries_initialized) {
		libraries_initialized = false;
		library_run_all("_ModuleFini");
	}
}


void static_library_replace(struct static_library *lib, const char *name, void *fun)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
#if defined(ALICEHUB_IOS)
			struct hll_replacement *replacement = xmalloc(sizeof(*replacement));
			*replacement = (struct hll_replacement){ &lib->functions[i], lib->functions[i], replacements };
			replacements = replacement;
#endif
			lib->functions[i].fun = fun;
			lib->functions[i].resolve = NULL;
			return;
		}
	}
	ERROR("No library function '%s.%s'", lib->name, name);
}
