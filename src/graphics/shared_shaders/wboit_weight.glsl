// wboit_weight.glsl
//
// Weighted Blended OIT weight (McGuire & Bavoil 2013): nearer / less
// transparent fragments contribute more, which approximates back-to-front
// ordering without sorting. Every pass that writes into the WBOIT accumulation
// targets must use this same weight, or the passes sort inconsistently against
// each other.

// z is the positive view-space depth. The scale constants (5, 200) are tuned
// for meters-scale scenes; retune if transparent surfaces span very different
// depth ranges.
float wboitWeight(float alpha, float z) {
   return alpha * clamp(
      10.0 / (1e-5 + pow(z / 5.0, 2.0) + pow(z / 200.0, 6.0)),
      1e-2, 3e3);
}
