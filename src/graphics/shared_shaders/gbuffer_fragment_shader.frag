#version 460 core

layout(location = 0) out vec4 gAlbedo;    // RGB: albedo, A: metallic
layout(location = 1) out vec4 gNormal;    // RGB: world normal, A: roughness
layout(location = 2) out vec4 gMaterial;  // R: emissive, G: texture flags, B: occlusion, A: alpha

uniform sampler2D u_textures[32];

in vec3 vert_normal;
in mat3 vert_TBN;
in vec3 vert_pos;
in vec2 vert_uv;
in vec4 vert_color;
flat in int vert_colorTextureUnit;
flat in int vert_normalTextureUnit;
in float vert_occlusionFactor;
flat in int vert_materialTextureUnit;
flat in float vert_emissiveScalar;
flat in int vert_maskTextureUnit;

void main() {
   // Get normal from normal map or use vertex normal
   vec3 normal;
   if (vert_normalTextureUnit == -1) {
      normal = normalize(vert_normal);
   } else {
      vec3 normalMap = texture(u_textures[vert_normalTextureUnit], vert_uv).rgb;
      normalMap = normalMap * 2.0 - 1.0;
      normal = normalize(vert_TBN * normalMap);
   }

   // Get base albedo from texture or vertex color (no HSV shift)
   vec3 objectColor;
   float alpha = 1.0;
   if (vert_colorTextureUnit >= 0) {
      vec4 textureColor = texture(u_textures[vert_colorTextureUnit], vert_uv);
      objectColor = textureColor.rgb;
      alpha = textureColor.a;
   } else {
      objectColor = vert_color.rgb;
      alpha = vert_color.a;
   }

   if(alpha < 1./255.) discard;

   // Apply color mask: blend between unmodified albedo and albedo tinted by vert_color
   float mask = (vert_maskTextureUnit >= 0)
      ? texture(u_textures[vert_maskTextureUnit], vert_uv).r
      : 1.0;
   vec3 finalColor = mix(objectColor, objectColor * vert_color.rgb, mask);

   // Calculate emissive strength
   float emissiveStrength;
   if (vert_emissiveScalar < 0.001) {
      emissiveStrength = 0.0;
   } else if (vert_materialTextureUnit == -1) {
      emissiveStrength = vert_emissiveScalar;
   } else {
      float textureValue = texture(u_textures[vert_materialTextureUnit], vert_uv).r;
      emissiveStrength = textureValue * vert_emissiveScalar;
   }

   // Output to G-buffer
   gAlbedo = vec4(finalColor, 0.0);  // A: metallic factor (hardcoded to 0 for now)
   gNormal = vec4(normal * 0.5 + 0.5, 0.5);  // Encode normal to [0,1], A: roughness
   gMaterial = vec4(emissiveStrength, 1.0, vert_occlusionFactor, alpha);  // R: emissive, G: geometry flag, B: occlusion, A: alpha
}
