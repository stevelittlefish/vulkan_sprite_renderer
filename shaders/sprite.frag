#version 450
#extension GL_EXT_nonuniform_qualifier : require
#define NUM_TEXTURES 5

layout(binding = 1) uniform sampler2D texSampler[NUM_TEXTURES];

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
