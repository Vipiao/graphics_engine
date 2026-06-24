// PositionSelector.h
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct SelectorResult {
    std::vector<glm::dvec2> projectedPositions;  // 2D screen positions (-1 to 1 range)
    int closestIndex;                            // Index of closest position to cursor (-1 if none)
    double distanceToClosest;                    // Euclidean distance to closest position
};

class PositionSelector {
public:
    /**
     * @brief Select from 3D positions projected to screen space
     * @param worldPositions 3D world positions to project
     * @param projectedRadius Radius for separation in screen space (applied to all points)
     * @param cameraPosition Camera world position
     * @param cameraOrientation Camera world orientation
     * @param fieldOfView Vertical field of view in radians
     * @param aspectRatio Screen width/height ratio
     * @param cursorPosition 2D cursor position in normalized coordinates (-1 to 1)
     * @param separationIterations Number of iterations for point separation (default 5)
     * @return SelectorResult with projected positions and closest selection
     */
    static SelectorResult selectFromPositions(
        const std::vector<glm::dvec3>& worldPositions,
        double projectedRadius,
        const glm::dvec3& cameraPosition,
        const glm::dquat& cameraOrientation, 
        double fieldOfView,
        double aspectRatio,
        const glm::dvec2& cursorPosition,
        int separationIterations = 5);

private:
    static void separateOverlappingPoints(std::vector<glm::dvec2>& positions, 
                                        double radius, int iterations);
    static int findClosestPoint(const std::vector<glm::dvec2>& positions, const glm::dvec2& cursor, 
                               double& outDistance);
};