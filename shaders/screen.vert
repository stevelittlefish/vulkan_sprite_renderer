#version 450

layout(location = 0) out vec2 frag_texcoord;

vec2 positions[4] = vec2[] (
	vec2(-1.0,  1.0),
	vec2( 1.0,  1.0),
	vec2( 1.0, -1.0),
	vec2(-1.0, -1.0)
);

vec2 texcoords[4] = vec2[] (
	vec2(0.0, 1.0),
	vec2(1.0, 1.0),
	vec2(1.0, 0.0),
	vec2(0.0, 0.0)
);

uint indices[6] = uint[] (
	0, 1, 2,
	0, 2, 3
);

void main() {
	uint idx = indices[gl_VertexIndex];

	gl_Position = vec4(positions[idx], 0.0, 1.0);
	frag_texcoord = texcoords[idx];
}
