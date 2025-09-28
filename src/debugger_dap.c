/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * Based largely on DAP implementation from xsystem35-sdl2
 * Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <SDL_thread.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define VM_PRIVATE

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"
#include "base64.h"
#include "cJSON.h"
#include "debugger.h"
#include "gfx/gfx.h"
#include "input.h"
#include "json.h"
#include "msgqueue.h"
#include "parts/parts_internal.h"
#include "sact.h"
#include "scene.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define THREAD_ID 1

static enum {
	DAP_UNINITIALIZED,
	DAP_INITIALIZED,
	DAP_RUNNING,
	DAP_STOPPED
} dap_state = DAP_UNINITIALIZED;

static enum {
	NOT_STEPPING = 0,
	STEPPING_INTO,
	STEPPING_OVER,
	STEPPING_FINISH
} stepping = NOT_STEPPING;
static int stepping_file = 0;
static int stepping_line = 0;

static struct msgq *queue;

static void json_add_sjis_to_object(cJSON *obj, const char *name, const char *sjis)
{
	char *utf = sjis2utf(sjis, 0);
	cJSON_AddStringToObject(obj, name, utf);
	free(utf);
}

static void send_json(cJSON *json)
{
	static int seq = 1;

	cJSON_AddNumberToObject(json, "seq", seq++);
	char *str = cJSON_PrintUnformatted(json);
	printf("Content-Length: %zu\r\n\r\n%s", strlen(str), str);

	fflush(stdout);
	free(str);
	cJSON_Delete(json);
}

static void send_response(cJSON *response, bool success)
{
	cJSON_AddBoolToObject(response, "success", success);
	send_json(response);
}

static void emit_event(const char *name, cJSON *body)
{
	cJSON *event = cJSON_CreateObject();
	cJSON_AddStringToObject(event, "type", "event");
	cJSON_AddStringToObject(event, "event", name);
	if (body) {
		cJSON_AddItemToObjectCS(event, "body", body);
	}
	send_json(event);
}

static void emit_initialized_event(void)
{
	emit_event("initialized", NULL);
}

static void emit_terminated_event(void)
{
	emit_event("terminated", NULL);
}

static void emit_stopped_event(struct dbg_stop *stop)
{
	cJSON *body = cJSON_CreateObject();
	switch (stop->type) {
	case DBG_STOP_PAUSE:
		cJSON_AddStringToObject(body, "reason", "pause");
		cJSON_AddStringToObject(body, "description", "Paused by user");
		break;
	case DBG_STOP_ERROR:
		cJSON_AddStringToObject(body, "reason", "exception");
		cJSON_AddStringToObject(body, "description", "Paused on error");
		break;
	case DBG_STOP_BREAKPOINT:
		cJSON_AddStringToObject(body, "reason", "breakpoint");
		cJSON_AddStringToObject(body, "description", "Paused on breakpoint");
		break;
	case DBG_STOP_STEP:
		cJSON_AddStringToObject(body, "reason", "step");
		cJSON_AddStringToObject(body, "description", "Paused by step action");
	}
	if (stop->message)
		cJSON_AddStringToObject(body, "text", stop->message);
	cJSON_AddBoolToObject(body, "allThreadsStopped", true);
	emit_event("stopped", body);
}

static void emit_output_event(const char *category, const char *output)
{
	cJSON *body = cJSON_CreateObject();
	cJSON_AddStringToObject(body, "category", category);
	cJSON_AddStringToObject(body, "output", output);
	emit_event("output", body);
}

static void cmd_initialize(cJSON *args, cJSON *resp)
{
	cJSON *body;

	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddBoolToObject(body, "supportsConfigurationDoneRequest", true);
	//cJSON_AddBoolToObject(body, "supportsFunctionBreakpoints", true);
	//cJSON_AddBoolToObject(body, "supportsConditionalBreakpoints", true);
	//cJSON_AddBoolToObject(body, "supportsHitConditionalBreakpoints", true);
	cJSON_AddBoolToObject(body, "supportsEvaluateForHovers", true);
	//cJSON_AddBoolToObject(body, "supportsSetVariable", true);
	//cJSON_AddBoolToObject(body, "supportsSetExpression", true);
	//cJSON_AddBoolToObject(body, "supportsTerminateRequest", true);
	cJSON_AddBoolToObject(body, "supportsInstructionBreakpoints", true);
	cJSON_AddBoolToObject(body, "supportsSteppingGranularity", true);
	send_response(resp, true);

	emit_initialized_event();
	dap_state = DAP_INITIALIZED;
}

static void cmd_launch(cJSON *args, cJSON *resp)
{
	if (dap_state != DAP_INITIALIZED) {
		send_response(resp, false);
		return;
	}

	// TODO: handle noDebug option
	send_response(resp, true);
	dap_state = DAP_RUNNING;
}

static void cmd_configurationDone(cJSON *args, cJSON *resp)
{
	send_response(resp, true);
}

static void cmd_stackTrace(cJSON *args, cJSON *resp)
{
	cJSON *body, *stack_frames, *frame;
	// FIXME: we ignore startFrame/levels arguments
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddNumberToObject(body, "totalFrames", call_stack_ptr);
	cJSON_AddItemToObjectCS(body, "stackFrames", stack_frames = cJSON_CreateArray());
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		cJSON_AddItemToArray(stack_frames, frame = cJSON_CreateObject());
		cJSON_AddNumberToObject(frame, "id", i);
		// this *shouldn't* happen, but since we're in the debugger...
		int fno = call_stack[i].fno;
		if (fno < 0 || fno >= ain->nr_functions) {
			cJSON_AddStringToObject(frame, "name", "<invalid function>");
		} else {
			json_add_sjis_to_object(frame, "name", ain->functions[fno].name);
		}
		char ip[9];
		int addr = (i == call_stack_ptr - 1) ? instr_ptr : call_stack[i+1].call_address;
		snprintf(ip, 9, "%x", (unsigned)addr);
		cJSON_AddStringToObject(frame, "instructionPointerReference", ip);
		int file, line;
		if (dbg_info && dbg_info_addr2line(dbg_info, addr, &file, &line)) {
			cJSON_AddNumberToObject(frame, "line", line);

			const char *fname = dbg_info_source_name(dbg_info, file);
			cJSON *source = cJSON_CreateObject();
			cJSON_AddStringToObject(source, "name", fname);
			char *path = dbg_info_source_path(dbg_info, file);
			cJSON_AddStringToObject(source, "path", path);
			free(path);
			cJSON_AddNumberToObject(source, "sourceReference", 0);
			cJSON_AddItemToObjectCS(frame, "source", source);
		} else {
			cJSON_AddNumberToObject(frame, "line", 0);
		}
		cJSON_AddNumberToObject(frame, "column", 0);
	}
	send_response(resp, true);
}

enum var_ref_type {
	VAR_REF_ARGUMENTS = 0,
	VAR_REF_LOCALS = 1,
	VAR_REF_MEMBERS = 2,
	VAR_REF_GENERIC = 3,
};

static inline unsigned var_ref(unsigned slot, enum var_ref_type type)
{
	return (slot << 4) | type;
}

static inline unsigned var_ref_slot(unsigned var_ref)
{
	return var_ref >> 4;
}

static inline enum var_ref_type var_ref_type(unsigned var_ref)
{
	return var_ref & 0xF;
}

static void cmd_scopes(cJSON *args, cJSON *resp)
{
	if (!args) {
		send_response(resp, false);
		return;
	}

	cJSON *j_id = cJSON_GetObjectItemCaseSensitive(args, "frameId");
	if (!j_id || !cJSON_IsNumber(j_id)) {
		send_response(resp, false);
		return;
	}

	int id = j_id->valueint;
	if (id < 0 || id >= call_stack_ptr) {
		send_response(resp, false);
		return;
	}

	if (call_stack[id].fno < 0 || call_stack[id].fno >= ain->nr_functions) {
		send_response(resp, false);
		return;
	}

	cJSON *body, *scopes, *arg_scope, *loc_scope, *mem_scope;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(body, "scopes", scopes = cJSON_CreateArray());

	int l_page = call_stack[id].page_slot;
	if (page_index_valid(l_page) && heap_get_page(l_page)->type == LOCAL_PAGE) {
		int arg_ref = var_ref(l_page, VAR_REF_ARGUMENTS);
		int loc_ref = var_ref(l_page, VAR_REF_LOCALS);
		cJSON_AddItemToArray(scopes, arg_scope = cJSON_CreateObject());
		cJSON_AddStringToObject(arg_scope, "name", "Arguments");
		cJSON_AddStringToObject(arg_scope, "presentationHint", "arguments");
		cJSON_AddNumberToObject(arg_scope, "variablesReference", arg_ref);
		cJSON_AddItemToArray(scopes, loc_scope = cJSON_CreateObject());
		cJSON_AddStringToObject(loc_scope, "name", "Locals");
		cJSON_AddStringToObject(loc_scope, "presentationHint", "locals");
		cJSON_AddNumberToObject(loc_scope, "variablesReference", loc_ref);
	}

	int s_page = call_stack[id].struct_page;
	if (page_index_valid(s_page) && heap_get_page(s_page)->type == STRUCT_PAGE) {
		int mem_ref = var_ref(s_page, VAR_REF_MEMBERS);
		cJSON_AddItemToArray(scopes, mem_scope = cJSON_CreateObject());
		cJSON_AddStringToObject(mem_scope, "name", "Members");
		cJSON_AddStringToObject(mem_scope, "presentationHint", "locals");
		cJSON_AddNumberToObject(mem_scope, "variablesReference", mem_ref);
	}

	// TODO: stack values?

	send_response(resp, true);
}

static bool type_is_structured(struct ain_type *type)
{
	switch (type->data) {
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_ARRAY_TYPE:
	case AIN_REF_ARRAY_TYPE:
		return true;
	default:
		return false;
	}
}

static int make_var_ref(struct ain_type *type, union vm_value value)
{
	if (type_is_structured(type) && page_index_valid(value.i))
		return var_ref(value.i, VAR_REF_GENERIC);
	return 0;
}

static cJSON *value_to_json(const char *name, struct ain_type *type, struct page *page, int slot)
{
	char *s_type = ain_strtype_d(ain, type);
	struct string *s_val = dbg_variable_to_string(type, page, slot, 0);

	cJSON *json = cJSON_CreateObject();
	json_add_sjis_to_object(json, "name", name);
	json_add_sjis_to_object(json, "value", s_val->text);
	json_add_sjis_to_object(json, "type", s_type);
	cJSON_AddNumberToObject(json, "variablesReference", make_var_ref(type, page->values[slot]));

	free_string(s_val);
	free(s_type);
	return json;
}

static void add_var_to_json_array(cJSON *array, struct ain_variable *var, struct page *page, int slot)
{
	if (var->type.data == AIN_VOID)
		return;
	cJSON_AddItemToArray(array, value_to_json(var->name, &var->type, page, slot));
}

static void add_arguments_to_array(cJSON *array, struct page *page)
{
	struct ain_function *f = &ain->functions[page->index];
	for (int i = 0; i < f->nr_args; i++) {
		add_var_to_json_array(array, &f->vars[i], page, i);
	}
}

static void add_locals_to_array(cJSON *array, struct page *page)
{
	struct ain_function *f = &ain->functions[page->index];
	for (int i = f->nr_args; i < f->nr_vars; i++) {
		add_var_to_json_array(array, &f->vars[i], page, i);
	}
}

static void add_variables_to_array(cJSON *array, struct page *page)
{
	struct ain_function *f = &ain->functions[page->index];
	for (int i = 0; i < f->nr_vars; i++) {
		add_var_to_json_array(array, &f->vars[i], page, i);
	}
}

static void add_members_to_array(cJSON *array, struct page *page)
{
	struct ain_struct *s = &ain->structures[page->index];
	for (int i = 0; i < s->nr_members; i++) {
		add_var_to_json_array(array, &s->members[i], page, i);
	}
}

static void add_globals_to_array(cJSON *array, struct page *page)
{
	for (int i = 0; i < ain->nr_globals; i++) {
		add_var_to_json_array(array, &ain->globals[i], page, i);
	}
}

static void add_array_elements_to_array(cJSON *array, struct page *page)
{
	if (!page)
		return;

	for (int i = 0; i < page->nr_vars; i++) {
		char name[512];
		snprintf(name, 512, "[%d]", i);
		struct ain_type type;
		type.data = variable_type(page, i, &type.struc, &type.rank);
		cJSON_AddItemToArray(array, value_to_json(name, &type, page, i));
	}
}

static void add_page_to_array(cJSON *array, struct page *page)
{
	switch (page->type) {
	case GLOBAL_PAGE:
		add_globals_to_array(array, page);
		break;
	case LOCAL_PAGE:
		add_variables_to_array(array, page);
		break;
	case STRUCT_PAGE:
		add_members_to_array(array, page);
		break;
	case ARRAY_PAGE:
		add_array_elements_to_array(array, page);
		break;
	default:
		break;
	}
}

static cJSON *var_ref_to_json(struct page *page, enum var_ref_type type)
{
	cJSON *vars = cJSON_CreateArray();
	if (type == VAR_REF_ARGUMENTS) {
		add_arguments_to_array(vars, page);
	} else if (type == VAR_REF_LOCALS) {
		add_locals_to_array(vars, page);
	} else if (type == VAR_REF_MEMBERS) {
		add_members_to_array(vars, page);
	} else if (type == VAR_REF_GENERIC) {
		add_page_to_array(vars, page);
	} else {
		cJSON_Delete(vars);
		return NULL;
	}
	return vars;
}

static void cmd_variables(cJSON *args, cJSON *resp)
{
	if (!args) {
		send_response(resp, false);
		return;
	}

	cJSON *j_ref = cJSON_GetObjectItemCaseSensitive(args, "variablesReference");
	if (!j_ref || !cJSON_IsNumber(j_ref)) {
		send_response(resp, false);
		return;
	}

	int slot = var_ref_slot(j_ref->valueint);
	enum var_ref_type type = var_ref_type(j_ref->valueint);
	if (!page_index_valid(slot)) {
		send_response(resp, false);
		return;
	}

	struct page *page = heap_get_page(slot);

	// FIXME: filter/start/count are ignored

	cJSON *vars = var_ref_to_json(page, type);
	if (!vars) {
		send_response(resp, false);
		return;
	}

	cJSON *body = cJSON_CreateObject();
	cJSON_AddItemToObjectCS(body, "variables", vars);
	cJSON_AddItemToObjectCS(resp, "body", body);
	send_response(resp, true);
}

void delete_breakpoints_in_file(int addr, struct breakpoint *bp, void *file_)
{
	int file = *(int *)file_;
	int f;
	if (dbg_info_addr2line(dbg_info, addr, &f, NULL) && f == file) {
		dbg_clear_breakpoint(addr, NULL);
	}
}

static void cmd_setBreakpoints(cJSON *args, cJSON *resp)
{
	if (!dbg_info) {
		cJSON_AddStringToObject(resp, "message", "no debug info available");
		send_response(resp, false);
		return;
	}
	cJSON *source = cJSON_GetObjectItemCaseSensitive(args, "source");
	cJSON *source_path = cJSON_GetObjectItemCaseSensitive(source, "path");
	cJSON *req_bps = cJSON_GetObjectItemCaseSensitive(args, "breakpoints");
	if (!cJSON_IsString(source_path) || !cJSON_IsArray(req_bps)) {
		cJSON_AddStringToObject(resp, "message", "invalid arguments");
		send_response(resp, false);
		return;
	}
	const char *path = source_path->valuestring;
	int file = dbg_info_find_file(dbg_info, path);
	if (file < 0) {
		cJSON_AddStringToObject(resp, "message", "no such file");
		send_response(resp, false);
		return;
	}

	dbg_foreach_breakpoint(delete_breakpoints_in_file, &file);

	cJSON *body, *resp_bps;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(body, "breakpoints", resp_bps = cJSON_CreateArray());

	char message[256];
	cJSON *req_bp;
	cJSON_ArrayForEach(req_bp, req_bps) {
		cJSON *resp_bp = cJSON_CreateObject();
		cJSON_AddItemToArray(resp_bps, resp_bp);

		cJSON *line = cJSON_GetObjectItemCaseSensitive(req_bp, "line");
		int line_no = line->valueint;
		int addr = dbg_info_line2addr(dbg_info, file, line_no);
		if (addr < 0) {
			snprintf(message, sizeof(message), "no line %d in %s", line_no, path);
			cJSON_AddBoolToObject(resp_bp, "verified", false);
			cJSON_AddStringToObject(resp_bp, "message", message);
			continue;
		}
		bool verified = dbg_set_address_breakpoint(addr, NULL, NULL);
		if (!verified) {
			snprintf(message, sizeof(message), "failed to set breakpoint at 0x%x", addr);
			cJSON_AddBoolToObject(resp_bp, "verified", false);
			cJSON_AddStringToObject(resp_bp, "message", message);
			continue;
		}

		dbg_info_addr2line(dbg_info, addr, NULL, &line_no);
		cJSON_AddNumberToObject(resp_bp, "id", addr);
		cJSON_AddBoolToObject(resp_bp, "verified", true);
		cJSON_AddNumberToObject(resp_bp, "line", line_no);
	}

	send_response(resp, true);
}

static void cmd_setInstructionBreakpoints(cJSON *args, cJSON *resp)
{
	static uint32_t *old_breakpoints = NULL;
	static int nr_old_breakpoints = 0;

	uint32_t *addresses = NULL;
	if (!args) {
		goto fail;
	}

	cJSON *j_bp = cJSON_GetObjectItemCaseSensitive(args, "breakpoints");
	if (!j_bp || !cJSON_IsArray(j_bp)) {
		goto fail;
	}

	int nr_breakpoints = cJSON_GetArraySize(j_bp);
	addresses = xcalloc(nr_breakpoints, sizeof(uint32_t));

	int i;
	cJSON *e;
	cJSON_ArrayForEachIndex(i, e, j_bp) {
		if (!cJSON_IsObject(e)) {
			goto fail;
		}
		cJSON *ref = cJSON_GetObjectItemCaseSensitive(e, "instructionReference");
		if (!ref || !cJSON_IsString(ref)) {
			goto fail;
		}
		char *endptr;
		addresses[i] = strtol(ref->valuestring, &endptr, 16);
		if (ref->valuestring[0] == '\0' || *endptr != '\0') {
			goto fail;
		}
		// FIXME: offset is ignored
		// TODO: condition
		// TODO: hitCondition
	}

	// clear old breakpoints
	for (int i = 0; i < nr_old_breakpoints; i++) {
		dbg_clear_breakpoint(old_breakpoints[i], NULL);
	}

	cJSON *body, *breakpoints, *bp;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(body, "breakpoints", breakpoints = cJSON_CreateArray());
	for (int i = 0; i < nr_breakpoints; i++) {
		char s_addr[9];
		snprintf(s_addr, 9, "%x", addresses[i]);
		cJSON_AddItemToArray(breakpoints, bp = cJSON_CreateObject());
		cJSON_AddStringToObject(bp, "instructionReference", s_addr);
		bool verified = dbg_set_address_breakpoint(addresses[i], NULL, NULL);
		cJSON_AddBoolToObject(bp, "verified", verified);
		// TODO: id, message, offset
	}

	// save new breakpoints list
	free(old_breakpoints);
	old_breakpoints = addresses;
	nr_old_breakpoints = nr_breakpoints;

	send_response(resp, true);
	return;
fail:
	free(addresses);
	send_response(resp, false);
}

static void cmd_evaluate(cJSON *args, cJSON *resp)
{
	if (!args) {
		send_response(resp, false);
		return;
	}

	cJSON *j_expr = cJSON_GetObjectItemCaseSensitive(args, "expression");
	if (!j_expr || !cJSON_IsString(j_expr)) {
		send_response(resp, false);
		return;
	}

	unsigned old_frame = dbg_current_frame;
	cJSON *j_id = cJSON_GetObjectItemCaseSensitive(args, "frameId");
	if (j_id && cJSON_IsNumber(j_id)) {
		int frame_id = j_id->valueint;
		if (frame_id >= 0 && frame_id < call_stack_ptr) {
			dbg_current_frame = call_stack_ptr - 1 - frame_id;
		}
	}

	struct ain_type type;
	char *sjis_expr = utf2sjis(j_expr->valuestring, 0);
	union vm_value val = dbg_eval_string(sjis_expr, &type);
	free(sjis_expr);

	dbg_current_frame = old_frame;

	struct string *s_val = dbg_value_to_string(&type, val, 0);
	char *s_type = ain_strtype_d(ain, &type);

	cJSON *body;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	json_add_sjis_to_object(body, "result", s_val->text);
	json_add_sjis_to_object(body, "type", s_type);
	cJSON_AddNumberToObject(body, "variablesReference", make_var_ref(&type, val));
	// TODO: presentationHint

	send_response(resp, true);
}

static void cmd_continue(cJSON *args, cJSON *resp)
{
	send_response(resp, true);

	stepping = NOT_STEPPING;
	stepping_file = stepping_line = 0;
	dap_state = DAP_RUNNING;
	dbg_continue();
}

static void cmd_pause(cJSON *args, cJSON *resp)
{
	if (dap_state < DAP_RUNNING) {
		cJSON_AddBoolToObject(resp, "success", false);
		return;
	}

	cJSON_AddBoolToObject(resp, "success", true);
	send_json(resp);

	if (dap_state != DAP_STOPPED) {
		dbg_repl(DBG_STOP_PAUSE, NULL);
	}
}

enum stepping_granularity {
	STEPPING_GRANULARITY_STATEMENT,
	STEPPING_GRANULARITY_LINE,
	STEPPING_GRANULARITY_INSTRUCTION,
};

static enum stepping_granularity get_granularity(cJSON *args)
{
	cJSON *g = cJSON_GetObjectItemCaseSensitive(args, "granularity");
	if (cJSON_IsString(g)) {
		if (!strcmp(g->valuestring, "line"))
			return STEPPING_GRANULARITY_LINE;
		if (!strcmp(g->valuestring, "instruction"))
			return STEPPING_GRANULARITY_INSTRUCTION;
	}
	return STEPPING_GRANULARITY_STATEMENT;
}

static void cmd_stepIn(cJSON *args, cJSON *resp)
{
	stepping_file = stepping_line = 0;
	if (dbg_info && get_granularity(args) != STEPPING_GRANULARITY_INSTRUCTION)
		dbg_info_addr2line(dbg_info, instr_ptr, &stepping_file, &stepping_line);

	dbg_set_step_into_breakpoint();
	send_response(resp, true);

	dap_state = DAP_RUNNING;
	stepping = STEPPING_INTO;
	dbg_continue();
}

static void cmd_stepOut(cJSON *args, cJSON *resp)
{
	dbg_set_finish_breakpoint();
	send_response(resp, true);

	dap_state = DAP_RUNNING;
	stepping = STEPPING_FINISH;
	dbg_continue();
}

static void cmd_next(cJSON *args, cJSON *resp)
{
	stepping_file = stepping_line = 0;
	if (dbg_info && get_granularity(args) != STEPPING_GRANULARITY_INSTRUCTION)
		dbg_info_addr2line(dbg_info, instr_ptr, &stepping_file, &stepping_line);

	dbg_set_step_over_breakpoint();
	send_response(resp, true);

	dap_state = DAP_RUNNING;
	stepping = STEPPING_OVER;
	dbg_continue();
}

static void cmd_threads(cJSON *args, cJSON *resp)
{
	cJSON *body, *threads, *thread;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(body, "threads", threads = cJSON_CreateArray());
	cJSON_AddItemToArray(threads, thread = cJSON_CreateObject());
	cJSON_AddNumberToObject(thread, "id", 0);
	cJSON_AddStringToObject(thread, "name", "main_thread");
	send_response(resp, true);
}

static void cmd_setVariable(cJSON *args, cJSON *resp)
{
	// TODO
	send_response(resp, false);
}

static void cmd_disconnect(cJSON *args, cJSON *resp)
{
	send_response(resp, true);
	vm_exit(0);
}

// get scene metadata
static void cmd_xsystem4_scene(cJSON *args, cJSON *resp)
{
	cJSON *body;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(body, "entities", scene_to_json(true));
	send_response(resp, true);
}

static bool get_id(cJSON *args, const char *id_name, int *id_out)
{
	if (!args)
		return false;

	cJSON *j_id = cJSON_GetObjectItemCaseSensitive(args, id_name);
	if (!j_id || !cJSON_IsNumber(j_id))
		return false;

	*id_out = j_id->valueint;
	return true;
}

// render a scene entity (using sprite->render())
static void cmd_xsystem4_renderEntity(cJSON *args, cJSON *resp)
{
	int id;
	struct sprite *sp;

	if (!get_id(args, "entityId", &id) || !(sp = scene_get(id))) {
		send_response(resp, false);
		return;
	}

	// create output texture
	Texture t;
	gfx_init_texture_rgb(&t, config.view_width, config.view_height, (SDL_Color){0,0,0});

	// render entity to texture
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &t, 0, 0, t.w, t.h);
	sp->render(sp);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);

	// send response
	cJSON *body;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddNumberToObject(body, "entityId", id);
	cJSON_AddItemToObjectCS(body, "texture", texture_to_json_with_pixels(&t));
	send_response(resp, true);
	gfx_delete_texture(&t);
}

// get the stored texture data of a sact_sprite
static void cmd_xsystem4_spriteTexture(cJSON *args, cJSON *resp)
{
	int id;
	struct sact_sprite *sp;

	if (!get_id(args, "spriteId", &id) || !(sp = sact_try_get_sprite(id)) || !sp->texture.handle) {
		send_response(resp, false);
		return;
	}

	cJSON *body;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddNumberToObject(body, "spriteId", id);
	cJSON_AddItemToObjectCS(body, "texture", texture_to_json_with_pixels(&sp->texture));
	if (sp->text.texture.handle) {
		cJSON_AddItemToObjectCS(body, "textTexture", texture_to_json_with_pixels(&sp->text.texture));
	}
	send_response(resp, true);
}

static struct parts *get_parts(cJSON *args, int *id_out)
{
	struct parts *parts;

	if (!get_id(args, "partsId", id_out)) {
		WARNING("get_id failed");
		return NULL;
	}
	if (!(parts = parts_try_get(*id_out))) {
		WARNING("parts_try_get(%d) failed", *id_out);
		return NULL;
	}
	return parts;
}

static void draw_rectangle(Texture *t, Color c, Rectangle r)
{
	gfx_draw_line(t, r.x, r.y, r.x + r.w, r.y, c.r, c.g, c.b);
	gfx_draw_line(t, r.x, r.y + r.h, r.x + r.w, r.y + r.h, c.r, c.g, c.b);
	gfx_draw_line(t, r.x, r.y, r.x, r.y + r.h, c.r, c.g, c.b);
	gfx_draw_line(t, r.x + r.w, r.y, r.x + r.w, r.y + r.h, c.r, c.g, c.b);
}

// render a parts object
static void cmd_xsystem4_renderParts(cJSON *args, cJSON *resp)
{
	int parts_id;
	struct parts *parts = get_parts(args, &parts_id);
	if (!parts) {
		send_response(resp, false);
		return;
	}

	// create output texture, with darkened grayscale main surface as background
	Texture t;
	gfx_init_texture_rgb(&t, config.view_width, config.view_height, (SDL_Color){0,0,0});
	gfx_copy_grayscale(&t, 0, 0, gfx_main_surface(), 0, 0, t.w, t.h);
	gfx_fill_multiply(&t, 0, 0, t.w, t.h, 20, 20, 20);

	// render parts to textre
	GLuint fbo = gfx_set_framebuffer(GL_FRAMEBUFFER, &t, 0, 0, t.w, t.h);
	parts_render_family(parts);
	gfx_reset_framebuffer(GL_FRAMEBUFFER, fbo);

	struct parts_common *state = &parts->states[parts->state].common;
	// draw bounds
	draw_rectangle(&t, (Color){255,0,0}, (Rectangle) {
		parts->global.pos.x + state->origin_offset.x,
		parts->global.pos.y + state->origin_offset.y,
		state->w,
		state->h
	});
	// draw origin
	draw_rectangle(&t, (Color){0,255,0}, (Rectangle) {
		parts->global.pos.x - 1,
		parts->global.pos.y - 1,
		3,
		3
	});

	// send response
	cJSON *body;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddNumberToObject(body, "partsId", parts_id);
	cJSON_AddItemToObjectCS(body, "texture", texture_to_json_with_pixels(&t));
	send_response(resp, true);
	gfx_delete_texture(&t);
}

// get the stored texture data of a parts object
static void cmd_xsystem4_partsTexture(cJSON *args, cJSON *resp)
{
	int parts_id;
	struct parts *parts = get_parts(args, &parts_id);;
	if (!parts) {
		send_response(resp, false);
		return;
	}

	Texture *t = &parts->states[parts->state].common.texture;

	cJSON *body;
	cJSON_AddItemToObjectCS(resp, "body", body = cJSON_CreateObject());
	cJSON_AddNumberToObject(body, "partsId", parts_id);
	cJSON_AddItemToObjectCS(body, "texture", texture_to_json_with_pixels(t));
	send_response(resp, true);
}

static struct {
	const char *name;
	void (*fun)(cJSON *args, cJSON *resp);
	bool continue_repl;
} commands[] = {
	{ "initialize", cmd_initialize, true },
	{ "launch", cmd_launch, true },
	{ "configurationDone", cmd_configurationDone, true },
	{ "continue", cmd_continue, false },
	{ "stackTrace", cmd_stackTrace, true },
	{ "stepIn", cmd_stepIn, false },
	{ "stepOut", cmd_stepOut, false },
	{ "next", cmd_next, false },
	{ "pause", cmd_pause, true },
	{ "evaluate", cmd_evaluate, true },
	{ "setBreakpoints", cmd_setBreakpoints, true },
	{ "setInstructionBreakpoints", cmd_setInstructionBreakpoints, true },
	{ "threads", cmd_threads, true },
	{ "scopes", cmd_scopes, true },
	{ "variables", cmd_variables, true },
	{ "setVariable", cmd_setVariable, true },
	{ "disconnect", cmd_disconnect, true },
	// DAP extensions
	{ "xsystem4.scene", cmd_xsystem4_scene, true },
	{ "xsystem4.renderEntity", cmd_xsystem4_renderEntity, true },
	{ "xsystem4.spriteTexture", cmd_xsystem4_spriteTexture, true },
	{ "xsystem4.renderParts", cmd_xsystem4_renderParts, true },
	{ "xsystem4.partsTexture", cmd_xsystem4_partsTexture, true },
};

static bool handle_request(cJSON *request)
{
	cJSON *resp = cJSON_CreateObject();
	cJSON_AddStringToObject(resp, "type", "response");
	cJSON *request_seq = cJSON_DetachItemFromObjectCaseSensitive(request, "seq");
	cJSON_AddItemToObjectCS(resp, "request_seq", request_seq);
	cJSON *command = cJSON_DetachItemFromObjectCaseSensitive(request, "command");
	cJSON_AddItemToObjectCS(resp, "command", command);
	cJSON *args = cJSON_GetObjectItemCaseSensitive(request, "arguments");

	if (!cJSON_IsString(command)) {
		WARNING("protocol error: command is not a string");
		cJSON_Delete(resp);
		return true;
	}

	for (unsigned i = 0; i < sizeof(commands) / sizeof(*commands); i++) {
		if (!strcmp(command->valuestring, commands[i].name)) {
			commands[i].fun(args, resp);
			return commands[i].continue_repl;
		}
	}

	WARNING("unknown command \"%s\"", command->valuestring);
	return true;
}

static bool handle_message(char *msg)
{
	cJSON *json = cJSON_Parse(msg);
	cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");
	bool continue_repl = true;
	if (cJSON_IsString(type) && !strcmp(type->valuestring, "request"))
		continue_repl = handle_request(json);
	cJSON_Delete(json);
	free(msg);
	return continue_repl;
}

static int read_command_thread(void *data)
{
	int content_length = -1;
	char header[512];
	while (fgets(header, sizeof(header), stdin)) {
		if (sscanf(header, "Content-Length: %d", &content_length) == 1) {
			continue;
		} else if ((header[0] == '\r' && header[1] == '\n') || header[0] == '\n') {
			if (content_length < 0) {
				WARNING("Debug Adapter Protocol error: no Content-Length header");
				continue;
			}
			char *buf = malloc(content_length);
			if (fread(buf, content_length, 1, stdin) != 1) {
				WARNING("fread(stdin): %s", strerror(errno));
				free(buf);
				continue;
			}
			msgq_enqueue(queue, buf);
			content_length = -1;
		} else {
			WARNING("Unknown Debug Adapter Protocol header: %s", header);
		}
	}
	msgq_enqueue(queue, NULL); // EOF
	return 0;
}

void dbg_dap_init(void)
{
	atexit(dbg_dap_quit);
	queue = msgq_new();

#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	SDL_CreateThread(read_command_thread, "Debugger", NULL);

	while (dap_state < DAP_RUNNING) {
		char *msg = msgq_dequeue(queue);
		if (!msg)
			break;
		handle_message(msg);
	}
}

void dbg_dap_quit(void)
{
	emit_terminated_event();
}

void dbg_dap_repl(struct dbg_stop *stop)
{
	if (stepping) {
		if (stepping_line) {
			int file, line;
			dbg_info_addr2line(dbg_info, instr_ptr, &file, &line);
			if (file == stepping_file && line == stepping_line) {
				if (stepping == STEPPING_INTO)
					dbg_set_step_into_breakpoint();
				else if (stepping == STEPPING_OVER)
					dbg_set_step_over_breakpoint();
				dbg_continue();
			}
		}
		stepping = NOT_STEPPING;
		stepping_file = stepping_line = 0;
	}

	emit_stopped_event(stop);
	dap_state = DAP_STOPPED;

	bool continue_repl = true;
	while (continue_repl) {
		handle_window_events();
		char *msg = msgq_dequeue_timeout(queue, 50);
		if (!msg)
			continue;
		continue_repl = handle_message(msg);
	}
}

void dbg_dap_handle_messages(void)
{
	while (!msgq_isempty(queue)) {
		char *msg = msgq_dequeue(queue);
		if (!msg)
			break;
		handle_message(msg);
	}
}

void dbg_dap_log(const char *log, const char *fmt, va_list ap)
{
	size_t len = max(1024, strlen(fmt) * 2);
	char *buf = xmalloc(len);
	vsnprintf(buf, len, fmt, ap);
	emit_output_event(log, buf);
	free(buf);
}
