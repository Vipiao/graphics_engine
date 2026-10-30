// scatter_vertex_shader.vert
#version 460 core

// Draws one vertex of one item's mesh into the G-buffer.

#include "scatter_item.glsl"

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;
layout (location = 4) in float layer;

uniform mat4 view;
uniform mat4 projection;

// The depth pre-pass and the shading pass must place it alike
invariant gl_Position;

out vec3 vert_normal;
out mat3 vert_TBN;
out vec3 vert_pos;
// z: the texture layer
out vec3 vert_uv;
out vec4 vert_color;
flat out int vert_colorTextureUnit;
flat out int vert_normalTextureUnit;
flat out int vert_materialTextureUnit;
flat out float vert_emissiveScalar;
flat out int vert_maskTextureUnit;
flat out vec3 vert_crossfade;

void main() {
    const ScatterDrawnItem drawn = scatterDrawnItem();
    const mat3 bodyRotation = drawn.instance.bodyRotation;
    const vec3 bodyScale = drawn.instance.bodyScale.xyz;

    // Normals take the inverse body scale; the item's scale is uniform
    const vec3 worldNormal = normalize(bodyRotation * ((drawn.rotation * normal) / bodyScale));
    const vec3 worldTangent = normalize(bodyRotation * ((drawn.rotation * tangent) * bodyScale));
    const vec3 bitangent = cross(worldNormal, worldTangent);

    const vec4 viewPosition = view * vec4(scatterCameraRelative(drawn, position), 1.0);
    vert_pos = viewPosition.xyz;
    vert_normal = mat3(view) * worldNormal;
    vert_TBN = mat3(view) * mat3(worldTangent, bitangent, worldNormal);
    vert_uv = vec3(uv, layer);

    const ScatterKindData kind = kinds[drawn.item.kind];
    vert_color = kind.color;
    vert_colorTextureUnit = kind.textureUnits.x;
    vert_normalTextureUnit = kind.textureUnits.y;
    vert_materialTextureUnit = kind.textureUnits.z;
    vert_maskTextureUnit = kind.textureUnits.w;
    vert_emissiveScalar = 0.0;
    // The LOD meshes are drawn from command 0, so the draw is the mesh. The shadow
    // mesh is an item's only caster, so only fades out at its far end.
    const uint mesh = uint(gl_DrawID);
    vert_crossfade = mesh == kind.shadowMesh ? scatterFadeOut(drawn, mesh)
                                             : scatterCrossfade(drawn, mesh - kind.firstMesh);

    gl_Position = projection * viewPosition;
}
