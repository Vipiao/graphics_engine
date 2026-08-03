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
#include "../FrameRenderParams.h"
#include "../instancedGeometry/InstancedGeometry.h"

// Forward declarations
class SSBOManager;

/**
 * @brief Instance handler for managing geometries, instances, and instanced rendering
 */
class InstanceHandler {
public:
    explicit InstanceHandler(SSBOManager* ssboManager);
    ~InstanceHandler();

    // Owns shader programs and GL handles; copying would double-delete them.
    InstanceHandler(const InstanceHandler&) = delete;
    InstanceHandler& operator=(const InstanceHandler&) = delete;

    // Texture management
    int createTexture(const std::string& texturePath);
    
    // Geometry management. The render layer selects which of the passes below
    // draws the geometry.
    std::weak_ptr<Geometry> createGeometry(const std::string& modelPath,
                                           RenderLayer layer = RenderLayer::Opaque);
    std::weak_ptr<Geometry> createGeometry(const std::vector<GeometryVertex>& vertices,
                                           RenderLayer layer = RenderLayer::Opaque);
    void releaseGeometry(std::weak_ptr<Geometry> geometry);

    // Each pass draws exactly one render layer, using the blend and depth
    // state configured by the pass owner that bound the framebuffer
    // (DeferredRenderer / ShadowRenderer).

    // G-buffer and shadow-depth passes for opaque geometry.
    void renderGeometry(const FrameRenderParams& params);
    void renderDepth(const FrameRenderParams& params);

    // Weighted Blended OIT pass for transparent geometry.
    void renderOIT(const FrameRenderParams& params);

    // Forward pass for overlay geometry, drawn after the OIT composite.
    void renderOverlay(const FrameRenderParams& params);

    // Shader reloading
    std::pair<bool, std::string> reloadShaders();

private:
    // Texture management
    TextureManagerBase m_textureManager;

    // Core data
    std::vector<std::shared_ptr<Geometry>> m_geometries;
    
    // OpenGL resources
    SSBOManager* m_ssboManager;
    ShaderProgram m_overlayShaderProgram;
    ShaderProgram m_gbufferShaderProgram;
    ShaderProgram m_depthShaderProgram;
    ShaderProgram m_oitShaderProgram;
    
    // Internal helpers
    void createShaderProgram();

    // Common rendering logic: per-frame uniforms, texture binds, and the
    // instanced draws for every geometry on the given layer, drawn with the
    // caller's current render state.
    void renderGeometryHelper(const FrameRenderParams& params, RenderLayer layer);
};