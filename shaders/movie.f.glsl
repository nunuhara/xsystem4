/* Copyright (C) 2023 kichikuou <KichikuouChrome@gmail.com>
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

uniform sampler2D texture_y;
uniform sampler2D texture_cb;
uniform sampler2D texture_cr;

in vec2 tex_coord;
out vec4 frag_color;

mat4 rec601 = mat4(
	1.16438,  0.00000,  1.59603, -0.87079,
	1.16438, -0.39176, -0.81297,  0.52959,
	1.16438,  2.01723,  0.00000, -1.08139,
	0.0,      0.0,      0.0,      1.0
);

void main() {
	float y = texture(texture_y, tex_coord).r;
	float cb = texture(texture_cb, tex_coord).r;
	float cr = texture(texture_cr, tex_coord).r;

	frag_color = vec4(y, cb, cr, 1.0) * rec601;
}
