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

#include <string.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "parts.h"
#include "parts_internal.h"

// Replace each UTF-8 string in `strs` with a new CP932 string. The function
// does not free the old strings.
void parts_utf2sjis_array(const char **strs, int n)
{
	for (int i = 0; i < n; i++)
		strs[i] = utf2sjis(strs[i], 0);
}

const char *parts_widget_look_suffix(int look)
{
	static const char *suffixes[PARTS_NR_LOOKS] = { "／通常", "／オン", "／ダウン", "／無効" };
	static bool converted;
	if (!converted) {
		parts_utf2sjis_array(suffixes, PARTS_NR_LOOKS);
		converted = true;
	}
	return suffixes[look];
}

struct string *parts_widget_resource_name(struct string *base, const char *infix,
		const char *suffix)
{
	struct string *name = string_dup(base);
	string_append_cstr(&name, infix, strlen(infix));
	string_append_cstr(&name, suffix, strlen(suffix));
	return name;
}

void parts_widget_free(struct parts *parts)
{
	switch (parts->type) {
	case PARTS_TYPE_SPRITE:
		return;
	case PARTS_TYPE_BUTTON:
		parts_button_free(parts->widget.button);
		break;
	case PARTS_TYPE_CHECKBOX:
		parts_checkbox_free(parts->widget.checkbox);
		break;
	case PARTS_TYPE_VSCROLLBAR:
	case PARTS_TYPE_HSCROLLBAR:
		parts_scrollbar_free(parts->widget.scrollbar);
		break;
	case PARTS_TYPE_LAYOUTBOX:
		free(parts->widget.layoutbox);
		break;
	case PARTS_TYPE_RADIOBOX:
		parts_radiobox_free(parts->widget.radiobox);
		break;
	}
	parts->type = PARTS_TYPE_SPRITE;
	memset(&parts->widget, 0, sizeof(parts->widget));
}

void parts_type_reset(struct parts *parts, enum parts_type type,
		enum parts_sprite_type sprite_type)
{
	parts_widget_free(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++)
		parts_sprite_reset(&parts->sprites[i], sprite_type);
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
	parts->type = type;
}

void parts_set_type(struct parts *parts, enum parts_type type)
{
	switch (type) {
	case PARTS_TYPE_SPRITE:
		break;
	case PARTS_TYPE_BUTTON:
		parts_get_button(parts);
		break;
	case PARTS_TYPE_CHECKBOX:
		parts_get_checkbox(parts);
		break;
	case PARTS_TYPE_VSCROLLBAR:
	case PARTS_TYPE_HSCROLLBAR:
		parts_get_scrollbar(parts, type == PARTS_TYPE_HSCROLLBAR);
		break;
	case PARTS_TYPE_LAYOUTBOX:
		parts_get_layoutbox(parts);
		break;
	case PARTS_TYPE_RADIOBOX:
		parts_get_radiobox(parts);
		break;
	}
}

void parts_widget_update(struct parts *parts)
{
	if (parts_is_scrollbar(parts))
		parts_scrollbar_update(parts);
}

void parts_widget_end_input(struct parts *parts)
{
	if (parts_is_scrollbar(parts))
		parts_scrollbar_reset_input(parts);
}
