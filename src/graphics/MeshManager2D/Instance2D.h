// src/graphics/MeshManager2D/Instance2D.h
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

/**
 * @brief Data structure holding the per-instance state of a 2D geometry
 *
 * Owned by the Geometry2D it was created from. Mutate the fields directly, then
 * hand the instance back to Geometry2D::updateInstanceInBuffer to upload them.
 */
class Instance2D {
public:
    uint64_t m_uniqueId;
    glm::dvec2 m_position{0.0, 0.0};
    glm::dvec2 m_scale{1.0, 1.0};
    double m_orientation{0.0}; // radians
    glm::dvec4 m_color{1.0, 1.0, 1.0, 1.0};
    uint32_t m_bufferIndex; // Index in the geometry's instance buffer

    Instance2D() : m_uniqueId(s_nextInstanceId++), m_bufferIndex(0) {}

private:
    inline static uint64_t s_nextInstanceId{1};
};
