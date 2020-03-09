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

#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/string.h"
#include "hll.h"
#include "MsgLogManager.h"

static size_t msg_log_size = 0;
static size_t msg_log_i = 0;
struct msg_log_entry *msg_log = NULL;

#define LOG_MAX 2048

static void msg_log_alloc(void)
{
	if (msg_log_i < msg_log_size)
		return;
	if (!msg_log_size)
		msg_log_size = 1;

	// rotate log
	if (msg_log_i >= LOG_MAX) {
		for (size_t i = 0; i < msg_log_i - LOG_MAX/2; i++) {
			if (msg_log[i].data_type == MSG_LOG_STRING)
				free_string(msg_log[i].s);
		}
		memcpy(msg_log, msg_log + (msg_log_i - LOG_MAX/2), sizeof(struct msg_log_entry) * LOG_MAX/2);
		msg_log_i = LOG_MAX/2;
		return;
	}

	msg_log = xrealloc(msg_log, sizeof(struct msg_log_entry) * msg_log_size * 2);
	msg_log_size *= 2;
}

int MsgLogManager_Numof(void)
{
	return msg_log_i;
}

static void MsgLogManager_AddInt(int type, int value)
{
	msg_log_alloc();
	msg_log[msg_log_i++] = (struct msg_log_entry) {
		.data_type = MSG_LOG_INT,
		.type = type,
		.i = value
	};
}

static void MsgLogManager_AddString(int type, struct string *str)
{
	msg_log_alloc();
	msg_log[msg_log_i++] = (struct msg_log_entry) {
		.data_type = MSG_LOG_STRING,
		.type = type,
		.s = string_ref(str)
	};
}

HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, GetType, int index);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, GetInt, int index);
HLL_WARN_UNIMPLEMENTED( , void, MsgLogManager, GetString, int index, struct string **str);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, Save, struct string *filename);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, Load, struct string *filename);
HLL_WARN_UNIMPLEMENTED( , void, MsgLogManager, SetLineMax, int line_max);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, GetInterface, void);

HLL_LIBRARY(MsgLogManager,
	    HLL_EXPORT(Numof, MsgLogManager_Numof),
	    HLL_EXPORT(AddInt, MsgLogManager_AddInt),
	    HLL_EXPORT(AddString, MsgLogManager_AddString),
	    HLL_EXPORT(GetType, MsgLogManager_GetType),
	    HLL_EXPORT(GetInt, MsgLogManager_GetInt),
	    HLL_EXPORT(GetString, MsgLogManager_GetString),
	    HLL_EXPORT(Save, MsgLogManager_Save),
	    HLL_EXPORT(Load, MsgLogManager_Load),
	    HLL_EXPORT(SetLineMax, MsgLogManager_SetLineMax),
	    HLL_EXPORT(GetInterface, MsgLogManager_GetInterface));
