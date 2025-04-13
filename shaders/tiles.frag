#version 450

layout(binding = 1) uniform sampler2D texSampler[2];

layout(push_constant) uniform PushConstantObject {
	mat4 mvp;
	vec4 color;
	uint texture_idx;
} push_constants;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

void main() {
	vec4 tex_color = texture(texSampler[push_constants.texture_idx], frag_tex_coord);
	if (tex_color.a < 0.5) {
		discard;
	}
    out_color = tex_color * push_constants.color;
}
