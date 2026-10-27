// ImpostorConfig.h
#pragma once

// How an impostor is baked: a mesh pictured from viewsPerSide^2 directions over
// the half-sphere above it (+Z up), one atlas cell each (impostor_views.glsl).
struct ImpostorBakeSpec {
    // Even, so no cell centre lies straight above, where the view axes are
    // undefined
    int m_viewsPerSide{8};
    // Cell side, border included. Divisible by a high power of two keeps more
    // mip levels clean of neighbouring views.
    int m_viewPixels{128};
    // Empty margin inside each cell
    int m_borderPixels{8};
};
