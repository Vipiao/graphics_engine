#version 460 core

#include "../shared_shaders/mesh_transform.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;
layout (location = 4) in float occlusionFactor;
layout (location = 5) in vec4 color;
layout (location = 6) in uint meshIndex;
layout (location = 7) in uint triangleIndex;

uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;

uniform mat4 view;
uniform mat4 projection;

out vec3 vert_normal;
out mat3 vert_TBN;
out vec3 vert_pos;
out vec2 vert_uv;
out vec4 vert_color;
flat out int vert_colorTextureUnit;
flat out int vert_normalTextureUnit;
out float vert_occlusionFactor;
flat out int vert_materialTextureUnit;
flat out float vert_emissiveScalar;
flat out int vert_maskTextureUnit;

void main() {
   vert_occlusionFactor = occlusionFactor;

   MeshData meshData = meshDataBuffer[meshIndex];
   vert_uv = uv;
   vert_colorTextureUnit = meshData.colorTextureUnit;
   vert_normalTextureUnit = meshData.normalTextureUnit;
   vert_materialTextureUnit = meshData.materialTextureUnit;
   vert_emissiveScalar = meshData.emissiveScalar;
   vert_maskTextureUnit = meshData.maskTextureUnit;

   uint deltaTime = u_time - meshData.time;
   float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

   // Convert base position to camera-relative space (L-space) using Dekker subtraction
   vec3 meshPositionL = dekkerSubtract(
       meshData.positionHigh.xyz, meshData.positionLow.xyz,
       u_cameraPositionHigh, u_cameraPositionLow
   );

   // Add velocity in camera-relative space (velocity is already small relative to camera distance)
   vec3 velocityDelta = meshData.velocity.xyz * deltaTimeFloat;
   meshPositionL += velocityDelta;

   mat3 orientation = calculatePhysicsOrientation(meshData.orientation, meshData.angVel, deltaTimeFloat);

   // Apply scale to the position before rotation
   vec3 scaledPosition = position * meshData.scale.xyz;

   vec3 rotatedPosition = applyRotationTransform(orientation, scaledPosition, meshData.centerOfRotation.xyz);

   // Build TBN matrix
   mat3 worldTBN = buildTBNMatrix(orientation, normal, tangent);
   vec3 worldNormal = normalize(orientation * normal);

   vec4 worldPos = vec4(meshPositionL + rotatedPosition, 1.0);

   // Transform to view space
   vec4 viewPos = view * worldPos;
   vert_pos = viewPos.xyz;
   vert_normal = mat3(view) * worldNormal;
   vert_TBN = mat3(view) * worldTBN;

   vert_color = color;
   gl_Position = projection * view * worldPos;
}
