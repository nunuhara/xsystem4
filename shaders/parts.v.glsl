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

uniform mat4 world_transform;
uniform mat4 view_transform;
uniform mat4 inv_clipper_transform;

in vec4 vertex_pos;
in vec2 vertex_uv;
out vec2 tex_coord;
out vec2 clip_coord;

void main() {
	vec4 world_pos = world_transform * vertex_pos;
	gl_Position = view_transform * world_pos;
	tex_coord = vertex_uv;
	clip_coord = (inv_clipper_transform * world_pos).xy;
}
