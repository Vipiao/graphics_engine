// cdlod_gbuffer_fragment_shader.frag
#version 460 core

// G-buffer stage for CDLOD bodies. It exists separately from the shared one
// because the surface normal is evaluated here rather than interpolated from
// the vertices: the injected surface body is compiled into this stage too, and
// asked for the normal at this pixel's own point on the sphere. Shading detail
// then follows the pixels instead of the patch's vertex spacing, and stops
// changing with the level the patch happened to be drawn at.
//
// Included for the surface hook; the patch and morph machinery it also declares
// belongs to the vertex stages and goes unused here.
#include "cdlod_patch.glsl"

layout(location = 0) out vec4 gAlbedo;    // RGB: albedo, A: metallic
layout(location = 1) out vec4 gNormal;    // RGB: view normal, A: roughness
layout(location = 2) out vec4 gMaterial;  // R: emissive, G: geometry flag, B: occlusion, A: alpha

in vec3 vert_spherePosition;
flat in float vert_halfExtent;
flat in mat3 vert_bodyToView;
in vec4 vert_color;
flat in float vert_emissiveScalar;

void main() {
   // Interpolating across a triangle lands inside the sphere rather than on it,
   // so the direction is what survives; the radius comes back from the body.
   vec3 spherePosition = normalize(vert_spherePosition) * vert_halfExtent;

   vec3 viewNormal = normalize(vert_bodyToView * cdlodDisplacedNormal(spherePosition));

   if (vert_color.a < 1.0 / 255.0) discard;

   gAlbedo = vec4(vert_color.rgb, 0.0);
   gNormal = vec4(viewNormal * 0.5 + 0.5, 0.5);
   // Untextured, and unoccluded until the surface grows a material.
   gMaterial = vec4(vert_emissiveScalar, 1.0, 1.0, vert_color.a);
}
