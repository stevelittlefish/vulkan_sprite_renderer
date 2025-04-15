#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(binding = 0) uniform UniformBufferObject {
    float t;
	mat4 mvps[1000];
} ubo;
layout(binding = 1) uniform sampler2D texSampler[2];

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 2) flat in uint frag_texture_index;

layout(location = 0) out vec4 out_color;

void main() {
	vec4 tex_color = texture(texSampler[nonuniformEXT(frag_texture_index)], frag_tex_coord);
	if (tex_color.a < 0.5) {
		discard;
	}
	out_color = tex_color * frag_color;
}
