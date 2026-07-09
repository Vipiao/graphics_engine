// ray_volume_vertex_shader.vert
#version 460 core

// Proxy-geometry vertex stage for volumetric effects. Transforms the proxy mesh
// into camera-relative view space (Dekker-compensated) exactly like the instance
// stage, and forwards the per-instance animated values so the fragment scaffold
// can shade the volume along the view ray.

struct MeshData {
   vec4 positionHigh;
   vec4 positionLow;
   vec4 velocity;
   vec4 orientation;         // Quaternion
   vec4 angVel;              // Unit axis (xyz), angle rate (w)
   vec4 centerOfRotation;
   vec4 scale;               // xyz = scale, w = padding
   uint time;
   int colorTextureUnit;
   int normalTextureUnit;
   int materialTextureUnit;
   float emissiveScalar;
   int maskTextureUnit;
   uint padding[2];
};

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

mat3 fromQuaternion(vec4 q) {
   float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
   float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
   float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
   return mat3(
      1.0 - 2.0*(yy+zz),       2.0*(xy+wz),       2.0*(xz-wy),
            2.0*(xy-wz), 1.0 - 2.0*(xx+zz),       2.0*(yz+wx),
            2.0*(xz+wy),       2.0*(yz-wx), 1.0 - 2.0*(xx+yy));
}

mat3 rotationMatrix(float angle, vec3 axis) {
   float c = cos(angle), s = sin(angle);
   float x = axis.x, y = axis.y, z = axis.z;
   return mat3(
      c + x*x*(1.0-c),     y*x*(1.0-c) + z*s, z*x*(1.0-c) - y*s,
      x*y*(1.0-c) - z*s,   c + y*y*(1.0-c),   z*y*(1.0-c) + x*s,
      x*z*(1.0-c) + y*s,   y*z*(1.0-c) - x*s, c + z*z*(1.0-c));
}

// Dekker subtraction: result = a - b, carrying the low-order error term.
vec3 dekkerSubtract(vec3 aHigh, vec3 aLow, vec3 bHigh, vec3 bLow) {
   precise vec3 r = aHigh - bHigh;
   precise vec3 error;
   for (int i = 0; i < 3; i++) {
      if (abs(aHigh[i]) > abs(bHigh[i])) {
         error[i] = aHigh[i] - r[i] - bHigh[i] + aLow[i] - bLow[i];
      } else {
         error[i] = -bHigh[i] - r[i] + aHigh[i] + aLow[i] - bLow[i];
      }
   }
   return r + error;
}

void main() {
   MeshData meshData = meshDataBuffer[ssboIndex];

   uint deltaTime = u_time - meshData.time;
   float deltaTimeFloat = float(deltaTime) + u_timeRemainder;

   // Local instance transform
   mat3 localRot = fromQuaternion(localOrientation);
   vec3 localPos = localRot * (position * localScale) + localPosition;

   // Mesh world transform in camera-relative space, with physics interpolation
   vec3 meshPositionL = dekkerSubtract(
      meshData.positionHigh.xyz, meshData.positionLow.xyz,
      u_cameraPositionHigh, u_cameraPositionLow);
   meshPositionL += meshData.velocity.xyz * deltaTimeFloat;

   mat3 worldOrientation =
      rotationMatrix(meshData.angVel.w * deltaTimeFloat, meshData.angVel.xyz)
      * fromQuaternion(meshData.orientation);

   vec3 cor = meshData.centerOfRotation.xyz;
   vec3 worldPos =
      worldOrientation * (localPos * meshData.scale.xyz - cor) + cor;
   vec3 worldCenter =
      worldOrientation * (localPosition * meshData.scale.xyz - cor) + cor;

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
