#version 460 core

out vec4 FragColor;

// Finished scene color (lighting + transparents), rendered rectilinear.
uniform sampler2D u_sceneColor;
uniform mat4 u_projection;
// Panini projection strengths. 0 = standard rectilinear (off), 1 = max distortion.
uniform float u_paniniHorizontal;
uniform float u_paniniVertical;
// Zooms the output so the distorted image fills the screen without sampling
// outside the rendered frustum. Computed on the CPU from the strengths.
uniform float u_paniniFitScale;

in vec2 texCoord;

// Inverse of one cylindrical Panini pass acting on the x axis of tan-space
// coordinates (view-space xy at z = -1). Given a distorted coordinate, returns
// the rectilinear one. d is the Panini distance: 0 = rectilinear (identity),
// 1 = max distortion. Forward mapping for reference, with phi = atan(t.x):
//   t' = t * (d + 1) * cos(phi) / (d + cos(phi))
vec2 inversePaniniX(vec2 t, float d) {
   float x = t.x;
   float r = sqrt((d + 1.0) * (d + 1.0) + x * x);
   float phi = asin(clamp(x * d / r, -1.0, 1.0)) + atan(x, d + 1.0);
   float cosPhi = cos(phi);
   // The forward mapping scales both axes by this factor; undo it on y.
   float scale = (d + 1.0) * cosPhi / (d + cosPhi);
   return vec2(tan(phi), t.y / scale);
}

// Screen coordinate of the rectilinear scene pixel that this (Panini
// distorted) output pixel shows. The fit scale keeps the result inside [0, 1]
// by construction; the bounds check at the call site is only a safety net
// against float rounding at the borders.
vec2 paniniSourceCoord(vec2 screenCoord) {
   if (u_paniniHorizontal <= 0.0 && u_paniniVertical <= 0.0) {
      return screenCoord;
   }
   // Screen coordinate to tan space.
   vec2 ndc = screenCoord * 2.0 - 1.0;
   vec2 projScale = vec2(u_projection[0][0], u_projection[1][1]);
   vec2 t = ndc / projScale;
   t *= u_paniniFitScale;
   // The forward mapping is the horizontal Panini pass followed by the vertical
   // one (same mapping with axes swapped), so invert in reverse order.
   t = inversePaniniX(t.yx, u_paniniVertical).yx;
   t = inversePaniniX(t, u_paniniHorizontal);
   return (t * projScale) * 0.5 + 0.5;
}

void main() {
   vec2 sourceCoord = paniniSourceCoord(texCoord);
   if (sourceCoord.x < 0.0 || sourceCoord.x > 1.0 ||
       sourceCoord.y < 0.0 || sourceCoord.y > 1.0) {
      discard; // Float-rounding safety net; the fit scale prevents this by construction.
   }
   FragColor = vec4(texture(u_sceneColor, sourceCoord).rgb, 1.0);
}
