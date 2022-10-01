/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

uniform vec2 center;
uniform float start_angle;
uniform float angle;
uniform vec4 color;

const float PI = 3.14159265358979323846;

in vec2 tex_coord;
out vec4 frag_color;

void main() {
	vec2 p = tex_coord - center;
	float a = atan(p.y, p.x);
	a = mod(a - start_angle, 2.0 * PI);
	if (a > angle)
		discard;
	frag_color = color;
}
