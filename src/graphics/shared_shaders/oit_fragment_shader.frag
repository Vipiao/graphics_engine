#version 460 core

// Weighted Blended Order-Independent Transparency (McGuire & Bavoil 2013).
// Each transparent fragment contributes independently to two accumulation
// targets, so the pass is order independent and needs no sorting:
//   accum     (additive)       : weighted, premultiplied color and weighted alpha
//   revealage (multiplicative) : running product of (1 - alpha)
// A later fullscreen pass reconstructs the blended result over the lit scene.

#include "material_decode.glsl"
#include "phong_lighting.glsl"
#include "wboit_weight.glsl"

layout(location = 0) out vec4 accum;
layout(location = 1) out float revealage;

uniform vec3 u_lightDir;

uniform sampler2D u_textures[MAX_TEXTURE_UNITS];

in vec3 vert_normal;
in mat3 vert_TBN;
in vec3 vert_pos;
in vec2 vert_uv;
in vec4 vert_color;
flat in int vert_colorTextureUnit;
flat in int vert_normalTextureUnit;
flat in int vert_materialTextureUnit;
flat in float vert_emissiveScalar;
flat in int vert_maskTextureUnit;

void main() {
   SurfaceMaterial surface = decodeSurfaceMaterial(
      u_textures, vert_normal, vert_TBN, vert_uv, vert_color,
      vert_colorTextureUnit, vert_normalTextureUnit, vert_materialTextureUnit,
      vert_maskTextureUnit, vert_emissiveScalar);

   if (surface.alpha < 1.0 / 255.0) discard;

   // Directional lighting, matching the forward/opaque shading model.
   vec3 result;
   if (surface.emissiveStrength >= 0.999) {
      result = surface.color;
   } else {
      vec3 lightDir = normalize(-u_lightDir);
      vec3 viewDir = normalize(-vert_pos);

      result = phongLighting(surface.color, surface.roughness, surface.metallic,
         surface.normal, lightDir, viewDir, 1.0, 1.0);
      result = mix(result, surface.color, surface.emissiveStrength);
   }

   float weight = wboitWeight(surface.alpha, -vert_pos.z);

   accum = vec4(result * surface.alpha, surface.alpha) * weight;
   revealage = surface.alpha;
}
