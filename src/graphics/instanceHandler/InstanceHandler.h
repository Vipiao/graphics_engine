// InstanceHandler.h
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glad/glad.h>
#include "../TextureManagerBase.h"
#include "../ShaderProgram.h"

// Forward declarations
class SSBOManager;
class Geometry;
class Instance;

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
    friend class InstanceHandler;
};

/**
 * @brief Data structure holding mesh geometry data
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
    
    friend class InstanceHandler;
};

/**
 * @brief Instance handler for managing geometries, instances, and instanced rendering
 */
class InstanceHandler {
public:
    explicit InstanceHandler(SSBOManager* ssboManager);
    ~InstanceHandler();

    // Texture management
    int createTexture(const std::string& texturePath);
    
    // Geometry management  
    std::weak_ptr<Geometry> createGeometry(const std::string& modelPath);
    void releaseGeometry(std::weak_ptr<Geometry> geometry);
    
    // Rendering
    void render(
        const glm::mat4& view, const glm::mat4& projection,
        uint64_t frame, uint64_t time, double timeRemainder,
        const glm::dvec3& lightDir, const glm::dvec3& camPos,
        bool renderOpaque = true, bool renderTransparent = true
    );

    // Geometry-only rendering for deferred pipeline
    void renderGeometry(
        const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
        double timeRemainder, const glm::dvec3& lightPos, const glm::dvec3& camPos,
        bool renderOpaque = true, bool renderTransparent = true);
    void renderDepth(
        const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
        double timeRemainder, const glm::dvec3& camPos,
        bool renderOpaque = true, bool renderTransparent = true);

    // Shader reloading
    std::pair<bool, std::string> reloadShaders();

private:
    // Texture management
    TextureManagerBase m_textureManager;

    // Core data
    std::vector<std::shared_ptr<Geometry>> m_geometries;
    
    // OpenGL resources
    SSBOManager* m_ssboManager;
    ShaderProgram m_shaderProgram;
    ShaderProgram m_gbufferShaderProgram;
    ShaderProgram m_depthShaderProgram;
    
    // Internal helpers
    void loadGeometryFromFile(Geometry* geometry, const std::string& modelPath);
    void setupGeometryOpenGL(Geometry* geometry, const std::vector<GeometryVertex>& vertices, 
                           const std::vector<uint32_t>& indices);
    void createShaderProgram();

    // Helper function for common rendering logic
    void renderGeometryHelper(
        const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
        double timeRemainder, const glm::dvec3& camPos,
        bool renderOpaque, bool renderTransparent);
};