// dither.glsl
// Screen-door fading without blending: a pixel is kept while a fixed noise
// pattern's value there lies in [crossfade.x, crossfade.y). crossfade.z shifts the
// pattern per object, so overlapping objects keep different pixels. Two meshes
// sharing a bound keep complementary pixels and cross-fade without holes.

// pixel: gl_FragCoord.xy
bool ditherKeeps(vec2 pixel, vec3 crossfade) {
    // Interleaved gradient noise: no texture, and close to blue noise
    const float gradient =
        fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    const float noise = fract(gradient + crossfade.z);
    return noise >= crossfade.x && noise < crossfade.y;
}
