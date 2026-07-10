// src/graphics/GeometryData.h
#pragma once

#include "GeometryInstance.h"
#include <vector>
#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>

struct Vertex2D {
    glm::vec2 position;
    glm::vec2 texCoord;
};

class GeometryData {
public:
    GeometryData(const std::vector<Vertex2D>& vertices,
                 const std::vector<unsigned int>& indices,
                 GLuint textureId = 0,
                 int textureUnit = -1,
                 bool enableTransparency = false,
                 size_t maxInstances = 1000);
    ~GeometryData();

    // Owns GL buffer/VAO handles; copying would double-delete them.
    GeometryData(const GeometryData&) = delete;
    GeometryData& operator=(const GeometryData&) = delete;
    
    // Instance management
    std::weak_ptr<GeometryInstance> createInstance();
    void removeInstance(GeometryInstance* instance);
    
    // Internal GPU sync methods
    void updateInstanceTransform(size_t index, const InstanceTransform& transform);
    
    // Rendering
    void render() const;
    
    // Getters
    size_t getInstanceCount() const { return m_instanceCount; }
    size_t getMaxInstances() const { return m_maxInstances; }
    GLuint getTextureId() const { return m_textureId; }
    int getTextureUnit() const { return m_textureUnit; }
    bool hasTransparency() const { return m_enableTransparency; }

private:
    // Mesh data
    std::vector<Vertex2D> m_vertices;
    std::vector<unsigned int> m_indices;
    GLuint m_textureId;
    int m_textureUnit;
    bool m_enableTransparency;
    
    // Instance management
    std::vector<std::shared_ptr<GeometryInstance>> m_instances;
    std::vector<InstanceTransform> m_instanceTransforms;
    size_t m_instanceCount;
    size_t m_maxInstances;
    
    // GPU resources
    GLuint m_VAO, m_VBO, m_EBO, m_instanceVBO;
    
    void initializeGPUBuffers();
    void swapInstance(size_t indexToRemove, size_t lastIndex);
};