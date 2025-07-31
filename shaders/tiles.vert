#version 450

layout(push_constant) uniform PushConstantObject {
	mat4 mvp;
	vec4 color;
	uint texture_idx;
} push_constants;

layout(location = 0) in vec3 position_in;
layout(location = 1) in vec2 texcoord_in;

layout(location = 0) out vec2 frag_texcoord;

void main() {
	gl_Position = push_constants.mvp * vec4(position_in, 1.0);
	frag_texcoord = texcoord_in;
}
