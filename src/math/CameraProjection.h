// CameraProjection.h
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

/**
 * @brief Static utility class for camera projection operations
 * 
 * Provides functions for converting between world space, camera space, and screen space coordinates.
 * Assumes camera coordinate system where +Y is forward, +X is right, +Z is up.
 */
class CameraProjection {
public:
    /**
     * @brief Project a 3D world position to normalized screen coordinates
     * @param worldPos World position to project
     * @param cameraPos Camera world position
     * @param cameraOri Camera world orientation
     * @param fov Vertical field of view in radians
     * @param aspectRatio Screen width/height ratio
     * @return 2D screen coordinates in range [-1, 1]. Returns (-2, -2) if point is behind camera
     */
    static glm::dvec2 worldToScreen(const glm::dvec3& worldPos, 
                                    const glm::dvec3& cameraPos,
                                    const glm::dquat& cameraOri, 
                                    double fov, 
                                    double aspectRatio);
    
    /**
     * @brief Transform world position to camera space
     * @param worldPos World position to transform
     * @param cameraPos Camera world position
     * @param cameraOri Camera world orientation
     * @return Position in camera space (+Y forward, +X right, +Z up)
     */
    static glm::dvec3 worldToCamera(const glm::dvec3& worldPos,
                                    const glm::dvec3& cameraPos,
                                    const glm::dquat& cameraOri);
    
    /**
     * @brief Check if a camera space position is behind the camera
     * @param cameraSpacePos Position in camera space
     * @return True if the position is behind the camera (y <= 0)
     */
    static bool isBehindCamera(const glm::dvec3& cameraSpacePos);
};