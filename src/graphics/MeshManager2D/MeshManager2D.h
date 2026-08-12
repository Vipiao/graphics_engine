// src/graphics/MeshManager2D.h
#pragma once

#include "Geometry2D.h"
#include "../ShaderProgram.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "../AssimpLoader.h"
#include "../Texture2D.h"

#include <memory>

class TextureStore;
#include <glm/glm.hpp>

/**
 * @brief 2D mesh manager with instanced rendering
 * 
 * Usage example:
 * @code
 * // Create manager
 * MeshManager2D meshManager;
 *
 * // Load a mesh with texture
 * auto geometry = meshManager.loadMesh("assets/ship.obj", "assets/ship.png");
 *
 * // Create instances directly from geometry
 * auto instance1 = geometry.lock()->addInstance();
 * auto instance2 = geometry.lock()->addInstance();
 *
 * // Remove instances when needed
 * geometry.lock()->removeInstance(instance1);
 *
 * // Set properties, then push them to the GPU in one upload.
 * // Lock in case weak pointer does not exist anymore.
 * if (auto inst = instance2.lock()) {
 *     inst->m_position = glm::dvec2(100.0, 50.0);
 *     inst->m_scale = glm::dvec2(2.0, 2.0);
 *     inst->m_orientation = glm::radians(45.0);
 *     geometry.lock()->updateInstanceInBuffer(inst.get());
 * }
 *
 * // Render all geometries and instances
 * glm::mat4 projection = glm::ortho(0.0f, 800.0f, 0.0f, 600.0f);
 * meshManager.render(projection);
 * @endcode
 */

class MeshManager2D {
public:
    explicit MeshManager2D(TextureStore* textureStore);
    ~MeshManager2D();

    // Owns a shader program; copying would double-delete it.
    MeshManager2D(const MeshManager2D&) = delete;
    MeshManager2D& operator=(const MeshManager2D&) = delete;

    // Texture management
    int createTexture(const std::string& path);

    // Geometry management
    std::weak_ptr<Geometry2D> loadMesh(const std::string& geometryPath,
                                       const std::string& texturePath = "",
                                       int textureUnit = -1,
                                       bool enableTransparency = false);
    void releaseGeometry(std::weak_ptr<Geometry2D> geometry);

    // Rendering
    void render(const glm::mat4& projection);

    // Getters
    size_t getGeometryCount() const { return m_geometries.size(); }
    size_t getTotalInstanceCount() const;

private:
    // Which of the store's textures this renderer binds; the index is the unit
    // the shaders reach it through, so entries are appended and never moved.
    std::vector<std::weak_ptr<Texture2D>> m_textureUnits;
    TextureStore* m_textureStore{nullptr};

    // Core data
    std::vector<std::shared_ptr<Geometry2D>> m_geometries;

    ShaderProgram m_shaderProgram;

    void initializeShaders();
};