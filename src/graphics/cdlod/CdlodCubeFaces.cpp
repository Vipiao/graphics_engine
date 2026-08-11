// CdlodCubeFaces.cpp
#include "CdlodCubeFaces.h"

const glm::dvec3 CdlodCubeFaces::k_faceOrigin[k_faceCount]{
    { 1.0, -1.0, -1.0},  // +X
    {-1.0, -1.0, -1.0},  // -X
    {-1.0,  1.0, -1.0},  // +Y
    {-1.0, -1.0, -1.0},  // -Y
    {-1.0, -1.0,  1.0},  // +Z
    {-1.0, -1.0, -1.0}   // -Z
};
const glm::dvec3 CdlodCubeFaces::k_faceUAxis[k_faceCount]{
    {0.0, 2.0, 0.0}, {0.0, 0.0, 2.0}, {0.0, 0.0, 2.0},
    {2.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}
};
const glm::dvec3 CdlodCubeFaces::k_faceVAxis[k_faceCount]{
    {0.0, 0.0, 2.0}, {0.0, 2.0, 0.0}, {2.0, 0.0, 0.0},
    {0.0, 0.0, 2.0}, {0.0, 2.0, 0.0}, {2.0, 0.0, 0.0}
};

std::vector<CdlodPatchFrame> CdlodCubeFaces::cubeRootFrames(double halfExtent) {
    std::vector<CdlodPatchFrame> frames{};
    frames.reserve(k_faceCount);

    // A frame's axes are half-edges measured from the patch centre, so a face
    // spanning its full basis has axes of half of it, and sits at the midpoint.
    for (int faceIndex{0}; faceIndex < k_faceCount; ++faceIndex) {
        const glm::dvec3 uAxis{k_faceUAxis[faceIndex] * 0.5 * halfExtent};
        const glm::dvec3 vAxis{k_faceVAxis[faceIndex] * 0.5 * halfExtent};
        frames.push_back(
            CdlodPatchFrame{k_faceOrigin[faceIndex] * halfExtent + uAxis + vAxis,
                            uAxis, vAxis});
    }
    return frames;
}
