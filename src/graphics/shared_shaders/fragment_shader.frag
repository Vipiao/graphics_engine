#version 460 core

#include "material_decode.glsl"
#include "phong_lighting.glsl"

out vec4 FragColor;

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

   // Early exit for full emissive materials
   if (surface.emissiveStrength >= 0.999) {
      FragColor = vec4(surface.color, surface.alpha);
      return;
   }

   // Calculate light and view directions (directional light)
   vec3 lightDir = normalize(-u_lightDir);
   vec3 viewDir = normalize(-vert_pos);

   // Forward pass: no ambient occlusion and no shadow map, so nothing dims
   // either term.
   vec3 result = phongLighting(surface.color, surface.roughness, surface.metallic,
      surface.normal, lightDir, viewDir, 1.0, 1.0);

   result = mix(result, surface.color, surface.emissiveStrength);

   FragColor = vec4(result, surface.alpha);
}
