// ray_volume_fragment_scaffold.frag
#version 460 core

// Scaffold for proxy-geometry volumetric effects. It reconstructs the view ray
// and the opaque scene depth, hands them to an injected shading body, and writes
// the result into the Weighted Blended OIT accumulation targets so the effect
// composites with all other transparency. The game supplies the body at the
// injection marker below; the body must define rayVolumeShade with the
// signature declared there.

#include "../shared_shaders/wboit_weight.glsl"
#include "../shared_shaders/dekker_arithmetic.glsl"
// For bodies that shade a surface the way the lighting passes do
#include "../shared_shaders/phong_lighting.glsl"

layout(location = 0) out vec4 accum;
layout(location = 1) out float revealage;

in vec3 vert_exitViewPos;
in vec2 vert_uv;
flat in vec4 vert_color;
flat in vec4 vert_value;
flat in vec3 vert_centerViewPos;
flat in vec2 vert_centerDistance;
flat in mat3 vert_rayVolumeSpaceToView;   // instance orientation: local -> view directions
flat in vec3 vert_cameraLocalHigh;
flat in vec3 vert_cameraLocalLow;

uniform sampler2D u_sceneDepth;      // G-buffer depth (opaque scene)
uniform sampler2D u_opaqueColor;     // lit opaque color behind this pixel
uniform vec2 u_screenSize;
uniform mat4 u_inverseProjection;
uniform uint u_time;                 // physics tick count, not frames; see wrappedPhysicsTime
uniform float u_timeRemainder;       // how far into the next physics tick this frame is
uniform float u_ambientScale;        // the scene's ambient light, 1 at full strength
uniform float u_directScale;         // the scene's direct light, 1 at full strength
uniform vec3 u_lightDir;             // view space, the way the light travels
uniform vec3 u_skyColor;             // the background, for bodies that reflect it

// Result of the injected shading body.
//   color       : straight (non-premultiplied) RGB
//   alpha       : coverage in [0,1]
//   weightDepth : positive view-space depth used for the WBOIT weight; pick the
//                 depth where the visible mass sits (default: instance center)
struct RayVolumeResult {
   vec3 color;
   float alpha;
   float weightDepth;
};

// Positive view-space depth of the opaque scene at this pixel. The depth buffer
// is the same resolution as the output, so the lookup is 1:1: fetch the exact
// texel (no filtering) rather than sampling, which also skips the sampler unit.
float sceneViewDepth() {
   vec2 uv = gl_FragCoord.xy / u_screenSize;
   float d = texelFetch(u_sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
   vec4 ndc = vec4(uv * 2.0 - 1.0, d, 1.0);
   vec4 viewH = u_inverseProjection * ndc;
   return -(viewH.z / viewH.w);
}

// Animation clocks for the body. Both are physics time, counted in the
// application's fixed simulation ticks rather than in frames or seconds; the
// physics runs at its own fixed rate, independent of the display's.
//   wrappedPhysicsTicks : whole ticks only, so it steps at the physics rate and
//                         reads the same on every peer of a session
//   wrappedPhysicsTime  : the same plus the fraction of the tick this frame sits
//                         at, so it advances smoothly on any display
// Both wrap every k_timeWrapTicks, which keeps float sub-tick resolution however
// long a session runs; animation periodic over that window hides the wrap.
const uint k_timeWrapTicks = 1048576u;   // 2^20 ticks

float wrappedPhysicsTicks() {
   return float(u_time & (k_timeWrapTicks - 1u));
}

float wrappedPhysicsTime() {
   return wrappedPhysicsTicks() + u_timeRemainder;
}

// The injected shading body defines:
//   RayVolumeResult rayVolumeShade(
//      vec3 rayDir, float exitDistance, float sceneDistance,
//      vec3 opaqueColor, vec4 value, vec4 color, vec2 uv,
//      vec3 centerViewPos, Df centerDistance,
//      mat3 rayVolumeSpaceToView, Df3 cameraLocalPosition);
// The camera sits at the view-space origin and rayDir is the unit view ray, so a
// point the ray reaches is rayDir * t. exitDistance is the t where the ray leaves
// the proxy, sceneDistance the t where it meets the opaque scene.
// centerDistance is the length of centerViewPos carried wide. To compare it with
// a nearby value (a radius, say), subtract that from hi, then add lo.
// rayVolumeSpaceToView maps a direction in the instance's own frame to view
// space; its transpose takes a view-space vector back. cameraLocalPosition is the
// camera in that frame, in metres, carried wide: a point the ray reaches is this
// plus transpose(rayVolumeSpaceToView) * (rayDir * t), and anything laid out in
// this frame, such as a pattern on a spinning planet, stays put on the body.
// opaqueColor is the
// lit opaque color behind this pixel: returning (opaqueColor + emission) as the
// result color makes the WBOIT over-blend resolve to opaque + emission*alpha,
// i.e. additive (exact for a single layer; approximate when overlapping other
// transparency).
__RAY_VOLUME_BODY__

void main() {
   float exitDistance = length(vert_exitViewPos);
   vec3 rayDir = vert_exitViewPos / exitDistance;
   // Depth is measured along -z, so the ray's z component turns it into a t
   float sceneDistance = sceneViewDepth() / max(-rayDir.z, 1e-4);
   vec3 opaqueColor = texelFetch(u_opaqueColor, ivec2(gl_FragCoord.xy), 0).rgb;

   RayVolumeResult r = rayVolumeShade(
      rayDir, exitDistance, sceneDistance,
      opaqueColor, vert_value, vert_color, vert_uv,
      vert_centerViewPos, Df(vert_centerDistance.x, vert_centerDistance.y),
      vert_rayVolumeSpaceToView, Df3(vert_cameraLocalHigh, vert_cameraLocalLow));

   if (r.alpha < 1.0 / 255.0) discard;

   float weight = wboitWeight(r.alpha, max(r.weightDepth, 1e-4));

   accum = vec4(r.color * r.alpha, r.alpha) * weight;
   revealage = r.alpha;
}
