/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#define DIFFUSE_EMISSIVE 0
#define DIFFUSE_NORMAL 1
#define DIFFUSE_LIGHT_MAP 2
#define DIFFUSE_ENV_MAP 3

#define ALPHA_BLEND 0
#define ALPHA_TEST 1
#define ALPHA_MAP_BLEND 2
#define ALPHA_MAP_TEST 3

#define FOG_LINEAR 1
#define FOG_LIGHT_SCATTERING 2

#define ENABLE_SPECULAR (ENGINE == REIGN_ENGINE || ENGINE == SEAL_ENGINE)
#define ENABLE_LIGHT_SCATTERING (ENGINE == REIGN_ENGINE || ENGINE == SEAL_ENGINE)

#if ENGINE == REIGN_ENGINE

#define NR_DIR_LIGHTS 3

struct dir_light {
	vec3 dir;
	vec3 diffuse;
	vec3 globe_diffuse;
};

uniform dir_light dir_lights[NR_DIR_LIGHTS];
uniform float rim_exponent;
uniform vec3 rim_color;
uniform float fog_near;
uniform float fog_far;
uniform vec3 fog_color;
in vec3 light_dir[NR_DIR_LIGHTS];

uniform vec3 global_ambient;

vec3 compute_diffuse(vec3 norm)
{
	vec3 diffuse = vec3(0.0);
	for (int i = 0; i < NR_DIR_LIGHTS; i++) {
		float half_lambert = dot(norm, -light_dir[i]) * 0.5 + 0.5;
		diffuse += mix(dir_lights[i].globe_diffuse, dir_lights[i].diffuse, half_lambert);
	}
	return diffuse;
}

#elif ENGINE == SEAL_ENGINE

const vec3 global_ambient = vec3(0.0);

// Hemisphere lighting (sky/middle/ground).
uniform vec3 hemi_light_dir;
uniform vec3 hemi_sky_color;
uniform vec3 hemi_mid_color;
uniform vec3 hemi_ground_color;

vec3 compute_diffuse(vec3 norm)
{
	float t = 0.5 * dot(norm, hemi_light_dir) + 0.5;
	vec3 low_mid = mix(hemi_ground_color, hemi_mid_color, t);
	vec3 mid_high = mix(hemi_mid_color, hemi_sky_color, t);
	return mix(low_mid, mid_high, t);
}

#else // TAPIR_ENGINE

const vec3 global_ambient = vec3(0.0);

vec3 compute_diffuse(vec3 norm)
{
	return vec3(1.0);
}

#endif // ENGINE == REIGN_ENGINE

#if ENABLE_LIGHT_SCATTERING
uniform int fog_type;
in vec3 ls_ex;
in vec3 ls_in;
#endif

#if ENABLE_SPECULAR
uniform sampler2D specular_texture;
uniform vec3 specular_color;
uniform float specular_shininess;
uniform bool use_specular_map;
in vec3 specular_dir;
#endif

#if ENGINE == SEAL_ENGINE
// Uncharted 2 (Hable) filmic tone mapping.
uniform vec4 tonemap_param;   // (ExposureBias, WhitePoint, A, B)
uniform vec4 tonemap_param2;  // (C, D, E, F)
uniform bool nolighting;

vec3 hable(vec3 v)
{
	float a = tonemap_param.z, b = tonemap_param.w;
	float c = tonemap_param2.x, d = tonemap_param2.y;
	float e = tonemap_param2.z, f = tonemap_param2.w;
	return ((v * (a * v + c * b) + d * e) / (v * (a * v + b) + d * f)) - e / f;
}

vec3 tone_map(vec3 color)
{
	vec3 curr = hable(color * tonemap_param.x);
	vec3 white = hable(vec3(tonemap_param.y));
	return curr / white;
}
#endif

uniform sampler2D tex;
uniform sampler2D blend_tex;
uniform bool use_blend_texture;
uniform sampler2D alpha_texture;
uniform sampler2D light_texture;
uniform sampler2D shadow_texture;
uniform float alpha_mod;

uniform bool use_normal_map;
uniform sampler2D normal_texture;

uniform vec3 instance_ambient;
uniform vec3 diffuse_mod;
uniform int diffuse_type;
uniform float shadow_darkness;
uniform float shadow_bias;
uniform int alpha_mode;

in vec2 tex_coord;
in vec2 light_tex_coord;
in vec4 color_mod;
in vec3 frag_pos;
in vec4 shadow_frag_pos;
in float dist;
in vec3 eye;
in vec3 normal;
in float blend_weight;
in vec2 blend_tex_coord;
out vec4 frag_color;

void main() {
	vec3 norm;
	if (use_normal_map) {
		// NOTE: TT3's shader doesn't normalize this vector.
		norm = texture(normal_texture, tex_coord).rgb * 2.0 - 1.0;
	} else {
		norm = normalize(normal);
	}

	vec3 view_dir = normalize(eye - frag_pos);
	vec3 frag_rgb = global_ambient + instance_ambient;

	// Diffuse lighting
	vec4 texel;
	vec3 diffuse = vec3(1.0);
	if (diffuse_type == DIFFUSE_ENV_MAP) {
		texel = texture(tex, (vec2(normal.x, -normal.y) + 1.0) / 2.0);
	} else {
		texel = texture(tex, tex_coord);
		if (use_blend_texture) {
			texel = mix(texel, texture(blend_tex, blend_tex_coord), blend_weight);
		}
		if (diffuse_type != DIFFUSE_EMISSIVE) {
			diffuse = compute_diffuse(norm);
			if (diffuse_type == DIFFUSE_LIGHT_MAP) {
				diffuse *= texture(light_texture, light_tex_coord).rgb;
			}
		}
	}
	frag_rgb += texel.rgb * diffuse * color_mod.rgb * diffuse_mod;

#if ENABLE_SPECULAR
	// Specular lighting
	if (any(greaterThan(specular_color, vec3(0.0)))) {
		vec3 reflect_dir = reflect(specular_dir, norm);
		float specular = pow(max(dot(view_dir, reflect_dir), 0.0), specular_shininess);
		if (use_specular_map) {
			specular *= texture(specular_texture, tex_coord).r;
		}
		frag_rgb += specular_color * specular;
	}
#endif

#if ENGINE == REIGN_ENGINE
	// Rim lighting
	if (rim_exponent > 0.0) {
		// Normal map is not used here.
		vec3 n = use_normal_map ? vec3(0.0, 0.0, 1.0) : normalize(normal);
		frag_rgb += pow(1.0 - max(dot(n, view_dir), 0.0), rim_exponent) * rim_color;
	}

	// Fog
	if (fog_type == FOG_LINEAR) {
		float fog_factor = clamp((dist - fog_near) / (fog_far - fog_near), 0.0, 1.0);
		frag_rgb = mix(frag_rgb, fog_color, fog_factor);
	}
#endif

#if ENABLE_LIGHT_SCATTERING
	if (fog_type == FOG_LIGHT_SCATTERING) {
		frag_rgb = frag_rgb * ls_ex + ls_in;
	}
#endif

	// Shadow mapping
	if (shadow_darkness > 0.0) {
		vec3 shadow_coords = shadow_frag_pos.xyz / shadow_frag_pos.w * 0.5 + 0.5;
		shadow_coords.z = min(1.0, shadow_coords.z - shadow_bias);
		// PCF with 9 samples.
		float brightness = 1.0;
		vec2 texel_size = 1.0 / vec2(textureSize(shadow_texture, 0));
		for (int x = -1; x <= 1; ++x) {
			for (int y = -1; y <= 1; ++y) {
				float depth = texture(shadow_texture, shadow_coords.xy + vec2(x, y) * texel_size).r;
				if (depth < shadow_coords.z)
					brightness -= 0.05 * shadow_darkness;
			}
		}
		frag_rgb *= brightness;
	}

#if ENGINE == SEAL_ENGINE
	if (!nolighting)
		frag_rgb = tone_map(frag_rgb);
#endif

	// Alpha mapping
	float alpha = texel.a * color_mod.a * alpha_mod;
	if (alpha_mode == ALPHA_TEST) {
		if (alpha < 0.75)
			discard;
	} else if (alpha_mode == ALPHA_MAP_BLEND) {
		alpha *= texture(alpha_texture, tex_coord).r;
	} else if (alpha_mode == ALPHA_MAP_TEST) {
		if (texture(alpha_texture, tex_coord).r < 0.1)
			discard;
	}

	frag_color = vec4(frag_rgb, alpha);
}
