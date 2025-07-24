/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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
uniform sampler2D light_texture;
uniform bool use_lightmap;
uniform float alpha_mod;
uniform bool use_fog;

in float dist;
in vec2 tex_coord;
out vec4 frag_color;

const float FOG_MAX_DIST = 12.0;
const vec3 FOG_COLOR = vec3(0.0, 0.0, 0.0);

void main() {
        vec4 texel = texture(tex, tex_coord);
        float light_factor = 1.0;
        if (use_lightmap) {
                light_factor = texture(light_texture, tex_coord).a;
        }
        if (use_fog) {
                float fog_factor = (FOG_MAX_DIST - dist) / FOG_MAX_DIST;
                fog_factor = clamp(fog_factor, 0.3, 1.0);
                frag_color = vec4(mix(FOG_COLOR, texel.rgb * light_factor, fog_factor), texel.a * alpha_mod);
        } else {
                if (texel.a < 0.01) {
                        discard;
                }
                frag_color = vec4(texel.rgb * light_factor, texel.a * alpha_mod);
        }
}
