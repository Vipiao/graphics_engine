// instance_vertex_shader.vert
#version 460 core

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

// Per-instance attributes (from instance VBO)
layout (location = 4) in vec3 localPosition;
layout (location = 5) in vec4 localOrientation;
layout (location = 6) in vec3 localScale;
layout (location = 7) in vec4 instanceColor;
layout (location = 8) in int ssboIndex;
layout (location = 9) in int instanceColorTextureUnit;
layout (location = 10) in int instanceNormalTextureUnit;
layout (location = 11) in int instanceMaterialTextureUnit;
layout (location = 12) in int instanceMaskTextureUnit;

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
    // Fully transparent instances are hidden from the camera but still cast shadows:
    // the depth pass uses its own vertex shader which never reads instanceColor.
    if (instanceColor.a < 1.0 / 255.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // Outside clip volume, vertex is culled
        return;
    }

    // Get geometry world transform from SSBO using instance's ssboIndex
    MeshData meshData = meshDataBuffer[ssboIndex];

    // Calculate time delta
    uint deltaTime = u_time - meshData.time;
    float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

    // Step 1: Apply local instance transform to geometry vertex
    vec3 scaledPosition = position * localScale;
    mat3 localOrientationMatrix = fromQuaternion(localOrientation);
    vec3 localTransformedPos = localOrientationMatrix * scaledPosition + localPosition;

    // Apply local transform to normals and tangents
    vec3 localTransformedNormal = localOrientationMatrix * normal;
    vec3 localTransformedTangent = localOrientationMatrix * tangent;

    // Step 2: Get world geometry transform with physics interpolation
    vec3 meshPositionL = df3ToVec(df3Sub(
        Df3(meshData.positionHigh.xyz, meshData.positionLow.xyz),
        Df3(u_cameraPositionHigh, u_cameraPositionLow)));

    // Add velocity in camera-relative space
    vec3 velocityDelta = meshData.velocity.xyz * deltaTimeFloat;
    meshPositionL += velocityDelta;

    // Get world orientation with angular velocity
    mat3 worldOrientation = calculatePhysicsOrientation(meshData.orientation, meshData.angVel, deltaTimeFloat);

    // Apply world transform to locally transformed position
    vec3 worldTransformedPos = applyRotationTransform(worldOrientation, localTransformedPos * meshData.scale.xyz, meshData.centerOfRotation.xyz);

    // Build TBN matrix
   mat3 worldTBN = buildTBNMatrix(worldOrientation, localTransformedNormal, localTransformedTangent);
   vec3 worldNormal = normalize(worldOrientation * localTransformedNormal);

   // Final world position in camera space
   vec4 worldPos = vec4(meshPositionL + worldTransformedPos, 1.0);

   // Transform to view space
   vec4 viewPos = view * worldPos;
   vert_pos = viewPos.xyz;
   vert_normal = mat3(view) * worldNormal;
   vert_TBN = mat3(view) * worldTBN;

    vert_color = instanceColor;
    vert_uv = uv;

    // Use instance texture units (override geometry ones)
    vert_colorTextureUnit = instanceColorTextureUnit;
    vert_normalTextureUnit = instanceNormalTextureUnit;
    vert_materialTextureUnit = instanceMaterialTextureUnit;
    vert_emissiveScalar = meshData.emissiveScalar;
    vert_maskTextureUnit = instanceMaskTextureUnit;
    vert_occlusionFactor = 1.0; // Default to no occlusion

    gl_Position = projection * viewPos;
}
