// ColorUtils.h
#pragma once

#include <glm/glm.hpp>

/**
 * @brief Utility class for color space conversions
 */
class ColorUtils {
public:
    /**
     * @brief Convert RGB to HSV color space
     * @param rgb RGB color (each component 0-1)
     * @return HSV color (H: 0-1, S: 0-1, V: 0-1)
     */
    static glm::dvec3 rgbToHsv(const glm::dvec3& rgb);
    
    /**
     * @brief Convert HSV to RGB color space
     * @param hsv HSV color (H: 0-1, S: 0-1, V: 0-1)
     * @return RGB color (each component 0-1)
     */
    static glm::dvec3 hsvToRgb(const glm::dvec3& hsv);
};