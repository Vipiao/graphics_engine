#version 460 core

#include "material_decode.glsl"

layout(location = 0) out vec4 gAlbedo;    // RGB: albedo, A: metallic
layout(location = 1) out vec4 gNormal;    // RGB: world normal, A: roughness
layout(location = 2) out vec4 gMaterial;  // R: emissive, G: geometry flag, B: unused, A: alpha

uniform MaterialSampler u_textures[MAX_TEXTURE_UNITS];
// Alpha below it is cut out; passes that cut harder set it
uniform float u_alphaCutoff = 1.0 / 255.0;

in vec3 vert_normal;
in mat3 vert_TBN;
in vec3 vert_pos;
in MaterialUV vert_uv;
in vec4 vert_color;
flat in int vert_colorTextureUnit;
flat in int vert_normalTextureUnit;
flat in int vert_materialTextureUnit;
flat in float vert_emissiveScalar;
flat in int vert_maskTextureUnit;

// A depth pre-pass: DEPTH_ONLY cuts and writes depth alone (the reads only the
// outputs need are compiled away), then EARLY_FRAGMENT_TESTS shades what passed.
#ifdef EARLY_FRAGMENT_TESTS
layout(early_fragment_tests) in;
#endif

void main() {
   SurfaceMaterial surface = decodeSurfaceMaterial(
      u_textures, vert_normal, vert_TBN, vert_uv, vert_color,
      vert_colorTextureUnit, vert_normalTextureUnit, vert_materialTextureUnit,
      vert_maskTextureUnit, vert_emissiveScalar);

   if (surface.alpha < u_alphaCutoff) discard;
#ifdef DEPTH_ONLY
   return;
#endif

   // Output to G-buffer
   gAlbedo = vec4(surface.color, surface.metallic);
   gNormal = vec4(surface.normal * 0.5 + 0.5, surface.roughness);  // Encode normal to [0,1]
   gMaterial = vec4(surface.emissiveStrength, 1.0, 0.0, surface.alpha);
}
