// cdlod_default_surface.glsl
//
// Default CDLOD surface body: the bare sphere. The base shape is already a
// sphere, so the height field is flat everywhere. It is what a body gets when
// no surface snippet is supplied, and the smallest complete example of the
// contract in cdlod_patch.glsl.

float cdlodSurfaceElevation(vec3 spherePosition) {
   return 0.0;
}

vec3 cdlodSurfaceGradient(vec3 spherePosition) {
   return vec3(0.0);
}
