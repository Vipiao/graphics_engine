// impostor_bake.frag
#version 460 core

// The surface through material_decode.glsl, as the G-buffer pass sees it.
// Metallic and emission are left out.

#include "../shared_shaders/material_decode.glsl"

layout(location = 0) out vec4 outColor;   // RGB: albedo, A: coverage
layout(location = 1) out vec4 outNormal;  // RGB: normal packed to [0, 1], A: roughness

uniform sampler2D u_textures[MAX_TEXTURE_UNITS];
uniform vec4 u_color;
// Colour, normal, material, mask; -1 for none
uniform ivec4 u_textureUnits;

in vec3 vert_normal;
in mat3 vert_TBN;
in vec2 vert_uv;

void main() {
    SurfaceMaterial surface = decodeSurfaceMaterial(
        u_textures, vert_normal, vert_TBN, vert_uv, u_color,
        u_textureUnits.x, u_textureUnits.y, u_textureUnits.z, u_textureUnits.w, 0.0);

    if (surface.alpha < 1.0 / 255.0) discard;

    outColor = vec4(surface.color, surface.alpha);
    outNormal = vec4(surface.normal * 0.5 + 0.5, surface.roughness);
}
