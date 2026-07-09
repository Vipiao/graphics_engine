// RayVolumeHandler.h
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glad/glad.h>
#include "../ShaderProgram.h"
#include "../FrameRenderParams.h"
#include "../instancedGeometry/InstancedGeometry.h"

// Forward declaration
class SSBOManager;

/**
 * @brief Renderer for proxy-geometry volumetric effects.
 *
 * A mesh is uploaded as a proxy volume and drawn back-faces-only with hardware
 * depth testing disabled, so the effect stays visible with the camera inside it
 * and soft-clamps against the opaque scene instead of hard-clipping. The
 * fragment shading is supplied by the game as an injected GLSL body, making the
 * feature effect-agnostic. Instances carry an animated value vector (state +
 * velocity) that the shader forward-integrates like the physics interpolation.
 *
 * Draws into the same Weighted Blended OIT targets as the ordinary transparents;
 * the pass state is owned by DeferredRenderer::beginRayVolumeSubPass.
 */
class RayVolumeHandler {
public:
    explicit RayVolumeHandler(SSBOManager* ssboManager);
    ~RayVolumeHandler();

    // Build a shading program from the scaffold with the given body snippet
    // injected. Empty bodySnippetPath uses the built-in default body. Returns a
    // material index.
    size_t createMaterial(const std::string& bodySnippetPath = "");

    // Proxy mesh drawn for the given material.
    std::weak_ptr<Geometry> createGeometry(const std::string& modelPath,
                                           size_t materialIndex);
    void releaseGeometry(std::weak_ptr<Geometry> geometry);

    // Per-instance creation carrying the animated value vector (state is the
    // value at the current time, velocity its per-time-step derivative). The
    // caller sets the local transform afterward on the returned Instance and
    // calls Geometry::updateInstanceInBuffer, exactly like the instance handler.
    std::weak_ptr<Instance> addInstance(
        std::weak_ptr<Geometry> geometry, int ssboIndex,
        const glm::dvec4& color = glm::dvec4(1.0),
        const glm::dvec4& state = glm::dvec4(0.0),
        const glm::dvec4& velocity = glm::dvec4(0.0));
    void setInstanceValues(std::weak_ptr<Geometry> geometry,
                           std::weak_ptr<Instance> instance,
                           const glm::dvec4& state, const glm::dvec4& velocity);
    void removeInstance(std::weak_ptr<Geometry> geometry,
                        std::weak_ptr<Instance> instance);

    // Draw all volumes. The framebuffer, blend, cull and depth state are owned
    // by DeferredRenderer::beginRayVolumeSubPass; this binds programs, uniforms
    // (including the scene depth sampled per pixel) and issues the draws.
    void render(const FrameRenderParams& params,
                unsigned int sceneDepthTexture,
                unsigned int opaqueColorTexture,
                unsigned int screenWidth, unsigned int screenHeight);

    std::pair<bool, std::string> reloadShaders();

private:
    // Per-instance animated values, uploaded to the auxiliary instance buffer at
    // attribute locations 13 (state) and 14 (velocity). vec4 members are naturally
    // 16-byte aligned, so the 32-byte stride needs no padding.
    struct RayVolumeAux {
        glm::vec4 state{0.0f};
        glm::vec4 velocity{0.0f};
    };

    struct Material {
        ShaderProgram program;
        std::string bodySnippetPath;   // empty => built-in default body
    };

    // A proxy mesh reusing the shared instanced-geometry buffers for transform,
    // color and SSBO index, plus a parallel auxiliary buffer for the animated
    // values kept in lockstep with the base instance buffer.
    struct VolumeGeometry {
        std::shared_ptr<Geometry> geometry;
        GLuint auxVBO{0};
        std::vector<RayVolumeAux> aux;
        size_t auxCapacity{0};
        size_t materialIndex{0};
    };

    SSBOManager* m_ssboManager;
    std::vector<std::unique_ptr<Material>> m_materials;
    std::vector<VolumeGeometry> m_volumes;

    void buildMaterialProgram(Material& material);
    void setupAuxBuffer(VolumeGeometry& volume);
    void uploadAux(VolumeGeometry& volume, size_t index);
    VolumeGeometry* findVolume(const std::shared_ptr<Geometry>& geometry);
    static std::string buildFragmentSource(const std::string& bodySnippetPath);
};
