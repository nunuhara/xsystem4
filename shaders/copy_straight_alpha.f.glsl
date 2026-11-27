uniform sampler2D tex;
uniform sampler2D dst_tex;
uniform ivec2 dst_origin;

in vec2 tex_coord;
out vec4 frag_color;

void main() {
	vec4 src = texture(tex, tex_coord);
	vec4 dst = texelFetch(dst_tex, ivec2(gl_FragCoord.xy) - dst_origin, 0);
	float dst_weight = dst.a * (1.0 - src.a);
	float alpha = src.a + dst_weight;
	vec3 rgb = alpha > 0.0 ? (src.rgb * src.a + dst.rgb * dst_weight) / alpha : vec3(0.0);
	frag_color = vec4(rgb, alpha);
}
