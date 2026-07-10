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
    
    // Geometry management. Transparent geometries are drawn in the Weighted
    // Blended OIT pass instead of the opaque G-buffer pass.
    std::weak_ptr<Geometry> createGeometry(const std::string& modelPath,
                                           bool transparent = false);
    void releaseGeometry(std::weak_ptr<Geometry> geometry);
    
    // Rendering
    void render(
        const FrameRenderParams& params,
        bool renderOpaque = true, bool renderTransparent = true);

    // Geometry-only rendering for deferred pipeline
    void renderGeometry(
        const FrameRenderParams& params,
        bool renderOpaque = true, bool renderTransparent = true);
    void renderDepth(
        const FrameRenderParams& params,
        bool renderOpaque = true, bool renderTransparent = true);

    // Weighted Blended OIT pass for transparent geometry. The blend and depth
    // state is owned by DeferredRenderer::beginOITPass; this only binds the OIT
    // shader and draws the transparent geometries.
    void renderOIT(const FrameRenderParams& params);

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
    ShaderProgram m_oitShaderProgram;
    
    // Internal helpers
    void createShaderProgram();

    // Helper function for common rendering logic. When oitBlend is set, the
    // caller owns the blend and depth-write state (OIT pass), so the per-geometry
    // alpha-blend setup is skipped and depth writes stay disabled.
    void renderGeometryHelper(
        const FrameRenderParams& params,
        bool renderOpaque, bool renderTransparent, bool oitBlend = false);
};