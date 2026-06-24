// src/graphics/GeometryInstance.cpp
#include "GeometryInstance.h"
#include "GeometryData.h"

GeometryInstance::GeometryInstance(GeometryData* parent, size_t index)
    : m_color(1.0, 1.0, 1.0, 1.0), m_index(index), m_parent(parent) {
    // Initialize with default transform
    m_transform.position = glm::vec2(0.0f);
    m_transform.scale = glm::vec2(1.0f);
    m_transform.color = glm::vec4(static_cast<float>(m_color.r), static_cast<float>(m_color.g), 
                                  static_cast<float>(m_color.b), static_cast<float>(m_color.a));
    m_transform.orientation = 0.0f;
    m_transform.padding[0] = 0.0f;
    m_transform.padding[1] = 0.0f;
    m_transform.padding[2] = 0.0f;
}

void GeometryInstance::setPosition(const glm::vec2& pos) {
    m_transform.position = pos;
    updateParent();
}

void GeometryInstance::setScale(const glm::vec2& scale) {
    m_transform.scale = scale;
    updateParent();
}

void GeometryInstance::setOrientation(float radians) {
    m_transform.orientation = radians;
    updateParent();
}

void GeometryInstance::setColor(const glm::dvec4& color) {
    m_color = color;
    m_transform.color = glm::vec4(static_cast<float>(m_color.r), static_cast<float>(m_color.g), 
                                  static_cast<float>(m_color.b), static_cast<float>(m_color.a));
    updateParent();
}

void GeometryInstance::syncToGPU() {
    updateParent();
}

void GeometryInstance::updateParent() {
    if (m_parent) {
        m_parent->updateInstanceTransform(m_index, m_transform);
    }
}