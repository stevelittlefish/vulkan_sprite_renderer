#version 450

layout(binding = 0) uniform UniformBufferObject {
    float t;
} ubo;

layout(binding = 1) uniform sampler2D tex_sampler;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

void main() {
	// Wavy effect
	float wave = sin(ubo.t * 2.0 + frag_tex_coord.x * 2.0 + frag_tex_coord.y) * 0.01;
	vec2 tex_coord = frag_tex_coord + vec2(wave, wave);
	out_color = texture(tex_sampler, tex_coord);
}
