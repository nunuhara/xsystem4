/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/string.h"

#include "vm.h"
#include "parts.h"
#include "parts_internal.h"

#define PARTS_MSG_MAX_VARS 4

enum parts_msg_var_type {
	PARTS_MSG_VAR_NONE = 0,
	PARTS_MSG_VAR_INT = 1,
	PARTS_MSG_VAR_BOOL = 3,
};

struct parts_message {
	STAILQ_ENTRY(parts_message) entry;
	int parts_no;
	int delegate_index;
	int type;
	int variables[PARTS_MSG_MAX_VARS];
	enum parts_msg_var_type variable_types[PARTS_MSG_MAX_VARS];
	int nr_variables;
};

static STAILQ_HEAD(, parts_message) msg_queue =
		STAILQ_HEAD_INITIALIZER(msg_queue);

void parts_msg_push(struct parts* parts, int type, const char *fmt, ...)
{
	// Message system is introduced in Rance 9
	if (!parts_multi_controller)
		return;

	struct parts_message *msg = xmalloc(sizeof(*msg));
	msg->parts_no = parts->no;
	msg->delegate_index = parts->delegate_index;
	msg->type = type;
	msg->nr_variables = 0;

	va_list ap;
	va_start(ap, fmt);
	for (const char *p = fmt; *p; p++) {
		if (msg->nr_variables >= PARTS_MSG_MAX_VARS) {
			VM_ERROR("parts_msg_push: too many variables");
		}
		switch (*p) {
		case 'i':
		case 'b':
			msg->variable_types[msg->nr_variables] =
				*p == 'b' ? PARTS_MSG_VAR_BOOL : PARTS_MSG_VAR_INT;
			msg->variables[msg->nr_variables++] = va_arg(ap, int);
			break;
		default:
			VM_ERROR("parts_msg_push: unsupported format '%c'", *p);
		}
	}
	va_end(ap);

	STAILQ_INSERT_TAIL(&msg_queue, msg, entry);
}

void PE_ReleaseMessage(void)
{
	while (!STAILQ_EMPTY(&msg_queue)) {
		struct parts_message *msg = STAILQ_FIRST(&msg_queue);
		STAILQ_REMOVE_HEAD(&msg_queue, entry);
		free(msg);
	}
}

void PE_PopMessage(void)
{
	if (STAILQ_EMPTY(&msg_queue))
		return;
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	STAILQ_REMOVE_HEAD(&msg_queue, entry);
	free(msg);
}

int PE_GetMessageType(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg ? msg->type : 0;
}

int PE_GetMessagePartsNumber(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg ? msg->parts_no : 0;
}

int PE_GetMessageDelegateIndex(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg ? msg->delegate_index : -1;
}

int PE_GetMessageVariableCount(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	return msg ? msg->nr_variables : 0;
}

int PE_GetMessageVariableType(int index)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	if (!msg || index < 0 || index >= msg->nr_variables)
		return PARTS_MSG_VAR_NONE;
	return msg->variable_types[index];
}

int PE_GetMessageVariableInt(int index)
{
	if (PE_GetMessageVariableType(index) != PARTS_MSG_VAR_INT)
		return 0;
	return STAILQ_FIRST(&msg_queue)->variables[index];
}

float PE_GetMessageVariableFloat(possibly_unused int index)
{
	return 0.0f;
}

bool PE_GetMessageVariableBool(int index)
{
	if (PE_GetMessageVariableType(index) != PARTS_MSG_VAR_BOOL)
		return false;
	return STAILQ_FIRST(&msg_queue)->variables[index];
}

void PE_GetMessageVariableString(possibly_unused int index, struct string **out)
{
	if (*out)
		free_string(*out);
	*out = string_ref(&EMPTY_STRING);
}
