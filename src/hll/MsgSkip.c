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

#include "vm.h"
#include "xsystem4.h"

#include "hll.h"

static bool enabled = false;
static int state = 0;

static char *save_path;
static uint8_t *flags;
static int nr_flags;

static void msgskip_save(void)
{
	FILE *f = file_open_utf8(save_path, "wb");
	if (!f)
		ERROR("fopen: '%s': %s", display_utf0(save_path), strerror(errno));

	uint8_t header[4];
	LittleEndian_putDW(header, 0, nr_flags);
	fwrite(header, sizeof(header), 1, f);
	fwrite(flags, (nr_flags + 7) / 8, 1, f);
	fclose(f);
}

#ifdef __ANDROID__
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
	if (flags)
		return 1;

	nr_flags = ain->nr_messages;
	flags = xcalloc((nr_flags + 7) / 8, 1);

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
		if (data_size == (size_t)ain->nr_messages) {
			// Migrate from the old format.
			for (int i = 0; i < data_size; i++) {
				if (data[i])
					flags[i >> 3] |= 0x80 >> (i & 7);
			}
		} else {
			int n = LittleEndian_getDW(data, 0);
			if ((n + 7) / 8 != data_size - 4) {
				WARNING("Corrupted MsgSkip file '%s'", display_utf0(save_path));
			} else if (n != nr_flags) {
				WARNING("Incorrect size for MsgSkip file '%s'", display_utf0(save_path));
				if (n > nr_flags)
					n = nr_flags;
				memcpy(flags, data + 4, (n + 7) / 8);
			} else {
				memcpy(flags, data + 4, (nr_flags + 7) / 8);
			}
		}
		free(data);
	}
#ifdef __ANDROID__
	SDL_AddEventWatch(msgskip_event_handler, NULL);
#endif
	atexit(msgskip_save);
	return 1;
}

static void MsgSkip_SetFlag(int msgnum)
{
	if (msgnum < 0 || msgnum >= nr_flags)
		return;
	flags[msgnum >> 3] |= 0x80 >> (msgnum & 7);
}

static int MsgSkip_GetFlag(int msgnum)
{
	if (msgnum < 0 || msgnum >= nr_flags)
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

HLL_LIBRARY(MsgSkip,
	    HLL_EXPORT(Init, MsgSkip_Init),
	    HLL_EXPORT(UseFlag, MsgSkip_UseFlag),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(SetFlag, MsgSkip_SetFlag),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState),
	    HLL_EXPORT(GetFlag, MsgSkip_GetFlag),
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
}

static int vmMsgSkip_Query(possibly_unused int handle, int msgnum)
{
	int r = MsgSkip_GetFlag(msgnum);
	// vmMsgSkip does not have an explicit set function.
	MsgSkip_SetFlag(msgnum);
	return state && r;
}

HLL_LIBRARY(vmMsgSkip,
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
