#pragma once

// Scales on the scene's light, applied to every lit surface in the lighting pass.
// 1 is the light at full strength and 0 is none of it. Emissive surfaces ignore
// both.
struct LightIntensity {
   // The light arriving from everywhere but the source
   double m_ambient{ 1.0 };
   // The source's diffuse and specular light, and the reflections that stand in
   // for its bounce off the surroundings
   double m_direct{ 1.0 };
};
