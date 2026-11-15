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

#include "system4.h"
#include "parts.h"
#include "parts_internal.h"

struct parts_radiobox *parts_get_radiobox(struct parts *parts)
{
	if (parts->type == PARTS_TYPE_RADIOBOX)
		return parts->widget.radiobox;
	parts_type_reset(parts, PARTS_TYPE_RADIOBOX, PARTS_SPRITE_NONE);
	return parts->widget.radiobox = xcalloc(1, sizeof(struct parts_radiobox));
}

void parts_radiobox_free(struct parts_radiobox *box)
{
	free(box->members);
	free(box);
}

// In each radio box that contains `member`, checking `member` clears the other
// members, and clearing `member` checks it again.
void parts_radiobox_member_changed(struct parts *member, bool checked)
{
	struct parts *group;
	PARTS_LIST_FOREACH(group) {
		if (group->type != PARTS_TYPE_RADIOBOX)
			continue;
		struct parts_radiobox *r = group->widget.radiobox;
		if (r->updating)
			continue;
		bool found = false;
		for (int i = 0; i < r->count; i++)
			found |= r->members[i] == member->no;
		if (!found)
			continue;
		r->updating = true;
		if (!checked) {
			parts_checkbox_set_checked(member, true);
		} else {
			for (int i = 0; i < r->count; i++) {
				struct parts *peer = parts_try_get(r->members[i]);
				if (peer && peer != member && peer->type == PARTS_TYPE_CHECKBOX)
					parts_checkbox_set_checked(peer, false);
			}
		}
		r->updating = false;
	}
}
