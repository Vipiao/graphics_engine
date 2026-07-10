#version 460 core

#include "../shared_shaders/mesh_transform.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

// Per-vertex attributes (from geometry VBO)
layout (location = 0) in vec3 position;

// Per-instance attributes (from instance VBO)
layout (location = 4) in vec3 localPosition;
layout (location = 5) in vec4 localOrientation;
layout (location = 6) in vec3 localScale;
// layout (location = 7) is instanceColor - not needed for depth pass
layout (location = 8) in int ssboIndex;

uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;

uniform mat4 view;
uniform mat4 projection;

void main() {
    // Get geometry world transform from SSBO using instance's ssboIndex
    MeshData meshData = meshDataBuffer[ssboIndex];

    // Calculate time delta
    uint deltaTime = u_time - meshData.time;
    float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

    // Step 1: Apply local instance transform to geometry vertex
    vec3 scaledPosition = position * localScale;
    mat3 localOrientationMatrix = fromQuaternion(localOrientation);
    vec3 localTransformedPos = localOrientationMatrix * scaledPosition + localPosition;

    // Step 2: Get world geometry transform with physics interpolation
    vec3 meshPositionL = dekkerSubtract(
        meshData.positionHigh.xyz, meshData.positionLow.xyz,
        u_cameraPositionHigh, u_cameraPositionLow
    );

    // Add velocity in camera-relative space
    vec3 velocityDelta = meshData.velocity.xyz * deltaTimeFloat;
    meshPositionL += velocityDelta;

    // Get world orientation with angular velocity
    mat3 worldOrientation = calculatePhysicsOrientation(meshData.orientation, meshData.angVel, deltaTimeFloat);

    // Apply world transform to locally transformed position
    vec3 worldTransformedPos = applyRotationTransform(worldOrientation, localTransformedPos * meshData.scale.xyz, meshData.centerOfRotation.xyz);

    // Final world position in camera space
    vec4 worldPos = vec4(meshPositionL + worldTransformedPos, 1.0);

    gl_Position = projection * view * worldPos;
}
