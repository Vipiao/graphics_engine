#version 460 core

#include "../shared_shaders/dekker_arithmetic.glsl"
#include "../shared_shaders/mesh_transform.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

layout (location = 0) in vec3 position;
layout (location = 6) in uint meshIndex;

uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;

uniform mat4 view;
uniform mat4 projection;

void main() {
   MeshData meshData = meshDataBuffer[meshIndex];

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

   vec4 worldPos = vec4(meshPositionL + rotatedPosition, 1.0);

   gl_Position = projection * view * worldPos;
}
