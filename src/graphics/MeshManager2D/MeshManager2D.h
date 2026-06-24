// src/graphics/MeshManager2D.h
#pragma once

#include "GeometryData.h"
#include "../ShaderProgram.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "../AssimpLoader.h"
#include "../TextureManagerBase.h"
#include <glm/glm.hpp>

/**
 * @brief 2D mesh manager with instanced rendering
 * 
 * Usage example:
 * @code
 * // Create manager
 * MeshManager2D meshManager(1000); // Max 1000 instances per geometry
 * 
 * // Load a mesh with texture
 * auto geometryData = meshManager.loadMesh("assets/ship.obj", "assets/ship.png");
 * 
 * // Create instances directly from geometry
 * auto instance1 = geometryData.lock()->createInstance();
 * auto instance2 = geometryData.lock()->createInstance();
 * 
 * // Remove instances when needed
 * geometryData.lock()->removeInstance(instance1);
 * 
 * // Set properties (automatically syncs to GPU)
 * // Lock in case weak pointer does not exist anymore.
 * if (auto inst = instance1.lock()) {
 *     inst->setPosition(glm::vec2(100.0f, 50.0f));
 *     inst->setScale(glm::vec2(2.0f, 2.0f));
 *     inst->setOrientation(glm::radians(45.0f));
 * }
 * 
 * // Render all geometries and instances
 * glm::mat4 projection = glm::ortho(0.0f, 800.0f, 0.0f, 600.0f);
 * meshManager.render(projection);
 * @endcode
 */

class MeshManager2D {
public:
    MeshManager2D(size_t maxInstancesPerGeometry = 1000);
    ~MeshManager2D();

    // Texture management
    int createTexture(const std::string& path);
    
    std::weak_ptr<GeometryData> loadMesh(const std::string& geometryPath,
                                          const std::string& texturePath = "",
                                          int textureUnit = -1,
                                          bool enableTransparency = false);
    
    // Rendering
    void render(const glm::mat4& projection);
    
    // Getters
    size_t getGeometryCount() const { return m_geometries.size(); }
    size_t getTotalInstanceCount() const;

private:
    // Texture management
    TextureManagerBase m_textureManager;

    std::vector<std::shared_ptr<GeometryData>> m_geometries;
    
    ShaderProgram m_shaderProgram;
    size_t m_maxInstancesPerGeometry;
    
    void initializeShaders();
    void bindTextures();
};