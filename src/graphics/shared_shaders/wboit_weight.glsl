// wboit_weight.glsl
//
// Weighted Blended OIT weight (McGuire & Bavoil 2013): nearer / less
// transparent fragments contribute more, which approximates back-to-front
// ordering without sorting. Every pass that writes into the WBOIT accumulation
// targets must use this same weight, or the passes sort inconsistently against
// each other.
//
// The composite divides the accumulated color by the accumulated weight, so a
// fragment's weight is its share of the averaged transparent color. The share a
// sorted blend would give it is alpha times the transmittance of everything in
// front, which is why the near boost here is driven by the fragment's own
// opacity: a near opaque fragment hides the layers behind it, so it is boosted
// by 1/(1 - alpha), exactly the transmittance it removes, while a near but
// nearly clear fragment is barely boosted and cannot drown out denser layers
// further away. For two layers this reproduces the sorted result closely, and
// the weight stays within a range the fp16 accumulation buffer resolves.

// Depth span over which the near boost ramps in; logarithmic, so the ramp
// covers the decades between a cockpit window and distant geometry rather than
// saturating at one end. z is the positive view-space depth.
const float k_nearDist = 0.5;
const float k_farDist = 500.0;
// Caps the boost of an (almost) opaque near fragment.
const float k_minTransmittance = 0.01;

float wboitWeight(float alpha, float z) {
   float nearness = 1.0 - clamp(log2(z / k_nearDist) / log2(k_farDist / k_nearDist),
                                0.0, 1.0);
   float transmittance = max(1.0 - alpha, k_minTransmittance);
   return alpha * pow(transmittance, -nearness);
}
