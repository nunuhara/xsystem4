/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_DUNGEON_EVENT_MARKERS_H
#define SYSTEM4_DUNGEON_EVENT_MARKERS_H

#include <cglm/types.h>

struct event_markers;

struct event_markers *event_markers_create(void);
void event_markers_free(struct event_markers *em);
void event_markers_render(struct event_markers *em, const mat4 view_transform, const mat4 proj_transform);
void event_markers_set(struct event_markers *em, int x, int y, int z, int event_type);
void event_markers_set_blend_rate(struct event_markers *em, int x, int y, int z, int rate);

#endif /* SYSTEM4_DUNGEON_EVENT_MARKERS_H */
