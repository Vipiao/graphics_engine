// src/graphics/GeometryData.cpp
#include "GeometryData.h"
#include <iostream>
#include <algorithm>

GeometryData::GeometryData(const std::vector<Vertex2D>& vertices,
                          const std::vector<unsigned int>& indices,
                          GLuint textureId,
                          int textureUnit,
                          bool enableTransparency,
                          size_t maxInstances)
    : m_vertices(vertices), m_indices(indices), m_textureId(textureId),
      m_textureUnit(textureUnit), m_enableTransparency(enableTransparency), 
      m_instanceCount(0), m_maxInstances(maxInstances) {
    
    // Reserve space for instance data
    m_instances.reserve(maxInstances);
    m_instanceTransforms.reserve(maxInstances);
    
    initializeGPUBuffers();
}

GeometryData::~GeometryData() {
    // Cleanup GPU resources
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
    glDeleteBuffers(1, &m_EBO);
    glDeleteBuffers(1, &m_instanceVBO);
}

void GeometryData::initializeGPUBuffers() {
    // Create VAO
    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);
    
    // Interleaved vertex buffer (position + texcoord)
    glGenBuffers(1, &m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex2D),
                 m_vertices.data(), GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)offsetof(Vertex2D, position));
    glEnableVertexAttribArray(0);
    
    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)offsetof(Vertex2D, texCoord));
    glEnableVertexAttribArray(1);
    
    // Element buffer
    glGenBuffers(1, &m_EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int),
                 m_indices.data(), GL_STATIC_DRAW);
    
    // Instance buffer
    glGenBuffers(1, &m_instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, m_maxInstances * sizeof(InstanceTransform),
                 nullptr, GL_DYNAMIC_DRAW);
    
    // Instance attributes (position, scale, orientation)
    // Position (vec2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceTransform), 
                         (void*)offsetof(InstanceTransform, position));
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);
    
    // Scale (vec2)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceTransform),
                         (void*)offsetof(InstanceTransform, scale));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    // Color (vec4)
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceTransform),
                         (void*)offsetof(InstanceTransform, color));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);
    
    // Orientation (float)
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceTransform),
                         (void*)offsetof(InstanceTransform, orientation));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);
    
    glBindVertexArray(0);
}

std::weak_ptr<GeometryInstance> GeometryData::createInstance() {
    if (m_instanceCount >= m_maxInstances) {
        std::cerr << "GeometryData: Maximum instances reached (" << m_maxInstances << ")" << std::endl;
        return std::weak_ptr<GeometryInstance>();
    }
    
    // Create new instance
    auto instance = std::make_shared<GeometryInstance>(this, m_instanceCount);
    GeometryInstance* instancePtr = instance.get();
    
    // Add to containers
    m_instances.push_back(std::move(instance));
    m_instanceTransforms.push_back(instancePtr->getTransform());
    
    m_instanceCount++;
    
    // Update GPU buffer for just the new instance
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, (m_instanceCount - 1) * sizeof(InstanceTransform),
                   sizeof(InstanceTransform), &m_instanceTransforms[m_instanceCount - 1]);
    
    return std::weak_ptr<GeometryInstance>(m_instances.back());
}

void GeometryData::removeInstance(GeometryInstance* instance) {
    if (!instance || instance->getParent() != this) {
        return;
    }
    
    size_t indexToRemove = instance->getIndex();
    if (indexToRemove >= m_instanceCount) {
        return;
    }
    
    // Swap with last instance if not already the last
    if (indexToRemove < m_instanceCount - 1) {
        swapInstance(indexToRemove, m_instanceCount - 1);
    }
    
    // Remove last instance
    m_instances.pop_back();
    m_instanceTransforms.pop_back();
    m_instanceCount--;
    
    // Update GPU buffer - only need to update the swapped instance if there was a swap
    if (indexToRemove < m_instanceCount) {
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        glBufferSubData(GL_ARRAY_BUFFER, indexToRemove * sizeof(InstanceTransform),
                       sizeof(InstanceTransform), &m_instanceTransforms[indexToRemove]);
    }
}

void GeometryData::swapInstance(size_t indexToRemove, size_t lastIndex) {
    // Swap shared_ptrs
    std::swap(m_instances[indexToRemove], m_instances[lastIndex]);
    std::swap(m_instanceTransforms[indexToRemove], m_instanceTransforms[lastIndex]);
    
    // Update the swapped instance's index
    m_instances[indexToRemove]->setIndex(indexToRemove);
}

void GeometryData::updateInstanceTransform(size_t index, const InstanceTransform& transform) {
    if (index < m_instanceCount) {
        m_instanceTransforms[index] = transform;
        
        // Update GPU buffer for single instance
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 
                       index * sizeof(InstanceTransform),
                       sizeof(InstanceTransform),
                       &transform);
    }
}

void GeometryData::render() const {
    if (m_instanceCount == 0) {
        return;
    }

    // Handle transparency
    if (m_enableTransparency) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    
    glBindVertexArray(m_VAO);
    
    // Bind texture if available
    if (m_textureId != 0) {
        glActiveTexture(GL_TEXTURE0 + m_textureUnit);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
    }
    
    // Instanced draw call
    glDrawElementsInstanced(GL_TRIANGLES, 
                           static_cast<GLsizei>(m_indices.size()),
                           GL_UNSIGNED_INT, 0, 
                           static_cast<GLsizei>(m_instanceCount));
    
    glBindVertexArray(0);
}