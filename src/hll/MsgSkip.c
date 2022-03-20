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

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
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
	FILE *f = fopen(save_path, "wb");
	if (!f)
		ERROR("fopen: '%s': %s", save_path, strerror(errno));

	fwrite(flags, nr_flags, 1, f);
	fclose(f);
	free(flags);
}

static bool msgskip_initialized = false;

static int MsgSkip_Init(struct string *name)
{
	if (msgskip_initialized)
		return 1;

	save_path = unix_path(name->text);

	uint8_t *data;
	size_t data_size;
	if (file_exists(save_path)) {
		data = file_read(save_path, &data_size);
	} else {
		data = xcalloc(ain->nr_messages, 1);
		data_size = ain->nr_messages;
	}
	if (data_size != (size_t)ain->nr_messages) {
		WARNING("Incorrect file size for MsgSkip file '%s'", save_path);
		if (data_size < (size_t)ain->nr_messages) {
			data = xrealloc_array(data, data_size, ain->nr_messages, 1);
		}
	}
	flags = data;
	nr_flags = ain->nr_messages;
	atexit(msgskip_save);
	msgskip_initialized = true;
	return 1;
}

static void MsgSkip_SetFlag(int msgnum)
{
	if (msgnum >= nr_flags)
		return;
	flags[msgnum] = 1;
}

static int MsgSkip_GetFlag(int msgnum)
{
	if (msgnum >= nr_flags)
		return 0;
	return !!flags[msgnum];
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
