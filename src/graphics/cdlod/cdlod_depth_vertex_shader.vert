// cdlod_depth_vertex_shader.vert
#version 460 core

#include "../shared_shaders/mesh_transform.glsl"
#include "cdlod_patch.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

// Per-node attributes. The shading-only ones are left undeclared; the depth pass
// needs nothing but the position.
layout (location = 4) in vec2 nodeOffset;
layout (location = 5) in float nodeSize;
layout (location = 6) in int faceIndex;
layout (location = 7) in int nodeLevel;
layout (location = 8) in int ssboIndex;

uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;

uniform mat4 view;
uniform mat4 projection;

// u_halfExtent, u_patchVertices, u_lodRangeFactor and u_cameraBodyPosition are
// declared by cdlod_patch.glsl, which every stage consulting the surface shares.

void main() {
   MeshData meshData = meshDataBuffer[ssboIndex];

   uint deltaTime = u_time - meshData.time;
   float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

   // The same displacement the G-buffer stage applies, or the shadow map would
   // be cast by a different surface than the one it falls on.
   vec2 patchUv =
      cdlodMorphedPatchUv(gl_VertexID, nodeOffset, nodeSize, faceIndex, nodeLevel);
   vec3 spherePosition = cdlodSpherePoint(
      cdlodPatchFaceUv(patchUv, nodeOffset, nodeSize), faceIndex);
   vec3 localPosition = cdlodDisplacedPosition(spherePosition);

   vec3 meshPositionL = dekkerSubtract(
      meshData.positionHigh.xyz, meshData.positionLow.xyz,
      u_cameraPositionHigh, u_cameraPositionLow
   );
   meshPositionL += meshData.velocity.xyz * deltaTimeFloat;

   mat3 worldOrientation =
      calculatePhysicsOrientation(meshData.orientation, meshData.angVel, deltaTimeFloat);
   vec3 worldTransformedPos = applyRotationTransform(
      worldOrientation, localPosition * meshData.scale.xyz, meshData.centerOfRotation.xyz);

   gl_Position = projection * view * vec4(meshPositionL + worldTransformedPos, 1.0);
}
