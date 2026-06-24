// CameraProjection.cpp
#include "CameraProjection.h"
#include <cmath>

glm::dvec2 CameraProjection::worldToScreen(const glm::dvec3& worldPos, 
                                          const glm::dvec3& cameraPos,
                                          const glm::dquat& cameraOri, 
                                          double fov, 
                                          double aspectRatio) {
    // Transform to camera space
    glm::dvec3 cameraSpace = worldToCamera(worldPos, cameraPos, cameraOri);
    
    // Check if point is behind camera
    if (isBehindCamera(cameraSpace)) {
        // Point is behind camera, mark as non-selectable
        return glm::dvec2(-2.0, -2.0);
    }
    
    // Perspective projection
    double projectedX = cameraSpace.x / cameraSpace.y;
    double projectedY = cameraSpace.z / cameraSpace.y;
    
    // Convert to normalized screen coordinates using FOV
    double tanHalfFov = std::tan(fov * 0.5);
    projectedX /= tanHalfFov;
    projectedY /= tanHalfFov;  // Don't adjust for aspect ratio
    
    return glm::dvec2(projectedX, projectedY);
}

glm::dvec3 CameraProjection::worldToCamera(const glm::dvec3& worldPos,
                                          const glm::dvec3& cameraPos,
                                          const glm::dquat& cameraOri) {
    // Transform to camera space (camera looks down +Y axis)
    return glm::conjugate(cameraOri) * (worldPos - cameraPos);
}

bool CameraProjection::isBehindCamera(const glm::dvec3& cameraSpacePos) {
    // In camera space, +Y is forward, so anything with y <= 0 is behind the camera
    return cameraSpacePos.y <= 0.0;
}