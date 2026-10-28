// scatter_impostor.frag
#version 460 core

// An impostor texel into the G-buffer: the four views blended, cut at half
// coverage. Reads are capped at vert_maxLod and kept half a texel inside their
// cell, so filtering never reaches a neighbouring view.
//
// A depth pre-pass: DEPTH_ONLY cuts and writes depth alone, then the shading
// pass shades what passed.

#ifndef DEPTH_ONLY
layout(early_fragment_tests) in;
#endif

layout(location = 0) out vec4 gAlbedo;    // RGB: albedo, A: metallic
layout(location = 1) out vec4 gNormal;    // RGB: view normal, A: roughness
layout(location = 2) out vec4 gMaterial;  // R: emissive, G: geometry flag, B: unused, A: alpha

uniform sampler2D u_textures[MAX_TEXTURE_UNITS];

in vec2 vert_atlasUV[4];
flat in vec4 vert_cellRect[4];
flat in vec4 vert_weights;
flat in mat3 vert_meshToView;
flat in ivec2 vert_atlasUnits;
flat in float vert_atlasSide;
flat in float vert_maxLod;

void main() {
    // The views are the same size on screen, so one level serves all four
    const float lod =
        min(textureQueryLod(u_textures[vert_atlasUnits.x], vert_atlasUV[0]).y, vert_maxLod);
    // The coarser of the two levels a trilinear read blends
    const float halfTexel = 0.5 * exp2(ceil(lod)) / vert_atlasSide;

    // The colour alone decides the cut, so cut pixels never read the normals
    vec4 colour = vec4(0.0);
    vec2 uvs[4];
    for (int nearest = 0; nearest < 4; ++nearest) {
        const float weight = vert_weights[nearest];
        if (weight <= 0.0) continue;

        uvs[nearest] = clamp(vert_atlasUV[nearest], vert_cellRect[nearest].xy + halfTexel,
                             vert_cellRect[nearest].zw - halfTexel);
        colour += weight * textureLod(u_textures[vert_atlasUnits.x], uvs[nearest], lod);
    }
    if (colour.a < 0.5) discard;
#ifdef DEPTH_ONLY
    return;
#endif

    // RGB: normal in the mesh's frame. A: roughness.
    vec4 normalRoughness = vec4(0.0);
    for (int nearest = 0; nearest < 4; ++nearest) {
        const float weight = vert_weights[nearest];
        if (weight <= 0.0) continue;
        normalRoughness +=
            weight * textureLod(u_textures[vert_atlasUnits.y], uvs[nearest], lod);
    }

    const vec3 normal = normalize(vert_meshToView * (normalRoughness.rgb * 2.0 - 1.0));

    gAlbedo = vec4(colour.rgb, 0.0);
    gNormal = vec4(normal * 0.5 + 0.5, normalRoughness.a);
    gMaterial = vec4(0.0, 1.0, 0.0, 1.0);
}
