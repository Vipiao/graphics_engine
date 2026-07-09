// ray_volume_default_body.glsl
//
// Default ray-volume body: a uniform gas sphere. No surface shading - the proxy
// is treated purely as a volume. The view ray is intersected analytically with
// the sphere, and the opacity comes from how far the ray travels through the gas
// (Beer-Lambert), clamped against the opaque scene so it fades into surfaces.
// The sphere is defined in view space by its center. Its radius comes from the
// custom value channel (value.y); if unset, it falls back to the proxy mesh's
// own size (the back face lies on the sphere, so its distance to the center is
// the radius). Author the proxy mesh at least as large as the radius so its
// silhouette does not clip the soft edges.

RayVolumeResult rayVolumeShade(
   vec3 viewPos, vec3 rayDir, float backDepth, float sceneDepth,
   vec3 opaqueColor, vec4 value, vec4 color, vec2 uv,
   vec3 centerViewPos, mat3 viewBasis)
{
   RayVolumeResult res;
   res.color = vec3(0.0);
   res.alpha = 0.0;
   res.weightDepth = -centerViewPos.z;

   vec3 c = centerViewPos;
   float radius = value.y > 0.0 ? value.y : length(viewPos - centerViewPos);

   // Ray from the camera (origin) along rayDir (unit). Solve
   // t^2 - 2 (rayDir·c) t + (|c|^2 - r^2) = 0 for the entry/exit distances.
   float b = dot(rayDir, c);
   float disc = b * b - (dot(c, c) - radius * radius);
   if (disc <= 0.0) {
      return res;   // ray misses the sphere (silhouette); scaffold discards
   }
   float sq = sqrt(disc);
   float tNear = b - sq;
   float tFar = b + sq;

   // Distance along the ray at which the opaque scene is hit (depth is measured
   // along -z, so convert with the ray's z component).
   float tScene = sceneDepth / max(-rayDir.z, 1e-4);

   // Visible span of gas: clamp against the camera (inside the sphere) and the
   // opaque scene in front of the far side.
   float entry = max(tNear, 0.0);
   float exit = min(tFar, tScene);
   float chord = max(exit - entry, 0.0);
   if (chord <= 0.0) {
      return res;
   }

   // Uniform density: opacity grows with the traversed thickness, normalized by
   // the radius so the look is scale independent. value.x tunes density.
   float density = value.x > 0.0 ? value.x : 1.0;
   float alpha = 1.0 - exp(-density * chord / max(radius, 1e-4));

   // Add opaqueColor for additive blending (approximation).
   res.color = color.rgb; // + opaqueColor;
   res.alpha = clamp(alpha * color.a, 0.0, 1.0);
   res.weightDepth = entry * max(-rayDir.z, 1e-4);   // depth of the near gas edge
   return res;
}
