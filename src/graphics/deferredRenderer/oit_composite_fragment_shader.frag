#version 460 core

// Composite the Weighted Blended OIT accumulation targets over the lit scene.
// Runs as a fullscreen pass with fixed-function blend
// (GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA), which expands the output below to:
//   scene = avgColor * (1 - revealage) + scene * revealage
out vec4 FragColor;

uniform sampler2D u_accum;      // RGB: Σ(color·alpha·weight), A: Σ(alpha·weight)
uniform sampler2D u_revealage;  // R: Π(1 - alpha) = fraction of background surviving

void main() {
   ivec2 coord = ivec2(gl_FragCoord.xy);
   vec4 accum = texelFetch(u_accum, coord, 0);
   float revealage = texelFetch(u_revealage, coord, 0).r;

   // Undo the weighting to recover the average transparent color. Guard the
   // denominator against zero (and against inf/nan from fp16 overflow).
   float denom = accum.a;
   if (isinf(denom) || isnan(denom)) {
      denom = max(max(accum.r, accum.g), accum.b);
   }
   vec3 avgColor = accum.rgb / max(denom, 1e-5);

   FragColor = vec4(avgColor, revealage);
}
