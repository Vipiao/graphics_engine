// cdlod_gbuffer_fragment_shader.frag
#version 460 core

// G-buffer stage for CDLOD bodies. It exists separately from the shared one
// because the surface normal is evaluated here rather than interpolated from
// the vertices: the injected surface body is compiled into this stage too, and
// asked for the normal at this pixel's own crude point. Shading detail
// then follows the pixels instead of the patch's vertex spacing, and stops
// changing with the level the patch happened to be drawn at.
//
// Included for the surface hook; the patch and morph machinery it also declares
// belongs to the vertex stages and goes unused here.
#include "cdlod_patch.glsl"

layout(location = 0) out vec4 gAlbedo;    // RGB: albedo, A: metallic
layout(location = 1) out vec4 gNormal;    // RGB: view normal, A: roughness
layout(location = 2) out vec4 gMaterial;  // R: emissive, G: geometry flag, B: occlusion, A: alpha

// The node centre, delivered flat and so unrounded, and this pixel's
// displacement from it, which is small enough that interpolating it costs
// nothing. See cdlod_vertex_shader.vert for why the point is split rather than
// sent whole.
flat in vec3 vert_patchCentreHigh;
flat in vec3 vert_patchCentreLow;
in vec3 vert_crudeOffset;
flat in mat3 vert_bodyToView;
in vec4 vert_color;
flat in float vert_emissiveScalar;

void main() {
   // Put back together at full width. The offset is affine in the patch
   // coordinate, so what arrives is exactly the point the surface would be asked
   // for at this pixel.
   //
   // Before the discard, and it must stay there: the surface takes screen
   // derivatives to choose a mip level with, and those are only defined where
   // every pixel of the quad is still running.
   Df3 crudePoint = df3AddVec(Df3(vert_patchCentreHigh, vert_patchCentreLow),
                              vert_crudeOffset);

   // The centre is flat, so the point and the displacement move across the
   // screen at exactly the same rate, and the displacement is small enough to
   // difference without losing the answer to rounding.
   vec3 viewNormal = normalize(vert_bodyToView *
                               cdlodSurfaceNormal(crudePoint, dFdx(vert_crudeOffset),
                                                  dFdy(vert_crudeOffset)));

   if (vert_color.a < 1.0 / 255.0) discard;

   gAlbedo = vec4(vert_color.rgb, 0.0);
   gNormal = vec4(viewNormal * 0.5 + 0.5, 0.5);
   // Untextured, and unoccluded until the surface grows a material.
   gMaterial = vec4(vert_emissiveScalar, 1.0, 1.0, vert_color.a);
}
