// InstancedGeometry.h
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glad/glad.h>

// Reusable instanced-geometry building block: a mesh uploaded once and drawn
// with many per-instance transforms/colors/textures, in camera-relative
// (Dekker) space. Owns no shaders and no render state; a renderer (e.g.
// InstanceHandler) supplies those and issues the draw calls.

/**
 * @brief Instance data for GPU buffer (local transforms only)
 */
struct InstanceData {
    glm::vec3 localPosition;     // Local position (no Dekker needed)
    float padding1;
    glm::vec4 localOrientation;  // Local orientation quaternion (x,y,z,w)
    glm::vec3 localScale;        // Local scale
    float padding2;
    glm::vec4 color;             // Instance color
    int32_t ssboIndex;           // Index into SSBO for geometry world transform
    int32_t colorTextureUnit;
    int32_t normalTextureUnit;
    int32_t materialTextureUnit;
    int32_t maskTextureUnit;
};

/**
 * @brief Vertex data structure
 */
struct GeometryVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec2 uv;
};

/**
 * @brief Data structure holding instance data
 */
class Instance {
public:
    uint64_t m_uniqueId;
    glm::dvec3 m_localPosition{0.0};
    glm::dquat m_localOrientation{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 m_localScale{1.0};
    glm::dvec4 m_color{1.0, 1.0, 1.0, 1.0};
    int m_ssboIndex{-1};        // SSBO index for world transform
    int m_colorTextureUnit{-1};
    int m_normalTextureUnit{-1};
    int m_materialTextureUnit{-1};
    int m_maskTextureUnit{-1};
    uint32_t m_bufferIndex; // Index in geometry's instance buffer

    Instance() : m_uniqueId(s_nextInstanceId++), m_bufferIndex(0) {}

private:
    static uint64_t s_nextInstanceId;
    friend class Geometry;
};

/**
 * @brief Data structure holding mesh geometry data and its instance buffer
 */
class Geometry {
public:
    uint64_t m_uniqueId;
    GLuint m_VAO, m_VBO, m_EBO;
    uint32_t m_vertexCount;
    uint32_t m_indexCount;
    bool m_hasIndices;

    // Rendering options
    double m_depthCompression = 1.0;  // 1.0 = normal, < 1.0 = compressed depth range
    bool m_enableAlphaBlending = false;

    // Instance management (kept in same order)
    GLuint m_instanceVBO;
    std::vector<std::shared_ptr<Instance>> m_instances;
    std::vector<InstanceData> m_instanceData;
    size_t m_instanceBufferCapacity{0};

    Geometry() : m_uniqueId(s_nextGeometryId++), m_VAO(0), m_VBO(0), m_EBO(0),
                 m_vertexCount(0), m_indexCount(0), m_hasIndices(false), m_instanceVBO(0) {}
    ~Geometry();

    // Load a mesh from file, combine its sub-meshes, and upload the vertex and
    // instance buffers with the standard attribute layout.
    static std::shared_ptr<Geometry> loadFromFile(const std::string& modelPath);

    // Instance management methods
    void setDepthCompression(double compression) { m_depthCompression = compression; }
    void setAlphaBlending(bool enable) { m_enableAlphaBlending = enable; }
    double getDepthCompression() const { return m_depthCompression; }
    bool getAlphaBlending() const { return m_enableAlphaBlending; }
    std::weak_ptr<Instance> addInstance(
        int ssboIndex, int colorTextureUnit = -1, int normalTextureUnit = -1,
        int materialTextureUnit = -1,
        const glm::dvec4& color = glm::dvec4(1.0, 1.0, 1.0, 1.0),
        int maskTextureUnit = -1);
    void removeInstance(std::weak_ptr<Instance> instance);
    void updateInstanceInBuffer(Instance* instance);

private:
    static uint64_t s_nextGeometryId;

    InstanceData createInstanceData(Instance* instance);
    void setupOpenGL(const std::vector<GeometryVertex>& vertices,
                     const std::vector<uint32_t>& indices);
};
