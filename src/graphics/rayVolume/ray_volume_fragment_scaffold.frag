// ray_volume_fragment_scaffold.frag
#version 460 core

// Scaffold for proxy-geometry volumetric effects. It reconstructs the view ray
// and the opaque scene depth, hands them to an injected shading body, and writes
// the result into the Weighted Blended OIT accumulation targets so the effect
// composites with all other transparency. The game supplies the body at the
// injection marker below; the body must define rayVolumeShade with the
// signature declared there.

#include "../shared_shaders/wboit_weight.glsl"

layout(location = 0) out vec4 accum;
layout(location = 1) out float revealage;

in vec3 vert_viewPos;
in vec2 vert_uv;
flat in vec4 vert_color;
flat in vec4 vert_value;
flat in vec3 vert_centerViewPos;
flat in mat3 vert_viewBasis;   // instance orientation: maps local -> view directions

uniform sampler2D u_sceneDepth;      // G-buffer depth (opaque scene)
uniform sampler2D u_opaqueColor;     // lit opaque color behind this pixel
uniform vec2 u_screenSize;
uniform mat4 u_inverseProjection;
uniform uint u_time;                 // fixed-step tick index of the current frame
uniform float u_timeRemainder;       // fraction of a tick elapsed at this frame

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

// Animation clocks for the body, both measured in the application's fixed
// simulation ticks:
//   physicsTime : advances one whole tick at a time, so it carries no render
//                 interpolation and reads the same on every peer of a session
//   frameTime   : physicsTime plus the interpolation within the current tick,
//                 so it advances smoothly with the frame rate
// Both wrap every k_timeWrapTicks, which keeps float sub-tick resolution however
// long a session runs; animation periodic over that window hides the wrap.
const uint k_timeWrapTicks = 1048576u;   // 2^20 ticks

float physicsTime() {
   return float(u_time & (k_timeWrapTicks - 1u));
}

float frameTime() {
   return physicsTime() + u_timeRemainder;
}

// The injected shading body defines:
//   RayVolumeResult rayVolumeShade(
//      vec3 viewPos, vec3 rayDir, float backDepth, float sceneDepth,
//      vec3 opaqueColor, vec4 value, vec4 color, vec2 uv,
//      vec3 centerViewPos, mat3 viewBasis);
// viewBasis maps a local-space direction to view space; transpose(viewBasis)
// takes a view-space vector into the instance's local frame. opaqueColor is the
// lit opaque color behind this pixel: returning (opaqueColor + emission) as the
// result color makes the WBOIT over-blend resolve to opaque + emission*alpha,
// i.e. additive (exact for a single layer; approximate when overlapping other
// transparency).
__RAY_VOLUME_BODY__

void main() {
   vec3 rayDir = normalize(vert_viewPos);
   float backDepth = -vert_viewPos.z;   // exit depth of the proxy (positive)
   float sceneDepth = sceneViewDepth();
   vec3 opaqueColor = texelFetch(u_opaqueColor, ivec2(gl_FragCoord.xy), 0).rgb;

   RayVolumeResult r = rayVolumeShade(
      vert_viewPos, rayDir, backDepth, sceneDepth,
      opaqueColor, vert_value, vert_color, vert_uv,
      vert_centerViewPos, vert_viewBasis);

   if (r.alpha < 1.0 / 255.0) discard;

   float weight = wboitWeight(r.alpha, max(r.weightDepth, 1e-4));

   accum = vec4(r.color * r.alpha, r.alpha) * weight;
   revealage = r.alpha;
}
