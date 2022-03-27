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
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define HLL_MAX_ARGS 64

struct hll_function {
	void *fun;
	ffi_cif cif;
	unsigned int nr_args;
	ffi_type **args;
	ffi_type *return_type;
};

static struct hll_function **libraries = NULL;

bool library_exists(int libno)
{
	return libraries[libno];
}

bool library_function_exists(int libno, int fno)
{
	return libraries[libno] && libraries[libno][fno].fun;
}

/*
 * Primitive HLL function tracing facility.
 */
//#define TRACE_HLL
#ifdef TRACE_HLL
#include "system4/string.h"

static void trace_hll_call(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	/*
	// list of traced libraries
	if (!strcmp(lib->name, "StoatSpriteEngine") || !strcmp(lib->name, "ChipmunkSpriteEngine")) {
		// list of excluded functions
		if (!strcmp(f->name, "CharSprite_GetAlphaRate")) goto notrace;
		if (!strcmp(f->name, "CharSprite_SetAlphaRate")) goto notrace;
		if (!strcmp(f->name, "KEY_GetState")) goto notrace;
		if (!strcmp(f->name, "Mouse_ClearWheel")) goto notrace;
		if (!strcmp(f->name, "Mouse_GetPos")) goto notrace;
		if (!strcmp(f->name, "Mouse_GetWheel")) goto notrace;
		if (!strcmp(f->name, "MultiSprite_UpdateView")) goto notrace;
		if (!strcmp(f->name, "SP_ExistAlpha")) goto notrace;
		if (!strcmp(f->name, "SP_GetHeight")) goto notrace;
		if (!strcmp(f->name, "SP_GetWidth")) goto notrace;
		if (!strcmp(f->name, "SP_IsPtIn")) goto notrace;
		if (!strcmp(f->name, "SP_IsPtInRect")) goto notrace;
		if (!strcmp(f->name, "SYSTEM_SetReadMessageSkipping")) goto notrace;
		if (!strcmp(f->name, "TRANS_Update")) goto notrace;
		if (!strcmp(f->name, "Update")) goto notrace;
		else goto notrace;
	}
	else if (!strcmp(lib->name, "VSFile"));
	else if (!strcmp(lib->name, "GoatGUIEngine")) {
		if (!strcmp(f->name, "Update")) goto notrace;
		if (!strcmp(f->name, "IsMotion")) goto notrace;
		if (!strcmp(f->name, "SetMotionTime")) goto notrace;
		if (!strcmp(f->name, "GetClickPartsNumber")) goto notrace;
	}
	//else if (!strcmp(lib->name, "DrawGraph"));
	else if (!strcmp(lib->name, "SACT2")) {
		if (strncmp(f->name, "SP_", 3)) goto notrace;
		if (!strcmp(f->name, "Key_IsDown")) goto notrace;
		if (!strcmp(f->name, "Mouse_GetPos")) goto notrace;
		if (!strcmp(f->name, "SP_ExistAlpha")) goto notrace;
		if (!strcmp(f->name, "SP_IsPtInRect")) goto notrace;
		if (!strcmp(f->name, "SP_IsPtIn")) goto notrace;
		if (!strcmp(f->name, "SP_IsUsing")) goto notrace;
		if (!strcmp(f->name, "SP_SetShow")) goto notrace;
		if (!strcmp(f->name, "SP_SetPos")) goto notrace;
		if (!strcmp(f->name, "Timer_Get")) goto notrace;
		if (!strcmp(f->name, "Update")) goto notrace;
	}
	else*/ if (!strcmp(lib->name, "SengokuRanceFont")) {
		if (!strcmp(f->name, "SP_SetReduceDescender")) goto notrace;
		if (!strcmp(f->name, "SP_ClearState")) goto notrace;
	}
	else goto notrace;

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
	return;
notrace:
	ffi_call(&fun->cif, (void*)fun->fun, r, _args);
}
#endif /* TRACE_HLL */

void hll_call(int libno, int fno)
{
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];

	if (!libraries[libno])
		VM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);

	struct hll_function *fun = &libraries[libno][fno];
	if (!fun->fun)
		VM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);

	// XXX: Try to prevent the heap from being reallocated mid-call.
	//      This only works to the extent that HLL functions can guarantee
	//      no more than 64 heap allocations occur within the call...
	heap_guarantee(64);

	void *args[HLL_MAX_ARGS];
	void *ptrs[HLL_MAX_ARGS];
	for (int i = f->nr_arguments - 1; i >= 0; i--) {
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT: {
			// need to create pointer for immediate ref types
			stack_ptr -= 2;
			int pageno = stack[stack_ptr].i;
			int varno  = stack[stack_ptr+1].i;
			ptrs[i] = &heap[pageno].page->values[varno];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_STRING:
			stack_ptr--;
			args[i] = &heap[stack[stack_ptr].i].s;
			break;
		case AIN_REF_STRING:
			stack_ptr--;
			ptrs[i] = &heap[stack[stack_ptr].i].s;
			args[i] = &ptrs[i];
			break;
		case AIN_STRUCT:
		case AIN_ARRAY_TYPE:
			stack_ptr--;
			args[i] = &heap[stack[stack_ptr].i].page;
			break;
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
			stack_ptr--;
			ptrs[i] = &heap[stack[stack_ptr].i].page;
			args[i] = &ptrs[i];
			break;
		default:
			stack_ptr--;
			args[i] = &stack[stack_ptr];
			break;
		}
	}

	union vm_value r;
#ifdef TRACE_HLL
	trace_hll_call(&ain->libraries[libno], f, fun, &r, args);
#else
	ffi_call(&fun->cif, (void*)fun->fun, &r, args);
#endif


	for (int i = 0, j = 0; i < f->nr_arguments; i++, j++) {
		// XXX: We don't increase the ref count when passing ref arguments to HLL
		//      functions, so we need to avoid decreasing it via variable_fini
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
			j++;
			break;
		case AIN_REF_STRING:
		case AIN_REF_STRUCT:
		case AIN_REF_FUNC_TYPE:
		case AIN_REF_ARRAY_TYPE:
			break;
		default:
			variable_fini(stack[stack_ptr+j], f->arguments[i].type.data);
			break;
		}
	}

	if (f->return_type.data != AIN_VOID) {
		if (f->return_type.data == AIN_STRING) {
			int slot = heap_alloc_slot(VM_STRING);
			heap[slot].s = r.ref;
			stack_push(slot);
		} else {
			stack_push(r);
		}
	}
}

extern struct static_library lib_ACXLoader;
extern struct static_library lib_ADVSYS;
extern struct static_library lib_AliceLogo;
extern struct static_library lib_AliceLogo2;
extern struct static_library lib_AliceLogo3;
extern struct static_library lib_AliceLogo4;
extern struct static_library lib_AliceLogo5;
extern struct static_library lib_AnteaterADVEngine;
extern struct static_library lib_BanMisc;
extern struct static_library lib_Bitarray;
extern struct static_library lib_ChipmunkSpriteEngine;
extern struct static_library lib_ChrLoader;
extern struct static_library lib_CommonSystemData;
extern struct static_library lib_Confirm;
extern struct static_library lib_Confirm2;
extern struct static_library lib_Confirm3;
extern struct static_library lib_CrayfishLogViewer;
extern struct static_library lib_Cursor;
extern struct static_library lib_DataFile;
extern struct static_library lib_DrawDungeon;
extern struct static_library lib_DrawDungeon14;
extern struct static_library lib_DrawGraph;
extern struct static_library lib_DrawMovie2;
extern struct static_library lib_DrawPluginManager;
extern struct static_library lib_DrawSimpleText;
extern struct static_library lib_File;
extern struct static_library lib_FileOperation;
extern struct static_library lib_GoatGUIEngine;
extern struct static_library lib_Gpx2Plus;
extern struct static_library lib_GUIEngine;
extern struct static_library lib_IbisInputEngine;
extern struct static_library lib_InputDevice;
extern struct static_library lib_InputString;
extern struct static_library lib_KiwiSoundEngine;
extern struct static_library lib_LoadCG;
extern struct static_library lib_MainEXFile;
extern struct static_library lib_MainSurface;
extern struct static_library lib_MarmotModelEngine;
extern struct static_library lib_Math;
extern struct static_library lib_MapLoader;
extern struct static_library lib_MenuMsg;
extern struct static_library lib_MonsterInfo;
extern struct static_library lib_MsgLogManager;
extern struct static_library lib_MsgLogViewer;
extern struct static_library lib_MsgSkip;
extern struct static_library lib_OutputLog;
extern struct static_library lib_PassRegister;
extern struct static_library lib_PlayDemo;
extern struct static_library lib_PlayMovie;
extern struct static_library lib_SACT2;
extern struct static_library lib_SACTDX;
extern struct static_library lib_SengokuRanceFont;
extern struct static_library lib_SoundFilePlayer;
extern struct static_library lib_StoatSpriteEngine;
extern struct static_library lib_StretchHelper;
extern struct static_library lib_SystemService;
extern struct static_library lib_SystemServiceEx;
extern struct static_library lib_Timer;
extern struct static_library lib_Toushin3Loader;
extern struct static_library lib_VSFile;

static struct static_library *static_libraries[] = {
	&lib_ACXLoader,
	&lib_ADVSYS,
	&lib_AliceLogo,
	&lib_AliceLogo2,
	&lib_AliceLogo3,
	&lib_AliceLogo4,
	&lib_AliceLogo5,
	&lib_AnteaterADVEngine,
	&lib_BanMisc,
	&lib_Bitarray,
	&lib_ChipmunkSpriteEngine,
	&lib_ChrLoader,
	&lib_CommonSystemData,
	&lib_Confirm,
	&lib_Confirm2,
	&lib_Confirm3,
	&lib_CrayfishLogViewer,
	&lib_Cursor,
	&lib_DataFile,
	&lib_DrawDungeon,
	&lib_DrawDungeon14,
	&lib_DrawGraph,
	&lib_DrawMovie2,
	&lib_DrawPluginManager,
	&lib_DrawSimpleText,
	&lib_File,
	&lib_FileOperation,
	&lib_GoatGUIEngine,
	&lib_Gpx2Plus,
	&lib_GUIEngine,
	&lib_IbisInputEngine,
	&lib_InputDevice,
	&lib_InputString,
	&lib_KiwiSoundEngine,
	&lib_LoadCG,
	&lib_MainEXFile,
	&lib_MainSurface,
	&lib_MarmotModelEngine,
	&lib_Math,
	&lib_MapLoader,
	&lib_MenuMsg,
	&lib_MonsterInfo,
	&lib_MsgLogManager,
	&lib_MsgLogViewer,
	&lib_MsgSkip,
	&lib_OutputLog,
	&lib_PassRegister,
	&lib_PlayDemo,
	&lib_PlayMovie,
	&lib_SACT2,
	&lib_SACTDX,
	&lib_SengokuRanceFont,
	&lib_SoundFilePlayer,
	&lib_StoatSpriteEngine,
	&lib_StretchHelper,
	&lib_SystemService,
	&lib_SystemServiceEx,
	&lib_Timer,
	&lib_Toushin3Loader,
	&lib_VSFile,
	NULL
};

static ffi_type *ain_to_ffi_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_VOID:
		return &ffi_type_void;
	case AIN_INT:
	case AIN_BOOL:
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
	case AIN_REF_TYPE:
	case AIN_IMAIN_SYSTEM: // ???
		return &ffi_type_pointer;
	default:
		ERROR("Unhandled type in HLL function: %s", ain_strtype(ain, type, -1));
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
		for (int j = 0; lib->functions[j].name; j++) {
			if (!strcmp(ainlib->functions[i].name, lib->functions[j].name)) {
				link_static_library_function(&dst[i], &ainlib->functions[i], lib->functions[j].fun);
				break;
			}
		}
		if (!dst[i].fun)
			;//WARNING("Unimplemented library function: %s.%s", ainlib->name, ainlib->functions[i].name);
		else if (ainlib->functions[i].nr_arguments >= HLL_MAX_ARGS)
			ERROR("Too many arguments to library function: %s", ainlib->functions[i].name);
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
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(ain->libraries[i].name, static_libraries[j]->name)) {
				library_run(static_libraries[j], name);
				break;
			}
		}
	}
}

static void link_libraries(void)
{
	if (libraries)
		return;

	libraries = xcalloc(ain->nr_libraries, sizeof(struct hll_function*));

	for (int i = 0; i < ain->nr_libraries; i++) {
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(ain->libraries[i].name, static_libraries[j]->name)) {
				libraries[i] = link_static_library(&ain->libraries[i], static_libraries[j]);
				break;
			}
		}
		if (!libraries[i])
			WARNING("Unimplemented library: %s", ain->libraries[i].name);
	}
}

void init_libraries(void)
{
	link_libraries();
	library_run_all("_ModuleInit");
}

void exit_libraries(void)
{
	library_run_all("_ModuleFini");
}
