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

const float PI = 3.14159265;

vec3 get_pixel(vec2 xy, float fade) {
        return mix(texture(old, xy).rgb, texture(tex, xy).rgb, fade);
}

void main() {
        vec2 p = tex_coord * resolution;
	// 1 - (x - 1)^2
	float fade_progress = 1.0 - pow(progress - 1.0, 2.0); // (0..0.5..1) -> (0..1) [parabolic]
	// sin(PI*x)^4
	float blur_size = pow(sin(PI * progress), 4.0); // (0..0.5..1) -> (0..1..0) [sine wave ^ 4]
	float blur_half = (blur_size * resolution.x) / 2.0;

        vec3 avg = vec3(0.0);
	float count = 0.0;

	avg += get_pixel(p / resolution, fade_progress);
	count++;

        for (float i = 1.0; i <= blur_half; i += 2.0) {
		avg += get_pixel(vec2(p.x + i, p.y) / resolution, fade_progress);
		avg += get_pixel(vec2(p.x - i, p.y) / resolution, fade_progress);
		count += 2.0;
        }

        frag_color = vec4(avg / count, 1.0);
}
