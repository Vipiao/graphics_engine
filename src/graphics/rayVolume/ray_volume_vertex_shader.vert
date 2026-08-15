// ray_volume_vertex_shader.vert
#version 460 core

// Proxy-geometry vertex stage for volumetric effects. Transforms the proxy mesh
// into camera-relative view space (Dekker-compensated) exactly like the instance
// stage, and forwards the per-instance animated values so the fragment scaffold
// can shade the volume along the view ray.

#include "../shared_shaders/dekker_arithmetic.glsl"
#include "../shared_shaders/mesh_transform.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

// Per-vertex attributes (from geometry VBO)
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;

// Per-instance attributes (base instance VBO, locations 4-12)
layout (location = 4) in vec3 localPosition;
layout (location = 5) in vec4 localOrientation;
layout (location = 6) in vec3 localScale;
layout (location = 7) in vec4 instanceColor;
layout (location = 8) in int ssboIndex;

// Per-instance attributes (auxiliary ray-volume VBO, locations 13-14). state is
// the value at the instance's reference time; velocity is its time derivative.
layout (location = 13) in vec4 state;
layout (location = 14) in vec4 velocity;

uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;

uniform mat4 view;
uniform mat4 projection;

out vec3 vert_viewPos;              // proxy surface position, camera-relative view space
out vec2 vert_uv;
flat out vec4 vert_color;
flat out vec4 vert_value;           // forward-integrated per-instance values
flat out vec3 vert_centerViewPos;   // instance origin in view space
flat out mat3 vert_viewBasis;       // instance orientation: local -> view directions

// The proxy mesh is passed through untouched: the injected body decides how the
// geometry maps to the effect, and any coverage margin needed to avoid clipping
// soft edges is the author's responsibility (mesh resolution / sizing).

void main() {
   MeshData meshData = meshDataBuffer[ssboIndex];

   uint deltaTime = u_time - meshData.time;
   float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

   // Local instance transform
   mat3 localRot = fromQuaternion(localOrientation);
   vec3 localPos = localRot * (position * localScale) + localPosition;

   // Mesh world transform in camera-relative space, with physics interpolation
   vec3 meshPositionL = df3ToVec(df3Sub(
      Df3(meshData.positionHigh.xyz, meshData.positionLow.xyz),
      Df3(u_cameraPositionHigh, u_cameraPositionLow)));
   meshPositionL += meshData.velocity.xyz * deltaTimeFloat;

   mat3 worldOrientation =
      calculatePhysicsOrientation(meshData.orientation, meshData.angVel, deltaTimeFloat);

   vec3 cor = meshData.centerOfRotation.xyz;
   vec3 worldPos =
      applyRotationTransform(worldOrientation, localPos * meshData.scale.xyz, cor);
   vec3 worldCenter =
      applyRotationTransform(worldOrientation, localPosition * meshData.scale.xyz, cor);

   vert_viewPos = (view * vec4(meshPositionL + worldPos, 1.0)).xyz;
   vert_centerViewPos = (view * vec4(meshPositionL + worldCenter, 1.0)).xyz;

   // Full model -> view rotation (instance local orientation composed with the
   // interpolated world orientation). Orthonormal, so its transpose maps view
   // vectors back into the instance's local frame.
   vert_viewBasis = mat3(view) * worldOrientation * localRot;

   vert_uv = uv;
   vert_color = instanceColor;
   vert_value = state + velocity * deltaTimeFloat;

   gl_Position = projection * vec4(vert_viewPos, 1.0);
}
