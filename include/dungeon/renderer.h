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

#ifndef SYSTEM4_DUNGEON_RENDERER_H
#define SYSTEM4_DUNGEON_RENDERER_H

#include <cglm/cglm.h>
#include <GL/glew.h>

struct dgn_cell;
struct dtx;
struct dungeon_renderer;

struct dungeon_renderer *dungeon_renderer_create(struct dtx *dtx, GLuint *event_textures, int nr_event_textures);
void dungeon_renderer_free(struct dungeon_renderer *r);
void dungeon_renderer_render(struct dungeon_renderer *r, struct dgn_cell **cells, int nr_cells, mat4 view_transform, mat4 proj_transform);

#endif /* SYSTEM4_DUNGEON_RENDERER_H */