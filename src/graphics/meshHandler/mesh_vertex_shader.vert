#version 460 core

#include "../shared_shaders/dekker_arithmetic.glsl"
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
// Colour, normal, material and mask units, one byte each; see
// MeshHandler::packTextureUnits, which is the only place this is written.
layout (location = 8) in uint textureUnits;

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

// One packed slot back to the unit the fragment stage indexes u_textures with.
// 255 is the packed form of "no texture", which the decode returns as -1.
int unpackTextureUnit(uint packedUnits, int slotIndex) {
   uint unit = (packedUnits >> (slotIndex * 8)) & 0xFFu;
   return unit == 0xFFu ? -1 : int(unit);
}

void main() {
   vert_occlusionFactor = occlusionFactor;

   MeshData meshData = meshDataBuffer[meshIndex];
   vert_uv = uv;
   // Per vertex rather than per mesh, so one mesh may wear several materials.
   // Declared flat downstream, so the provoking vertex decides for the triangle.
   vert_colorTextureUnit = unpackTextureUnit(textureUnits, 0);
   vert_normalTextureUnit = unpackTextureUnit(textureUnits, 1);
   vert_materialTextureUnit = unpackTextureUnit(textureUnits, 2);
   vert_maskTextureUnit = unpackTextureUnit(textureUnits, 3);
   vert_emissiveScalar = meshData.emissiveScalar;

   uint deltaTime = u_time - meshData.time;
   float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

   // Convert base position to camera-relative space (L-space) using Dekker subtraction
   vec3 meshPositionL = df3ToVec(df3Sub(
       Df3(meshData.positionHigh.xyz, meshData.positionLow.xyz),
       Df3(u_cameraPositionHigh, u_cameraPositionLow)));

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
