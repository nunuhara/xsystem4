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

#define M_PI 3.1415926535897932384626433832795
#define OLD_PERIOD ((2.0 * M_PI) / 360.0)
#define NEW_PERIOD ((2.0 * M_PI) / 720.0)
#define OLD_SHIFT (-M_PI)
#define NEW_SHIFT (M_PI / 2.0)

vec3 get_pixel(vec2 old_xy, vec2 new_xy) {
	// influence of old color is described by: C_old = (1-t)^2
	// influence of new color increases linearly: C_new = t
	// the resulting blend is not normalized
	vec3 old = texture(old, old_xy).rgb;
	vec3 new = texture(tex, new_xy).rgb;
	float old_weight = (1.0 - progress) * (1.0 - progress);
	float new_weight = progress;
	return (old * old_weight + new * new_weight);
}

float triangle(float f) {
        return 1.0 - abs(f * 2.0 - 1.0);
}

void main() {
	// old: x = amp * sin((2pi/360)*y + phase + -pi)
	// new: x = amp * sin((2pi/720)*y + phase + pi/2)
        vec2 p = tex_coord * resolution;
        float amp = triangle(progress) * 80.0; // (0..0.5..1) -> (0..80..0);
	float phase = progress * M_PI * 8.0;
	float old_off = amp * sin(OLD_PERIOD * p.y + OLD_SHIFT + phase);
	float new_off = amp * sin(NEW_PERIOD * p.y + NEW_SHIFT + phase);
	vec2 old_xy = fract(vec2(p.x + old_off, p.y) / resolution);
	vec2 new_xy = fract(vec2(p.x - new_off, p.y) / resolution);
        frag_color = vec4(get_pixel(old_xy, new_xy), 1.0);
}
