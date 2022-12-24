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

uniform sampler2D tex;   // the new scene
uniform sampler2D old;   // the old scene
uniform vec2 resolution; // the screen resolution
uniform float progress;  // effect progress (0..1)

in vec2 tex_coord;
out vec4 frag_color;

void main() {
	float rand = fract(sin(dot(vec3(tex_coord, progress), vec3(12.9898, 78.233, 81.2463))) * 43758.5453);

	vec3 col;
	if (progress < 0.5) {
		col = mix(texture(old, tex_coord).rgb, vec3(rand), progress * 2.0);
	} else {
		col = mix(texture(tex, tex_coord).rgb, vec3(rand), (1.0 - progress) * 2.0);
	}
	frag_color = vec4(col, 1.0);
}
