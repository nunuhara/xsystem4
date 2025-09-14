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

uniform sampler2D tex;
uniform float threshold;
uniform vec4 color;  // .a is the discard threshold

in vec2 tex_coord;
out vec4 frag_color;

void main() {
	// the fractional part of threshold becomes a weight value for the
	// edge pixels
	int size = int(floor(threshold));
	int cutoff = size + 1;
	float edge_weight = fract(threshold) / 4.0;
	vec2 tex_size = vec2(textureSize(tex, 0).xy);
	float t = ceil(threshold);

	float a_out = 0.0;

	for (int x = -cutoff; x <= cutoff; x++) {
		for (int y = -cutoff; y <= cutoff; y++) {
			float d = distance(vec2(x, y), vec2(0, 0));
			if (d > t)
				continue;
			float a = texture(tex, tex_coord + vec2(x, y) / tex_size).r;
			if (d > float(size))
				a *= edge_weight;
			a_out += a;
		}
	}

	if (a_out < color.a)
		discard;
	frag_color = vec4(color.rgb, min(a_out, 1.0));
}
