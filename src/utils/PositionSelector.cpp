// PositionSelector.cpp
#include "PositionSelector.h"
#include <algorithm>
#include "../math/CameraProjection.h"
#include <cmath>
#include <limits>

SelectorResult PositionSelector::selectFromPositions(
    const std::vector<glm::dvec3>& worldPositions,
    double projectedRadius,
    const glm::dvec3& cameraPosition,
    const glm::dquat& cameraOrientation,
    double fieldOfView,
    double aspectRatio,
    const glm::dvec2& cursorPosition,
    int separationIterations) {
    
    SelectorResult result;
    
    if (worldPositions.empty()) {
        result.closestIndex = -1;
        result.distanceToClosest = std::numeric_limits<double>::infinity();
        return result;
    }
    
    // Project all 3D positions to screen space
    result.projectedPositions.reserve(worldPositions.size());
    for (const glm::dvec3& worldPos : worldPositions) {
        glm::dvec2 screenPos = CameraProjection::worldToScreen(worldPos, cameraPosition, cameraOrientation, 
                                                              fieldOfView, aspectRatio);
        result.projectedPositions.push_back(screenPos);
    }
    
    // Separate overlapping points iteratively
    separateOverlappingPoints(result.projectedPositions, projectedRadius, separationIterations);
    
    // Find closest point to cursor (excluding behind-camera points)
    result.closestIndex = findClosestPoint(result.projectedPositions, cursorPosition, 
                                         result.distanceToClosest);
    
    return result;
}

void PositionSelector::separateOverlappingPoints(std::vector<glm::dvec2>& positions,
                                                double radius, int iterations) {
    if (positions.size() <= 1) return;
    
    for (int iter = 0; iter < iterations; ++iter) {
        bool anyMoved = false;
        
        // Check all pairs of points (skip behind-camera points at -2,-2)
        for (size_t i = 0; i < positions.size(); ++i) {
            // Skip behind-camera points
            if (positions[i].x <= -1.9 && positions[i].y <= -1.9) continue;
            
            for (size_t j = i + 1; j < positions.size(); ++j) {
                // Skip behind-camera points
                if (positions[j].x <= -1.9 && positions[j].y <= -1.9) continue;
                
                glm::dvec2 delta = positions[j] - positions[i];
                double distance = glm::length(delta);
                double minDistance = radius + radius; // Both points have same radius
                
                if (distance < minDistance && distance > 0.0) {
                    // Points are overlapping, push them apart
                    glm::dvec2 direction = delta / distance;
                    double overlap = minDistance - distance;
                    glm::dvec2 separation = direction * (overlap * 0.5);
                    
                    positions[i] -= separation;
                    positions[j] += separation;
                    anyMoved = true;
                }
            }
        }
        
        // Early termination if no points moved
        if (!anyMoved) {
            break;
        }
    }
}

int PositionSelector::findClosestPoint(const std::vector<glm::dvec2>& positions, 
                                     const glm::dvec2& cursor, double& outDistance) {
    if (positions.empty()) {
        outDistance = std::numeric_limits<double>::infinity();
        return -1;
    }
    
    int closestIndex = -1;
    double closestDistance = std::numeric_limits<double>::infinity();
    
    for (size_t i = 0; i < positions.size(); ++i) {
        // Skip behind-camera points (marked as -2, -2)
        if (positions[i].x <= -1.9 && positions[i].y <= -1.9) continue;
        
        double distance = glm::length(positions[i] - cursor);
        if (distance < closestDistance) {
            closestDistance = distance;
            closestIndex = static_cast<int>(i);
        }
    }
    
    outDistance = closestDistance;
    return closestIndex;
}