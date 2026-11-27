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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <SDL.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "savedata.h"
#include "vm.h"
#include "xsystem4.h"

#include "hll.h"

static bool enabled = false;
static int state = 0;

static char *save_path;
static uint8_t *flags;
static int nr_flags;
static bool save_hook_registered = false;

struct adv_skip_entry {
	char *name;
	int version;
	int step;
};
static struct adv_skip_entry *adv_flags;
static size_t nr_adv_flags;

static void free_adv_flags(void)
{
	for (size_t i = 0; i < nr_adv_flags; i++)
		free(adv_flags[i].name);
	free(adv_flags);
	adv_flags = NULL;
	nr_adv_flags = 0;
}

static void msgskip_save(void)
{
	if (!save_path || !flags)
		return;

	char *tmp_path = NULL;
	FILE *f = atomic_save_open(save_path, "wb", &tmp_path);
	if (!f) {
		WARNING("fopen: '%s': %s", display_utf0(save_path), strerror(errno));
		return;
	}

	uint8_t header[4];
	LittleEndian_putDW(header, 0, nr_flags);
	size_t bytes = (size_t)((nr_flags + 7) / 8);
	bool ok = fwrite(header, sizeof(header), 1, f) == 1 &&
		(bytes == 0 || fwrite(flags, bytes, 1, f) == 1);
	if (!ok) {
		atomic_save_abort(f, tmp_path);
		return;
	}
	atomic_save_commit(f, save_path, tmp_path, false);
}

#if defined(__ANDROID__)
static int msgskip_event_handler(possibly_unused void *user_data, SDL_Event *e) {
	switch (e->type) {
	case SDL_APP_WILLENTERBACKGROUND:
		msgskip_save();
		break;
	}
	return 0;
}
#endif

static int MsgSkip_Init(struct string *name)
{
	if (!name)
		return 0;

	if (flags && save_path)
		msgskip_save();
	free(flags);
	flags = NULL;
	free(save_path);
	save_path = NULL;
	free_adv_flags();

	nr_flags = ain && ain->nr_messages > 0 ? ain->nr_messages : 0;
	size_t alloc_bytes = (size_t)((nr_flags + 7) / 8);
	flags = xcalloc(alloc_bytes ? alloc_bytes : 1, 1);

	// Most games have system.GetSaveFolderName() prepended to `name`, but some
	// games pass a constant string "SaveData\\MsgSkip.asd". If you pass it to
	// savedir_path() as it is, "SaveData/" will be duplicated, so remove it.
	if (!strncmp(name->text, "SaveData\\", 9)) {
		save_path = savedir_path(name->text + 9);
	} else {
		save_path = savedir_path(name->text);
	}

	size_t data_size;
	uint8_t *data = file_read(save_path, &data_size);
	if (data) {
		if (ain && ain->nr_messages > 0 && data_size == (size_t)ain->nr_messages) {
			// Migrate from the old format.
			for (size_t i = 0; i < data_size; i++) {
				if (data[i])
					flags[i >> 3] |= 0x80 >> (i & 7);
			}
		} else if (data_size < 4) {
			WARNING("Corrupted MsgSkip file '%s'", display_utf0(save_path));
		} else {
			int n = LittleEndian_getDW(data, 0);
			if (n < 0 || (size_t)(((uint64_t)(uint32_t)n + 7) / 8) != data_size - 4) {
				WARNING("Corrupted MsgSkip file '%s'", display_utf0(save_path));
			} else if (n != nr_flags) {
				WARNING("Incorrect size for MsgSkip file '%s'", display_utf0(save_path));
				if (n > nr_flags)
					n = nr_flags;
				if (n > 0)
					memcpy(flags, data + 4, (size_t)((n + 7) / 8));
			} else if (nr_flags > 0) {
				memcpy(flags, data + 4, (size_t)((nr_flags + 7) / 8));
			}
		}
		free(data);
	}
	if (!save_hook_registered) {
#if defined(__ANDROID__)
		SDL_AddEventWatch(msgskip_event_handler, NULL);
#elif !defined(ALICEHUB_IOS)
		atexit(msgskip_save);
#endif
		save_hook_registered = true;
	}
	savedata_set_background_flush_hook(msgskip_save);
	return 1;
}

static void MsgSkip_SetFlag(int msgnum)
{
	if (!flags || msgnum < 0 || msgnum >= nr_flags)
		return;
	flags[msgnum >> 3] |= 0x80 >> (msgnum & 7);
}

static int MsgSkip_GetFlag(int msgnum)
{
	if (!flags || msgnum < 0 || msgnum >= nr_flags)
		return 0;
	return !!(flags[msgnum >> 3] & 0x80 >> (msgnum & 7));
}

static void MsgSkip_SetEnable(int enable)
{
	enabled = !!enable;
}

static int MsgSkip_GetEnable(void)
{
	return enabled;
}

static void MsgSkip_SetState(int _state)
{
	state = _state;
}

static int MsgSkip_GetState(void)
{
	return state;
}

HLL_WARN_UNIMPLEMENTED( , void, MsgSkip, UseFlag, int use);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofMsg, void);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofFlag, void);

static void MsgSkip_PreLink(void)
{
	int libno = ain_get_library(ain, "MsgSkip");
	assert(libno >= 0);
	if (ain_get_library_function(ain, libno, "Init") < 0) {
		struct string *name = cstr_to_string("MsgSkip.asd");
		MsgSkip_Init(name);
		free_string(name);
	}
}

static void MsgSkip_SetAdvFlag(struct string *adv_name, int version, int step)
{
	if (!adv_name)
		return;
	for (size_t i = 0; i < nr_adv_flags; i++) {
		if (adv_flags[i].version == version && adv_flags[i].step == step
				&& !strcmp(adv_flags[i].name, adv_name->text))
			return;
	}
	adv_flags = xrealloc(adv_flags, (nr_adv_flags + 1) * sizeof(*adv_flags));
	adv_flags[nr_adv_flags].name = xstrdup(adv_name->text);
	adv_flags[nr_adv_flags].version = version;
	adv_flags[nr_adv_flags].step = step;
	nr_adv_flags++;
}

static int MsgSkip_GetAdvFlag(struct string *adv_name, int version, int step)
{
	if (!adv_name)
		return 0;
	for (size_t i = 0; i < nr_adv_flags; i++) {
		if (adv_flags[i].version == version && adv_flags[i].step == step
				&& !strcmp(adv_flags[i].name, adv_name->text))
			return 1;
	}
	return 0;
}

static void MsgSkip_ModuleFini(void)
{
	savedata_set_background_flush_hook(NULL);
	msgskip_save();
#if defined(__ANDROID__)
	if (save_hook_registered) {
		SDL_DelEventWatch(msgskip_event_handler, NULL);
		save_hook_registered = false;
	}
#endif
	free(flags);
	flags = NULL;
	nr_flags = 0;
	free(save_path);
	save_path = NULL;
	free_adv_flags();
	enabled = false;
	state = 0;
}

HLL_LIBRARY(MsgSkip,
	    HLL_EXPORT(_PreLink, MsgSkip_PreLink),
	    HLL_EXPORT(_ModuleFini, MsgSkip_ModuleFini),
	    HLL_EXPORT(Init, MsgSkip_Init),
	    HLL_EXPORT(UseFlag, MsgSkip_UseFlag),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(SetFlag, MsgSkip_SetFlag),
	    HLL_EXPORT(SetAdvFlag, MsgSkip_SetAdvFlag),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState),
	    HLL_EXPORT(GetFlag, MsgSkip_GetFlag),
	    HLL_EXPORT(GetAdvFlag, MsgSkip_GetAdvFlag),
	    HLL_EXPORT(GetNumofMsg, MsgSkip_GetNumofMsg),
	    HLL_EXPORT(GetNumofFlag, MsgSkip_GetNumofFlag));

HLL_QUIET_UNIMPLEMENTED(, void, vmMsgSkip, Init, void *imainsystem);

static int vmMsgSkip_EnumHandle(void)
{
	return flags ? 1 : 0;
}

static int vmMsgSkip_Open(struct string *fname, possibly_unused int option)
{
	return MsgSkip_Init(fname);
}

static void vmMsgSkip_Close(possibly_unused int handle)
{
	msgskip_save();
	free(flags);
	flags = NULL;
	nr_flags = 0;
	free(save_path);
	save_path = NULL;
}

static int vmMsgSkip_Query(possibly_unused int handle, int msgnum)
{
	int r = MsgSkip_GetFlag(msgnum);
	// vmMsgSkip does not have an explicit set function.
	MsgSkip_SetFlag(msgnum);
	return state && r;
}

HLL_LIBRARY(vmMsgSkip,
	    HLL_EXPORT(_ModuleFini, MsgSkip_ModuleFini),
	    HLL_EXPORT(Init, vmMsgSkip_Init),
	    HLL_EXPORT(EnumHandle, vmMsgSkip_EnumHandle),
	    HLL_EXPORT(Open, vmMsgSkip_Open),
	    HLL_EXPORT(Close, vmMsgSkip_Close),
	    HLL_EXPORT(Query, vmMsgSkip_Query),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState)
	    );
