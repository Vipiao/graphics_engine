// IntegerVectorMath.h
#pragma once

#include <glm/glm.hpp>

/**
 * @brief Static utility class for integer vector operations that GLM doesn't allow
 */
class IntegerVectorMath {
public:
    /**
     * @brief Cross product of two integer vectors
     * @param a First vector
     * @param b Second vector
     * @return Cross product as integer vector
     */
    static glm::ivec3 cross(const glm::ivec3& a, const glm::ivec3& b) {
        return glm::ivec3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }
    
    /**
     * @brief Dot product of two integer vectors
     * @param a First vector
     * @param b Second vector
     * @return Dot product as integer
     */
    static int dot(const glm::ivec3& a, const glm::ivec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    
    /**
     * @brief Squared length of integer vector
     * @param v Vector
     * @return Squared length as integer
     */
    static int length2(const glm::ivec3& v) {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }
};