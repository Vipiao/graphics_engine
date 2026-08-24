#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// Per-frame camera, timing and projection parameters shared by the render passes.
// The shadow pass reuses this struct with the light's view/projection matrices.
struct FrameRenderParams {
   glm::dmat4 view{ 1.0 };
   glm::dmat4 projection{ 1.0 };
   uint64_t frame{ 0 };
   uint64_t time{ 0 };
   double timeRemainder{ 0.0 };
   glm::dvec3 lightDir{ 0.0, 0.0, -1.0 };
   glm::dvec3 camPos{ 0.0, 0.0, 0.0 };
   // Panini projection strengths, applied as a resampling distortion in the
   // post pass. 0 = standard rectilinear (off), 1 = max distortion.
   double paniniHorizontal{ 0.0 };
   double paniniVertical{ 0.0 };
   // Output zoom keeping every Panini source lookup inside the rendered
   // frustum. Owned by GraphicsEngineBase::getPaniniFitScale(); 1 = no zoom.
   double paniniFitScale{ 1.0 };
   // Blue-noise dither amplitude added in the post-processing pass before the
   // final 8-bit quantization, in color units. 0 = off; 1/255 covers one step.
   double ditherStrength{ 0.0 };
   // The shadow cascade this pass fills. A handler that grouped its work into
   // caster tiers draws tiers 0 through k for cascade k; one that did not draws
   // everything either way. All bits set clamps past the last tier, which is the
   // whole grouping: what every camera pass asks for by leaving this alone.
   unsigned int casterTier{ ~0u };
};
