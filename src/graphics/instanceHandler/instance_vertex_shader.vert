// instance_vertex_shader.vert
#version 460 core

struct MeshData {
   vec4 positionHigh;        // Offset= 0, size=16 bytes.
   vec4 positionLow;         // Offset=16, size=16 bytes.
   vec4 velocity;            // Offset=32, size=16 bytes.
   vec4 orientation;         // Offset=48, size=16 bytes. Quaternion
   vec4 angVel;              // Offset=64, size=16 bytes. Unit axis (xyz)
   vec4 centerOfRotation;    // Offset=80, size=16 bytes.
   vec4 scale;               // Offset=96, size=16 bytes. (xyz = scale, w = padding)
   uint time;                // Offset=112, size= 4 bytes.
   int colorTextureUnit;     // Offset=116, size= 4 bytes. (-1 means no textures)
   int normalTextureUnit;    // Offset=120, size= 4 bytes. (-1 means no textures)
   int materialTextureUnit;  // Offset=124, size= 4 bytes. (-1 means no textures)
   float emissiveScalar;     // Offset=128, size= 4 bytes.
   int maskTextureUnit;      // Offset=132, size= 4 bytes. (-1 means no mask texture)
   uint padding[2];          // Offset=136, size= 8 bytes. Padding to make total size 144 (divisible by 16)
};

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
layout (location = 8) in int meshIndex;
layout (location = 9) in int instanceColorTextureUnit;
layout (location = 10) in int instanceNormalTextureUnit;
layout (location = 11) in int instanceMaterialTextureUnit;
layout (location = 12) in int instanceMaskTextureUnit;

uniform uint u_frame;
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

mat3 fromQuaternion(vec4 quaternion) {
    float qw = quaternion.w;
    float qx = quaternion.x;
    float qy = quaternion.y;
    float qz = quaternion.z;

    // Calculate coefficients
    float xx = qx * qx;
    float yy = qy * qy;
    float zz = qz * qz;
    float xy = qx * qy;
    float xz = qx * qz;
    float yz = qy * qz;
    float wx = qw * qx;
    float wy = qw * qy;
    float wz = qw * qz;

    return mat3(
       1.0 - 2.0 * (yy + zz),       2.0 * (xy + wz),       2.0 * (xz - wy),
             2.0 * (xy - wz), 1.0 - 2.0 * (xx + zz),       2.0 * (yz + wx),
             2.0 * (xz + wy),       2.0 * (yz - wx), 1.0 - 2.0 * (xx + yy)
    );
}

mat3 rotationMatrix(float angle, vec3 unitAxis) {
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
    float ux = unitAxis.x;
    float uy = unitAxis.y;
    float uz = unitAxis.z;

    return mat3(
        cosAngle + ux * ux * (1.0 - cosAngle),
        uy * ux * (1.0 - cosAngle) + uz * sinAngle,
        uz * ux * (1.0 - cosAngle) - uy * sinAngle,

        ux * uy * (1.0 - cosAngle) - uz * sinAngle,
        cosAngle + uy * uy * (1.0 - cosAngle),
        uz * uy * (1.0 - cosAngle) + ux * sinAngle,

        ux * uz * (1.0 - cosAngle) + uy * sinAngle,
        uy * uz * (1.0 - cosAngle) - ux * sinAngle,
        cosAngle + uz * uz * (1.0 - cosAngle)
    );
}

// Dekker subtraction: result = a - b
vec3 dekkerSubtract(vec3 aHigh, vec3 aLow, vec3 bHigh, vec3 bLow) {
    precise vec3 r = aHigh - bHigh;
    precise vec3 error;
    for(int i = 0; i < 3; i++) {
        if(abs(aHigh[i]) > abs(bHigh[i])) {
            error[i] = aHigh[i] - r[i] - bHigh[i] + aLow[i] - bLow[i];
        } else {
            error[i] = -bHigh[i] - r[i] + aHigh[i] + aLow[i] - bLow[i];
        }
    }
    return r + error;
}

mat3 calculatePhysicsOrientation(vec4 baseOrientation, vec4 angVel, float deltaTime) {
    mat3 orientation = fromQuaternion(baseOrientation);
    return rotationMatrix(angVel.w * deltaTime, angVel.xyz) * orientation;
}

vec3 applyRotationTransform(mat3 orientation, vec3 position, vec3 centerOfRotation) {
    return orientation * (position - centerOfRotation) + centerOfRotation;
}

mat3 buildTBNMatrix(mat3 orientation, vec3 normal, vec3 tangent) {
    vec3 N = normalize(orientation * normal);
    vec3 T = normalize(orientation * tangent);
    
    // Re-orthogonalize T with respect to N using Gram-Schmidt process
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    
    return mat3(T, B, N);
}

void main() {
    // Fully transparent instances are hidden from the camera but still cast shadows:
    // the depth pass uses its own vertex shader which never reads instanceColor.
    if (instanceColor.a < 1.0 / 255.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // Outside clip volume, vertex is culled
        return;
    }

    // Get geometry world transform from SSBO using instance's meshIndex
    MeshData meshData = meshDataBuffer[meshIndex];
    
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