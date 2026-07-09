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
   uint padding[3];          // Offset=132, size=12 bytes. Padding to make total size 144 (divisible by 16)
};

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

uniform uint u_frame;
uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;

uniform mat4 view;
uniform mat4 projection;

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
    vec3 r = aHigh - bHigh;
    vec3 error;
    // Use the larger magnitude for better precision
    for(int i = 0; i < 3; i++) {
        if(abs(aHigh[i]) > abs(bHigh[i])) {
            error[i] = aHigh[i] - r[i] - bHigh[i] + aLow[i] - bLow[i];
        } else {
            error[i] = -bHigh[i] - r[i] + aHigh[i] + aLow[i] - bLow[i];
        }
    }
    return r + error; // Convert back to single precision
}

mat3 calculatePhysicsOrientation(vec4 baseOrientation, vec4 angVel, float deltaTime) {
    mat3 orientation = fromQuaternion(baseOrientation);
    return rotationMatrix(angVel.w * deltaTime, angVel.xyz) * orientation;
}

vec3 applyRotationTransform(mat3 orientation, vec3 position, vec3 centerOfRotation) {
    return orientation * (position - centerOfRotation) + centerOfRotation;
}

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