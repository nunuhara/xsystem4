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
uniform float blend_rate;
uniform vec2 bot_left;
uniform vec2 top_right;
uniform vec3 add_color;
uniform vec3 multiply_color;

in vec2 tex_coord;
out vec4 frag_color;

float point_in_rect(vec2 p, vec2 bot_left, vec2 top_right) {
        vec2 s = step(bot_left, p) - step(top_right, p);
        return s.x * s.y;
}

void main() {
        vec2 size = vec2(textureSize(tex, 0));
	vec2 bl = bot_left / size;
	vec2 tr = top_right / size;

	vec4 tex_color = texture(tex, tex_coord);
	vec3 mod_color = (tex_color.rgb + add_color) * multiply_color;
	frag_color = vec4(mod_color, tex_color.a * blend_rate) * point_in_rect(tex_coord, bl, tr);
}
