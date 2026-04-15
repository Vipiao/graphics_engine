// cdlod_vertex_shader.vert
#version 460 core

#include "../shared_shaders/mesh_transform.glsl"
#include "cdlod_patch.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

// No per-vertex attributes: the patch grid comes from gl_VertexID.
// Per-node attributes (from the body's instance VBO).
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
uniform vec4 u_baseColor;
// Tints each quadtree level differently so the debug view shows which level a
// patch was selected at, not just where its edges are.
uniform bool u_colorByLevel;

// The surface point this vertex sits over, before displacement. The fragment
// stage renormalizes it and evaluates the surface normal there, so the shading
// frequency follows the pixels rather than the patch's vertex spacing.
out vec3 vert_spherePosition;
// Body -> view rotation, for taking that normal into the space the G-buffer
// stores. Constant across a body, so it is passed flat rather than interpolated.
flat out mat3 vert_bodyToView;
out vec4 vert_color;
flat out float vert_emissiveScalar;

void main() {
   MeshData meshData = meshDataBuffer[ssboIndex];

   uint deltaTime = u_time - meshData.time;
   float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

   // Step 1: patch vertex -> position on the body, in the body's own frame
   vec2 patchUv =
      cdlodMorphedPatchUv(gl_VertexID, nodeOffset, nodeSize, faceIndex, nodeLevel);
   vec3 spherePosition = cdlodSpherePoint(
      cdlodPatchFaceUv(patchUv, nodeOffset, nodeSize), faceIndex);

   // Position only. The normal belonging to this surface is evaluated per pixel
   // instead, so nothing here has to carry a shading frame.
   vec3 localPosition = cdlodDisplacedPosition(spherePosition);

   // Step 2: body world transform with physics interpolation, camera-relative
   vec3 meshPositionL = dekkerSubtract(
      meshData.positionHigh.xyz, meshData.positionLow.xyz,
      u_cameraPositionHigh, u_cameraPositionLow
   );
   meshPositionL += meshData.velocity.xyz * deltaTimeFloat;

   mat3 worldOrientation =
      calculatePhysicsOrientation(meshData.orientation, meshData.angVel, deltaTimeFloat);
   vec3 worldTransformedPos = applyRotationTransform(
      worldOrientation, localPosition * meshData.scale.xyz, meshData.centerOfRotation.xyz);

   vec4 viewPos = view * vec4(meshPositionL + worldTransformedPos, 1.0);

   vert_spherePosition = spherePosition;
   vert_bodyToView = mat3(view) * worldOrientation;
   vert_color = u_colorByLevel ? vec4(cdlodLevelColor(nodeLevel), 1.0) : u_baseColor;
   vert_emissiveScalar = meshData.emissiveScalar;

   gl_Position = projection * viewPos;
}
