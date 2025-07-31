#version 450
#define NUM_MONSTERS 1000

layout(binding = 0) uniform UniformBufferObject {
    float t;
	mat4 mvps[NUM_MONSTERS];
} ubo;

layout(location = 0) in vec4 color_in;
layout(location = 1) in vec2 uv_in;
layout(location = 2) in vec2 uv2_in;
layout(location = 3) in uint texture_idx_in;
layout(location = 4) in uint sprite_idx_in;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec2 frag_texcoord;
layout(location = 2) out uint frag_texture_idx;

vec2 positions[4] = vec2[] (
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 1.0)
);

uint indices[6] = uint[] (
	0, 1, 2,
	0, 2, 3
);

void main() {
	uint idx = indices[gl_VertexIndex % 6];

	vec4 position = vec4(positions[idx], 0.0, 1.0);
	// Get the transform matrix from the ubo
	mat4 mvp = ubo.mvps[sprite_idx_in];
	// Transform the vertex position
	gl_Position = mvp * position;

	frag_texcoord = uv_in;
	if (positions[idx].x > 0.5) {
		frag_texcoord.x = uv2_in.x;
	}
	// TODO: is this flipped?
	if (positions[idx].y < 0.5) {
		frag_texcoord.y = uv2_in.y;
	}
	// TODO: get color from the vertex buffer
	frag_color = color_in;
	frag_texture_idx = texture_idx_in;
}
