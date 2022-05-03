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
uniform vec4 color;

in vec2 tex_coord;
out vec4 frag_color;

void main() {
	// alpha values at or above `rate + 16` get an alpha of 0
	// alpha values below `rate` get an alpha of 255
	// alpha values between `rate` and `rate + 16` are blended
	float delta = texture(tex, tex_coord).a - threshold;
	float alpha = 1.0 - clamp(delta * 16.0, 0.0, 1.0);
	frag_color = vec4(color.rgb, alpha);
}
