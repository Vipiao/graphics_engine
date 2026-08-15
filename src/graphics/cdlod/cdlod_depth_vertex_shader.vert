// cdlod_depth_vertex_shader.vert
#version 460 core

#include "../shared_shaders/mesh_transform.glsl"
#include "cdlod_patch.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

// Per-node attributes; the layout is the G-buffer stage's, since both are drawn
// from the same instance buffer. The depth pass needs nothing but the position.
layout (location = 4) in vec3 patchCentreHigh;
layout (location = 5) in vec3 patchCentreLow;
layout (location = 6) in vec3 patchUAxis;
layout (location = 7) in vec3 patchVAxis;
layout (location = 8) in int patchLevel;
layout (location = 9) in int instanceIndex;

uniform mat4 view;
uniform mat4 projection;

// u_patchVertices and the instance buffer are declared by cdlod_patch.glsl, which
// every stage consulting the surface shares.

void main() {
   CdlodInstanceData instance = cdlodInstanceBuffer[instanceIndex];
   MeshData meshData = meshDataBuffer[instance.ssboIndex];

   // The same displacement the G-buffer stage applies, or the shadow map would
   // be cast by a different surface than the one it falls on. Both passes are
   // handed the same camera, so both offsets are measured from the same point.
   vec3 crudePoint;  // the shading stage's, and nothing this pass shades
   vec3 cameraOffset = cdlodBuildVertex(
      gl_VertexID, Df3(patchCentreHigh, patchCentreLow),
      patchUAxis, patchVAxis, patchLevel, instance, crudePoint);

   vec3 worldOffset = instance.bodyRotation * (cameraOffset * meshData.scale.xyz);

   gl_Position = projection * view * vec4(worldOffset, 1.0);
}
