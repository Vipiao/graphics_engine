#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <utility>
#include <glad/glad.h>

class ShadowRenderer {
public:
    ShadowRenderer();
    ~ShadowRenderer();
    
    // Shadow map management
    // casterReach is how far toward the light a caster may stand from what it
    // shadows; see m_casterReach. It scales with the scene, so the caller sets it
    // alongside the cascade sizes.
    void setupShadowMaps(unsigned int width, unsigned int height,
                         unsigned int numCascades = 3,
                         const std::vector<double>& orthoSizes = {50.0, 200.0, 800.0},
                         double casterReach = 1000.0);
    void resizeShadowMaps(unsigned int width, unsigned int height);
    
    // Fraction of its own radius each cascade's centre is pushed along the view
    // direction. Coverage behind the camera is never seen, so it is spent on reach
    // in front; below 1 so the camera stays inside every cascade.
    static constexpr double k_cascadePush{0.9};

    // Shadow pass rendering. camForward is the unit view direction in world axes.
    void beginShadowPass(const glm::dvec3& lightDir, const glm::dvec3& camForward,
                         uint64_t frameNum);
    void endShadowPass();
    void bindCascadeLayer(unsigned int cascadeIndex);
    
    // Getters for shadow map data
    unsigned int getShadowMapTextureArray() const { return m_shadowDepthTextureArray; }
    unsigned int getShadowMapWidth() const { return m_shadowMapWidth; }
    unsigned int getShadowMapHeight() const { return m_shadowMapHeight; }

    unsigned int getNumCascades() const { return m_numCascades; }
    const std::vector<glm::dmat4>& getLightSpaceMatrices() const { return m_lightSpaceMatrices; }  // For rendering (L-space)
    std::vector<glm::dmat4> getLightSpaceMatricesForViewSpace(const glm::dmat4& viewMatrix) const;  // For lighting (view-space)
    const std::vector<float>& getCascadeBiasScales() const { return m_cascadeBiasScales; }
    const std::vector<double>& getCascadeOrthoSizes() const { return m_cascadeOrthoSizes; }

    // Shader reloading
    std::pair<bool, std::string> reloadShaders();
    
private:
    // Shadow map resources
    unsigned int m_shadowMapFBO{};
    unsigned int m_shadowDepthTextureArray{};
    unsigned int m_shadowMapWidth{};
    unsigned int m_shadowMapHeight{};
    unsigned int m_numCascades{3};
    bool m_shadowMapInitialized{false};

    // Cascade configuration
    std::vector<double> m_cascadeOrthoSizes;
    std::vector<glm::dmat4> m_cascadeProjectionMatrices;
    std::vector<glm::dmat4> m_lightSpaceMatrices;
    std::vector<float> m_cascadeBiasScales;
    // How far toward the light a caster may stand from the camera and still be
    // recorded, in metres. An occluder lies along the light axis from whatever it
    // shadows, so nothing but this bounds how distant it may be. Shared by every
    // cascade: a cascade's width bounds the receivers it covers, never how far
    // off the thing shadowing them is allowed to stand.
    double m_casterReach{1000.0};
    
    // Private methods
    void cleanupShadowMap();
    
    // Prevent copying
    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;
};
