#version 460 core

out vec4 FragColor;

uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;
uniform sampler2D u_textures[MAX_TEXTURE_UNITS];
uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_lightDir;

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
      alpha = textureColor.a * vert_color.a;
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

   // Calculate emissive strength based on scalar and texture
   float emissiveStrength;
   if (vert_emissiveScalar < 0.001) {
      emissiveStrength = 0.0;
   } else if (vert_materialTextureUnit == -1) {
      emissiveStrength = vert_emissiveScalar;
   } else {
      float textureValue = texture(u_textures[vert_materialTextureUnit], vert_uv).r;
      emissiveStrength = textureValue * vert_emissiveScalar;
   }

   // Early exit for full emissive materials
   if (emissiveStrength >= 0.999) {
      FragColor = vec4(finalColor, alpha);
      return;
   }

   // Calculate light and view directions (directional light)
   vec3 lightDir = normalize(-u_lightDir);
   vec3 viewDir = normalize(-vert_pos);

   float attenuation = 1.0;

   float ambientStrength = 0.3;
   vec3 ambient = ambientStrength * finalColor;

   float diff = max(dot(normal, lightDir), 0.0);
   vec3 diffuse = diff * finalColor;

   float specularStrength = 0.5;
   vec3 reflectDir = reflect(-lightDir, normal);
   float spec = pow(max(min(dot(viewDir, reflectDir) + 0.001, 1.), 0.0), 128.0);
   vec3 specular = specularStrength * spec * vec3(1.0);

   ambient *= vert_occlusionFactor;
   diffuse *= vert_occlusionFactor;
   specular *= vert_occlusionFactor;

   vec3 result = ambient + (diffuse + specular) * attenuation;

   result = mix(result, finalColor, emissiveStrength);

   FragColor = vec4(result, alpha);
}
