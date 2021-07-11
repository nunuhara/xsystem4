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
        // FIXME: This doesn't give the same result as SACT2.dll.
        //        SACT2.dll crunches the image along both axes, whereas
        //        this implementation only crunches the y-axis.
        vec2 p = tex_coord * resolution;
        float scale = triangle(progress) * 100.0; // (0..0.5..1) -> (0..100..0);
        float wobble = progress * 24.0;
        float offset = sin((p.x / 10.0) - wobble) * scale;
        frag_color = vec4(get_pixel(vec2(p.x, p.y + offset) / resolution), 1.0);
}
