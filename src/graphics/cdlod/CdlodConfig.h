// CdlodConfig.h
#pragma once

#include <glm/glm.hpp>

// Creation parameters for one CDLOD body. Deliberately free of GL types so the
// engine's public header can carry it without pulling in the renderer.
struct CdlodConfig {
    // The body's radius, in metres. The subdivision is a cube quadtree, but the
    // vertex stage projects it onto the sphere of this radius, so the surface
    // touches the cube only at the six face centres.
    double m_halfExtent{100.0};
    // Surface albedo until a material is supplied.
    glm::dvec4 m_baseColor{0.55, 0.52, 0.48, 1.0};

    // A node splits once the camera comes within this many of its own edge
    // lengths. Raising it splits sooner: finer surface, more triangles. This is
    // the quality knob; expressing it relative to node size is what makes the
    // ranges halve in step with the nodes, which is what keeps neighbouring
    // patches within one level of each other.
    //
    // It is an angular quantity, and the only one: a node that declines to split
    // subtends about 1 / m_lodRangeFactor radians whatever its level, so its
    // quads subtend 1 / (m_lodRangeFactor * patchQuads), where patchQuads is the
    // grid size every body shares. Independent of the body's radius and of how
    // far away it is, which is what makes it the single place to set how fine the
    // surface looks. In pixels, at a horizontal field of view f over a viewport
    // w pixels wide:
    //
    //    quadPixels ~ w / (f * m_lodRangeFactor * patchQuads)
    //
    // It also sets where morphing begins: cdlod_patch.glsl derives that from this
    // rather than keeping a constant of its own, since the merge this decides has
    // to happen after the morph it schedules.
    //
    // Must stay above sqrt(2). A node that declines to split is no further than
    // r/2 + diagonal from anything touching it, so a neighbour two levels finer
    // needs this factor at or below sqrt(2) to exist at all. Below that bound
    // two-level seams become reachable and the cracks double in height.
    double m_lodRangeFactor{2.0};
};
