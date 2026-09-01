// phong_lighting.glsl
//
// Directional Blinn-Phong model shared by the forward, OIT and deferred
// lighting passes so they shade identically. The strength constants live here
// as the single point of tuning.

// Metallic-roughness parameterization (Burley 2012; Karis 2013, "Real Shading
// in Unreal Engine 4"; glTF 2.0). Almost every dielectric reflects about this
// much head-on, so one number covers all of them, and a conductor reflects its
// albedo instead -- which is why a metal tints its highlight and plastic does
// not, and why a metal has no diffuse term to tint.
const float k_dielectricF0 = 0.04;

const float k_ambient = 0.3;
const float k_pi = 3.141592653589793;

// Reflectance at normal incidence: the one value that separates the two
// families of material.
vec3 materialF0(vec3 albedo, float metallic) {
   return mix(vec3(k_dielectricF0), albedo, metallic);
}

// Roughness as the exponent of a normalized Blinn-Phong lobe. The floor keeps
// the exponent off infinity at a mirror.
float blinnExponent(float roughness) {
   float r = max(roughness, 0.02);
   return 2.0 / (r * r * r * r) - 2.0;
}

// Schlick's approximation (Schlick 1994). Every surface turns mirror at grazing
// angles; a conductor starts most of the way there already, so the ramp is most
// of the story for a dielectric and almost none of it for a metal.
vec3 fresnelSchlick(vec3 f0, float cosTheta) {
   float g = 1.0 - clamp(cosTheta, 0.0, 1.0);
   return f0 + (1.0 - f0) * (g * g * g * g * g);
}

// normal, lightDir and viewDir are unit view-space vectors. ambientScale and
// diffuseSpecularScale fold in the per-pass visibility terms (SSAO, shadowing).
vec3 phongLighting(vec3 albedo, float roughness, float metallic, vec3 normal, vec3 lightDir,
                   vec3 viewDir, float ambientScale, float diffuseSpecularScale) {
   // Stands in for the light arriving from everywhere but the source, which a
   // dielectric scatters and a conductor reflects; either way it takes the
   // albedo's colour, so one term serves both.
   vec3 ambient = k_ambient * albedo * ambientScale;

   // A conductor absorbs what crosses its surface, so nothing scatters back out.
   float nDotL = max(dot(normal, lightDir), 0.0);
   vec3 diffuse = nDotL * albedo * (1.0 - metallic);

   // Blinn-Phong (Blinn 1977): the half vector measures how far the surface is
   // from mirroring the light, and stays well behaved at the grazing angles
   // where a reflection vector pinches the highlight into a sliver. The
   // (a + 8) / 8pi factor normalizes the lobe with the geometry term folded in,
   // so a tighter highlight is brighter in the proportion it is narrower.
   vec3 halfway = normalize(lightDir + viewDir);
   float exponent = blinnExponent(roughness);
   float lobe = (exponent + 8.0) / (8.0 * k_pi) *
      pow(max(dot(normal, halfway), 0.0), exponent);
   vec3 specular =
      fresnelSchlick(materialF0(albedo, metallic), dot(halfway, viewDir)) * lobe * nDotL;

   return ambient + (diffuse + specular) * diffuseSpecularScale;
}
