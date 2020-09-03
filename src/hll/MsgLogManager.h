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

#ifndef SYSTEM4_MSGLOGMANAGER_H
#define SYSTEM4_MSGLOGMANAGER_H

enum msg_data_type {
	MSG_LOG_INT,
	MSG_LOG_STRING
};

struct msg_log_entry {
	enum msg_data_type data_type;
	int type;
	union {
		int i;
		struct string *s;
	};
};

extern struct msg_log_entry *msg_log;

#endif /* SYSTEM4_MSGLOGMANAGER_H */
