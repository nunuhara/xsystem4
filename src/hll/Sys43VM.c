/* AIN v12 VM reflection, GPL-2.0-or-later. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "xsystem4.h"
#include "hll.h"
#include "savedata.h"

static bool Sys43VM_WriteGroupSaveComment(struct string *key_name, struct string *file_name, struct string *comment)
{
	if (!key_name || !file_name || !comment || comment->size == 0)
		return false;

	char *path = savedir_path(file_name->text);
	enum savefile_error err;
	struct gsave *gs = gsave_read(path, &err);
	if (!gs) {
		WARNING("%s: %s", display_utf0(path), savefile_strerror(err));
		free(path);
		return false;
	}
	if (!save_names_equal(key_name->text, gs->key)) {
		char expected[256];
		snprintf(expected, sizeof(expected), "%s", gs->key ? gs->key : "");
		gsave_free(gs);
		free(path);
		VM_ERROR("Attempted to write save comment with wrong key: expected '%s', got '%s'",
				display_sjis0(expected), display_sjis1(key_name->text));
	}
	if (gs->comments) {
		for (int32_t i = 0; i < gs->nr_comments; i++) {
			if (gs->comments[i])
				free_string(gs->comments[i]);
		}
		free(gs->comments);
	}
	gs->nr_comments = 1;
	gs->comments = xcalloc(1, sizeof(struct string *));
	gs->comments[0] = string_dup(comment);

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(path), strerror(errno));
		free(path);
		gsave_free(gs);
		return false;
	}

	bool encrypt = !AIN_VERSION_GTE(ain, 6, 0);
	int compression_level = AIN_VERSION_GTE(ain, 6, 0) ? 1 : 9;
	err = gsave_write(gs, fp, encrypt, compression_level);
	gsave_free(gs);
	if (err != SAVEFILE_SUCCESS) {
		WARNING("Failed to write save file: %s", savefile_strerror(err));
		atomic_save_abort(fp, tmp_path);
		free(path);
		return false;
	}
	bool ok = atomic_save_commit(fp, path, tmp_path, false);
	free(path);
	return ok;
}

static bool Sys43VM_ReadGroupSaveComment(struct string *key_name, struct string *file_name, struct string **comment)
{
	if (!key_name || !file_name || !comment)
		return false;
	char *path = savedir_path(file_name->text);
	enum savefile_error err;
	struct gsave *gs = gsave_read(path, &err);
	free(path);
	if (!gs)
		return false;

	if (!save_names_equal(key_name->text, gs->key)) {
		char expected[256];
		snprintf(expected, sizeof(expected), "%s", gs->key ? gs->key : "");
		gsave_free(gs);
		VM_ERROR("Attempted to read save comment with wrong key: expected '%s', got '%s'",
				display_sjis0(expected), display_sjis1(key_name->text));
	}
	if (*comment)
		free_string(*comment);
	if (gs->nr_comments > 0 && gs->comments && gs->comments[0])
		*comment = string_dup(gs->comments[0]);
	else
		*comment = string_ref(&EMPTY_STRING);
	gsave_free(gs);
	return true;
}

static bool Sys43VM_GetFunctionNameList(struct page **out)
{
	if (!out)
		return false;
	union vm_value dim = { .i = ain->nr_functions };
	struct page *list = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < ain->nr_functions; i++) {
		int slot = list->values[i].i;
		free_string(heap[slot].s);
		heap[slot].s = cstr_to_string(ain->functions[i].name);
	}
	if (*out) { delete_page_vars(*out); free_page(*out); }
	*out = list;
	return true;
}

static bool Sys43VM_BeginProfiler(void)
{
	return true;
}

static bool Sys43VM_EndProfiler(void)
{
	return true;
}

static int Sys43VM_GetActualNumofPage(void)
{
	return 0;
}

static int Sys43VM_GetActualMemorySize(void)
{
	return 0;
}

HLL_LIBRARY(Sys43VM,
	HLL_EXPORT(WriteGroupSaveComment, Sys43VM_WriteGroupSaveComment),
	HLL_EXPORT(ReadGroupSaveComment, Sys43VM_ReadGroupSaveComment),
	HLL_EXPORT(GetFunctionNameList, Sys43VM_GetFunctionNameList),
	HLL_EXPORT(BeginProfiler, Sys43VM_BeginProfiler),
	HLL_EXPORT(EndProfiler, Sys43VM_EndProfiler),
	HLL_EXPORT(GetActualNumofPage, Sys43VM_GetActualNumofPage),
	HLL_EXPORT(GetActualMemorySize, Sys43VM_GetActualMemorySize));
