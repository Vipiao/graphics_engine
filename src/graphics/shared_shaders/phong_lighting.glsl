// phong_lighting.glsl
//
// Directional Phong model shared by the forward, OIT and deferred lighting
// passes so they shade identically. The strength constants live here as the
// single point of tuning.

// normal, lightDir and viewDir are unit view-space vectors. ambientScale and
// diffuseSpecularScale fold in the per-pass visibility terms (occlusion, SSAO,
// shadowing).
vec3 phongLighting(vec3 albedo, vec3 normal, vec3 lightDir, vec3 viewDir,
                   float ambientScale, float diffuseSpecularScale) {
   float ambientStrength = 0.3;
   vec3 ambient = ambientStrength * albedo * ambientScale;

   float diff = max(dot(normal, lightDir), 0.0);
   vec3 diffuse = diff * albedo;

   float specularStrength = 0.5;
   vec3 reflectDir = reflect(-lightDir, normal);
   float spec = pow(max(min(dot(viewDir, reflectDir) + 0.001, 1.0), 0.0), 128.0);
   vec3 specular = specularStrength * spec * vec3(1.0);

   return ambient + (diffuse + specular) * diffuseSpecularScale;
}
