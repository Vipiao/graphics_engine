// src/graphics/MeshManager2D/Geometry2D.h
#pragma once

#include "Instance2D.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>

// 2D counterpart of Geometry: a screen-space mesh uploaded once and drawn with
// many per-instance transforms and colors. Owns no shaders and no render state;
// MeshManager2D supplies those and issues the draw call.

/**
 * @brief Vertex data structure
 */
struct Vertex2D {
    glm::vec2 position;
    glm::vec2 texCoord;
};

/**
 * @brief Instance data for the GPU buffer, mirrored by the instanced attributes
 */
struct InstanceData2D {
    glm::vec2 position;
    glm::vec2 scale;
    glm::vec4 color;   // RGBA color for GPU
    float orientation; // radians
    float padding[3];  // Align to 48 bytes total (next 16-byte boundary)
    // Total size: 48 bytes (8+8+16+4+12) - multiple of largest alignment (16)
};

/**
 * @brief Data structure holding 2D mesh data and its instance buffer
 */
class Geometry2D {
public:
    uint64_t m_uniqueId;

    Geometry2D(const std::vector<Vertex2D>& vertices,
               const std::vector<unsigned int>& indices,
               GLuint textureId = 0,
               int textureUnit = -1,
               bool enableTransparency = false);
    ~Geometry2D();

    // Owns GL buffer/VAO handles; copying would double-delete them.
    Geometry2D(const Geometry2D&) = delete;
    Geometry2D& operator=(const Geometry2D&) = delete;

    // Instance management methods
    std::weak_ptr<Instance2D> addInstance();
    void removeInstance(std::weak_ptr<Instance2D> instance);
    void updateInstanceInBuffer(Instance2D* instance);

    // Rendering
    void render() const;

    // Getters
    size_t getInstanceCount() const { return m_instances.size(); }
    GLuint getTextureId() const { return m_textureId; }
    int getTextureUnit() const { return m_textureUnit; }
    bool hasTransparency() const { return m_enableTransparency; }

private:
    inline static uint64_t s_nextGeometryId{1};

    // Mesh data
    std::vector<Vertex2D> m_vertices;
    std::vector<unsigned int> m_indices;
    GLuint m_textureId;
    int m_textureUnit;
    bool m_enableTransparency;

    // Instance management (kept in same order)
    GLuint m_instanceVBO;
    std::vector<std::shared_ptr<Instance2D>> m_instances;
    std::vector<InstanceData2D> m_instanceData;
    size_t m_instanceBufferCapacity{0};

    // GPU resources
    GLuint m_VAO, m_VBO, m_EBO;

    void setupOpenGL();
    // True when the instance is owned by this geometry and its index is live.
    bool ownsInstance(const Instance2D* instance) const;
    InstanceData2D createInstanceData(const Instance2D* instance) const;
    void uploadInstanceData(size_t index) const;
};
