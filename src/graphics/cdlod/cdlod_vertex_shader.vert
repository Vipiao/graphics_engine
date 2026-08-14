// cdlod_vertex_shader.vert
#version 460 core

#include "../shared_shaders/mesh_transform.glsl"
#include "cdlod_patch.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

// No per-vertex attributes: the patch grid comes from gl_VertexID.
// Per-node attributes (from the shared instance VBO). Locations start at 4,
// leaving 0..3 free so the layout stays readable against the instanced-geometry
// shaders. Location 8 carries the CDLOD instance rather than the SSBO index
// those shaders have there: everything true of the whole body, that index
// included, is reached through it.
layout (location = 4) in vec3 patchCentre;
layout (location = 5) in vec3 patchUAxis;
layout (location = 6) in vec3 patchVAxis;
layout (location = 7) in int patchLevel;
layout (location = 8) in int instanceIndex;

uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;

uniform mat4 view;
uniform mat4 projection;

// u_patchVertices and the instance buffer are declared by cdlod_patch.glsl, which
// every stage consulting the surface shares.
// Tints each quadtree level differently so the debug view shows which level a
// patch was selected at, not just where its edges are.
uniform bool u_colorByLevel;

// The crude point this vertex was built from, so the fragment stage can ask the
// surface for the normal there and shade per pixel rather than per vertex.
// Interpolating it is exact: it is affine in the patch coordinate.
out vec3 vert_crudePoint;
// Body -> view rotation, for taking that normal into the space the G-buffer
// stores. Constant across a body, so it is passed flat rather than interpolated.
flat out mat3 vert_bodyToView;
out vec4 vert_color;
flat out float vert_emissiveScalar;

void main() {
   CdlodInstanceData instance = cdlodInstanceBuffer[instanceIndex];
   MeshData meshData = meshDataBuffer[instance.ssboIndex];

   uint deltaTime = u_time - meshData.time;
   float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

   // Step 1: patch vertex -> crude point -> position on the body, in its own frame
   vec2 patchUv = cdlodMorphedPatchUv(gl_VertexID, patchCentre, patchUAxis, patchVAxis,
                                      patchLevel, instance);
   vec3 crudePoint = cdlodCrudePoint(patchUv, patchCentre, patchUAxis, patchVAxis);

   // Position only. The normal belonging to this surface is evaluated per pixel
   // instead, so nothing here has to carry a shading frame.
   vec3 localPosition = cdlodSurfacePoint(crudePoint);

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

   vert_crudePoint = crudePoint;
   vert_bodyToView = mat3(view) * worldOrientation;
   vert_color = u_colorByLevel ? vec4(cdlodLevelColor(patchLevel), 1.0) : instance.baseColor;
   vert_emissiveScalar = meshData.emissiveScalar;

   gl_Position = projection * viewPos;
}
