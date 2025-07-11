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

#ifndef SYSTEM4_DUNGEON_SKYBOX_H
#define SYSTEM4_DUNGEON_SKYBOX_H

#include <cglm/types.h>

enum draw_dungeon_version;
struct dtx;
struct skybox;

struct skybox *skybox_create(enum draw_dungeon_version version, struct dtx *dtx);
void skybox_free(struct skybox *s);
void skybox_render(struct skybox *s, const mat4 view_transform, const mat4 proj_transform);

#endif /* SYSTEM4_DUNGEON_SKYBOX_H */
