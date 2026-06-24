// GridGeometry.cpp
#include "GridGeometry.h"
#include <stdexcept>
#include "../utils/PolyhedronProcessor.h"

glm::dvec3 GridGeometry::worldToGrid(
    const glm::dvec3& worldPos,
    const glm::dvec3& gridPosition,
    const glm::dquat& gridOrientation,
    const glm::dvec3& gridCenter) {
    
    // Transform: 
    // 1. Translate relative to grid position
    // 2. Rotate by conjugate of grid orientation
    // 3. Add center offset
    return glm::conjugate(gridOrientation) * (worldPos - gridPosition) + gridCenter;
}

glm::dvec3 GridGeometry::gridToWorld(
    const glm::dvec3& gridPos,
    const glm::dvec3& gridPosition,
    const glm::dquat& gridOrientation,
    const glm::dvec3& gridCenter) {
    
    // Transform:
    // 1. Subtract center
    // 2. Apply grid orientation
    // 3. Add grid position
    return gridPosition + gridOrientation * (gridPos - gridCenter);
}

std::vector<glm::ivec3> GridGeometry::gridTraversal(
    const glm::dvec3& startPos, 
    const glm::dvec3& endPos) {
    
    // In case the direction is such that the end cell might be missed.
    // For example {-0.5, 0.5, 0.5}, {0.0, 0.0, 0.5}
    glm::ivec3 newOrigin = glm::floor(endPos);
    glm::dvec3 startPosRel{ startPos - static_cast<glm::dvec3>(newOrigin) };
    glm::dvec3 endPosRel{ endPos - static_cast<glm::dvec3>(newOrigin) };
    constexpr double shift{ 1.e-6 };
    
    if (endPosRel.x == 0.) {
        endPosRel.x = shift;
    }
    if (endPosRel.y == 0.) {
        endPosRel.y = shift;
    }
    if (endPosRel.z == 0.) {
        endPosRel.z = shift;
    }
    
    std::vector<glm::ivec3> cells;
    glm::dvec3 dir{ endPosRel - startPosRel };
    glm::ivec3 step{ glm::sign(dir) };
    glm::dvec3 nextBoundary{ glm::floor(startPosRel) + glm::dvec3{ step } };
    glm::dvec3 tMax{};
    glm::dvec3 tDelta{};
    
    for (int i = 0; i < 3; ++i) {
        // Avoid division by zero for axis-aligned rays
        if (dir[i] > 0.) {
            tMax[i] = (nextBoundary[i] - startPosRel[i]) / dir[i];
            tDelta[i] = glm::abs(step[i] / dir[i]);
        } else if (dir[i] < 0.) {
            tMax[i] = (nextBoundary[i] - startPosRel[i] + 1) / dir[i];
            tDelta[i] = glm::abs(step[i] / dir[i]);
        } else {
            tMax[i] = std::numeric_limits<double>::infinity();
            tDelta[i] = std::numeric_limits<double>::infinity();
        }
    }
    
    glm::ivec3 cell = glm::floor(startPosRel);
    glm::ivec3 endCell = glm::floor(endPosRel);
    
#ifndef NDEBUG // Debug mode.
    uint64_t maxIt{ static_cast<uint64_t>(glm::length(dir) + 1.) * 3 };
    uint64_t iteration{ 0 };
#endif
    
    while (true) {
#ifndef NDEBUG // Debug mode.
        if (iteration++ > maxIt) {
            throw std::runtime_error("gridTraversal too many iterations.");
        }
#endif
        cells.push_back(cell + newOrigin);
        if (cell == endCell) {
            break;
        }
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                cell.x += step.x;
                tMax.x += tDelta.x;
            } else {
                cell.z += step.z;
                tMax.z += tDelta.z;
            }
        } else {
            if (tMax.y < tMax.z) {
                cell.y += step.y;
                tMax.y += tDelta.y;
            } else {
                cell.z += step.z;
                tMax.z += tDelta.z;
            }
        }
    }
    return cells;
}
