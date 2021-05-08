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

#version 140

uniform mat4 view_transform;
uniform mat4 proj_transform;

in vec3 vertex_pos;
out float dist;

void main() {
        vec4 pos = view_transform * vec4(vertex_pos, 1.0);
        dist = abs(pos.z);
        gl_Position = proj_transform * pos;
        gl_PointSize = 600 / dist;  // FIXME
}
