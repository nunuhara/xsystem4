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

void parts_widget_free(struct parts *parts)
{
	switch (parts->type) {
	case PARTS_TYPE_SPRITE:
		return;
	case PARTS_TYPE_LAYOUTBOX:
		free(parts->widget.layoutbox);
		break;
	}
	parts->type = PARTS_TYPE_SPRITE;
	parts->widget.layoutbox = NULL;
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
	case PARTS_TYPE_LAYOUTBOX:
		parts_get_layoutbox(parts);
		break;
	}
}
