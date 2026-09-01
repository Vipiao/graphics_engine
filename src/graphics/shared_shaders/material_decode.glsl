// material_decode.glsl
//
// Shared surface-material decode for the fragment shaders fed by the mesh and
// instance vertex stages: resolves the interpolated vertex outputs into the
// final surface values (normal, masked albedo, alpha, emissive strength and the
// two material terms) that every pass shades from. Callers declare the varyings
// and the texture-array uniform and pass them in.

struct SurfaceMaterial {
   vec3 normal;             // unit view-space normal (normal mapped if present)
   vec3 color;              // albedo after the color-mask tint
   float alpha;
   float emissiveStrength;  // 0 = fully lit, 1 = fully emissive
   float roughness;         // 0 = mirror, 1 = fully matte
   float metallic;          // 0 = dielectric, 1 = conductor
};

// What a surface shades at when no material texture names a roughness: the lobe
// phong_lighting.glsl normalizes around, expressed as a roughness.
const float k_defaultRoughness = 0.35;

// Callers should discard fragments whose alpha is below 1/255. The texture
// unit parameters index textures; -1 means the texture is not present.
SurfaceMaterial decodeSurfaceMaterial(
   sampler2D textures[MAX_TEXTURE_UNITS],
   vec3 vertexNormal, mat3 TBN, vec2 uv, vec4 color,
   int colorTextureUnit, int normalTextureUnit, int materialTextureUnit,
   int maskTextureUnit, float emissiveScalar
) {
   SurfaceMaterial surface;

   // Get normal from normal map or use vertex normal
   if (normalTextureUnit == -1) {
      surface.normal = normalize(vertexNormal);
   } else {
      vec3 normalMap = texture(textures[normalTextureUnit], uv).rgb;
      normalMap = normalMap * 2.0 - 1.0;
      surface.normal = normalize(TBN * normalMap);
   }

   // Get base albedo from texture or vertex color (no HSV shift)
   vec3 objectColor;
   if (colorTextureUnit >= 0) {
      vec4 textureColor = texture(textures[colorTextureUnit], uv);
      objectColor = textureColor.rgb;
      surface.alpha = textureColor.a * color.a;
   } else {
      objectColor = color.rgb;
      surface.alpha = color.a;
   }

   // Apply color mask: blend between unmodified albedo and albedo tinted by color
   float mask = (maskTextureUnit >= 0)
      ? texture(textures[maskTextureUnit], uv).r
      : 1.0;
   surface.color = mix(objectColor, objectColor * color.rgb, mask);

   // The material texture carries R: emissive mask, G: roughness, B: metallic.
   // One fetch serves all three; without the texture the surface is a plain
   // dielectric at the neutral lobe, emissive only as its scalar says.
   if (materialTextureUnit == -1) {
      surface.emissiveStrength = emissiveScalar;
      surface.roughness = k_defaultRoughness;
      surface.metallic = 0.0;
   } else {
      vec3 material = texture(textures[materialTextureUnit], uv).rgb;
      surface.emissiveStrength = material.r * emissiveScalar;
      surface.roughness = material.g;
      surface.metallic = material.b;
   }

   return surface;
}
