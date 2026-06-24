// GridGeometry.h
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <limits>

/**
 * @brief Static utility class for grid coordinate transformations and geometry operations
 */
class GridGeometry {
public:
    /**
     * @brief Convert world coordinates to grid-local coordinates
     * @param worldPos World position to convert
     * @param gridPosition Position of the grid in world space
     * @param gridOrientation Orientation of the grid in world space
     * @param gridCenter Center point of the grid in grid-local space
     * @return Grid-local coordinates
     */
    static glm::dvec3 worldToGrid(
        const glm::dvec3& worldPos,
        const glm::dvec3& gridPosition,
        const glm::dquat& gridOrientation,
        const glm::dvec3& gridCenter);
    
    /**
     * @brief Convert grid-local coordinates to world coordinates
     * @param gridPos Grid-local position to convert
     * @param gridPosition Position of the grid in world space
     * @param gridOrientation Orientation of the grid in world space
     * @param gridCenter Center point of the grid in grid-local space
     * @return World coordinates
     */
    static glm::dvec3 gridToWorld(
        const glm::dvec3& gridPos,
        const glm::dvec3& gridPosition,
        const glm::dquat& gridOrientation,
        const glm::dvec3& gridCenter);
    
    /**
     * @brief Perform grid traversal algorithm between two points
     * @param startPos Starting position in grid-local coordinates
     * @param endPos Ending position in grid-local coordinates
     * @return Vector of grid coordinates traversed by the ray
     */
    static std::vector<glm::ivec3> gridTraversal(
        const glm::dvec3& startPos,
        const glm::dvec3& endPos);
};