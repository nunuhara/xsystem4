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

uniform sampler2D tex;   // the new scene
uniform sampler2D old;   // the old scene
uniform vec2 resolution; // the screen resolution
uniform float progress;  // effect progress (0..1)

in vec2 tex_coord;
out vec4 frag_color;

vec3 get_pixel(vec2 xy) {
        return mix(texture(old, xy).rgb, texture(tex, xy).rgb, progress);
}

float triangle(float f) {
        return 1.0 - abs(f * 2.0 - 1.0);
}

void main() {
        vec2 p = tex_coord * resolution;
        float scale = triangle(progress) * 80.0; // (0..0.5..1) -> (0..80..0);
        float wobble = progress * 24.0;
        float offset = sin((p.y / 100.0) - wobble) * scale;
        frag_color = vec4(get_pixel(fract(vec2(p.x + offset, p.y) / resolution)), 1.0);
}
