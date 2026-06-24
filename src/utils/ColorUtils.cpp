// ColorUtils.cpp
#include "ColorUtils.h"
#include <cmath>
#include <algorithm>

glm::dvec3 ColorUtils::rgbToHsv(const glm::dvec3& rgb) {
    double maxVal = std::max({rgb.r, rgb.g, rgb.b});
    double minVal = std::min({rgb.r, rgb.g, rgb.b});
    double delta = maxVal - minVal;
    
    glm::dvec3 hsv;
    hsv.z = maxVal; // V (Value)
    
    if (maxVal > 0.0) {
        hsv.y = delta / maxVal; // S (Saturation)
    } else {
        hsv.y = 0.0;
    }
    
    if (delta == 0.0) {
        hsv.x = 0.0; // H (Hue) undefined for gray
    } else if (maxVal == rgb.r) {
        hsv.x = 60.0 * std::fmod((rgb.g - rgb.b) / delta, 6.0);
    } else if (maxVal == rgb.g) {
        hsv.x = 60.0 * (2.0 + (rgb.b - rgb.r) / delta);
    } else {
        hsv.x = 60.0 * (4.0 + (rgb.r - rgb.g) / delta);
    }
    
    hsv.x /= 360.0; // Normalize to [0,1]
    if (hsv.x < 0.0) hsv.x += 1.0; // Ensure positive
    return hsv;
}

glm::dvec3 ColorUtils::hsvToRgb(const glm::dvec3& hsv) {
    double h = hsv.x * 360.0;
    double s = hsv.y;
    double v = hsv.z;
    
    double c = v * s;
    double x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
    double m = v - c;
    
    glm::dvec3 rgb;
    if (h < 60.0) rgb = glm::dvec3(c, x, 0.0);
    else if (h < 120.0) rgb = glm::dvec3(x, c, 0.0);
    else if (h < 180.0) rgb = glm::dvec3(0.0, c, x);
    else if (h < 240.0) rgb = glm::dvec3(0.0, x, c);
    else if (h < 300.0) rgb = glm::dvec3(x, 0.0, c);
    else rgb = glm::dvec3(c, 0.0, x);
    
    return rgb + m;
}