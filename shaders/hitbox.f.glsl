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
uniform vec2 bot_left;
uniform vec2 top_right;

in vec2 tex_coord;
out vec4 frag_color;

float point_in_rect(vec2 p, vec2 bot_left, vec2 top_right) {
        vec2 s = step(bot_left, p) - step(top_right, p);
        return s.x * s.y;
}

void main() {
        vec2 size = vec2(textureSize(tex, 0));
        vec2 tex_bot_left = bot_left / size;
        vec2 tex_top_right = top_right / size;

        vec4 texel = texture(tex, tex_coord);
        float t = point_in_rect(tex_coord, tex_bot_left, tex_top_right);
        frag_color = t * texel + (1.0 - t) * vec4(0, 0, 0, 0);
}
