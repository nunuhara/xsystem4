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

#define EFFECT_WIDTH 210.0

void main() {
        // start position of effect
        float lhs = ((resolution.x + EFFECT_WIDTH) * progress) - EFFECT_WIDTH;
        // current pixel x-coordinate
        float p = tex_coord.x * resolution.x;
	// calculate alpha value
	float alpha = clamp((p - lhs) / EFFECT_WIDTH, 0.0, 1.0);
	vec3 col = mix(texture(tex, tex_coord).rgb, texture(old, tex_coord).rgb, alpha);
	frag_color = vec4(col, 1.0);
}
