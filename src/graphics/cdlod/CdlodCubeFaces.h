// CdlodCubeFaces.h
#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "CdlodTree.h"

/**
 * @brief The cube a body is subdivided from, as quadtree roots.
 *
 * A face is origin + u * uAxis + v * vAxis for (u, v) in [0, 1]^2, in units
 * where the body spans [-1, 1] on every axis. Every basis is wound so
 * uAxis cross vAxis points out of the body, which makes a triangle wound
 * counter-clockwise in (u, v) counter-clockwise seen from outside -- what GL's
 * default front-face winding wants.
 *
 * This is the only place the cube exists. CdlodTree receives the six roots and
 * subdivides them without knowing what solid they came off, and the vertex stage
 * receives each patch's frame with the node record, so neither carries a copy of
 * the table below.
 */
class CdlodCubeFaces {
public:
    static constexpr int k_faceCount{6};

    // The six faces as patch frames, scaled so the cube spans halfExtent on
    // every axis: the roots of a body of that radius.
    static std::vector<CdlodPatchFrame> cubeRootFrames(double halfExtent);

private:
    static const glm::dvec3 k_faceOrigin[k_faceCount];
    static const glm::dvec3 k_faceUAxis[k_faceCount];
    static const glm::dvec3 k_faceVAxis[k_faceCount];
};
