// mesh_transform.glsl
//
// Shared vertex-stage transform toolkit for every pass that renders meshes or
// instances: the per-mesh SSBO entry layout and the pure helpers that turn a
// MeshData entry into a camera-relative world transform with physics
// interpolation (velocity and angular velocity applied on the GPU). Leaf
// shaders declare their own resource bindings (the MeshData SSBO and uniforms).
//
// MeshData is the GPU-side twin of the C++ struct in src/graphics/SSBOManager.h;
// any field change must be mirrored there (a static_assert guards the size).

struct MeshData {
   vec4 positionHigh;        // Offset= 0, size=16 bytes.
   vec4 positionLow;         // Offset=16, size=16 bytes.
   vec4 velocity;            // Offset=32, size=16 bytes.
   vec4 orientation;         // Offset=48, size=16 bytes. Quaternion
   vec4 angVel;              // Offset=64, size=16 bytes. Unit axis (xyz), angle rate (w)
   vec4 centerOfRotation;    // Offset=80, size=16 bytes.
   vec4 scale;               // Offset=96, size=16 bytes. (xyz = scale, w = padding)
   uint time;                // Offset=112, size= 4 bytes.
   float emissiveScalar;     // Offset=116, size= 4 bytes.
   uint padding[2];          // Offset=120, size= 8 bytes. Padding to make total size 128 (divisible by 16)
}; // Make sure to pad so size is divisible by 16 because you have a vec4.

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

// Dekker subtraction: result = a - b, carrying the low-order error term.
// "precise" keeps the compiler from reassociating the error-cancellation
// arithmetic, which would defeat the extended precision.
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
