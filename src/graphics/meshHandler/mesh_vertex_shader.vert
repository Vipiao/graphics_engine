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
}; // Make sure to pad so size is divisible by 16 because you have a vec4.

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