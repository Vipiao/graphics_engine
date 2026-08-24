#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <utility>
#include <glad/glad.h>
#include "math/Cylinder.h"

class ShadowRenderer {
public:
    ShadowRenderer();
    ~ShadowRenderer();
    
    // Shadow map management
    // casterReach is how far toward the light a caster may stand from what it
    // shadows; see m_casterReach. Every argument is scene-scale metres and none is
    // defaulted: a shadow setup that fits one scene fits no other, and a default
    // here would be a second place the caller's numbers are written down.
    void setupShadowMaps(unsigned int width, unsigned int height,
                         unsigned int numCascades,
                         const std::vector<double>& orthoSizes,
                         double casterReach);
    
    // Fraction of its own radius each cascade's centre is pushed along the view
    // direction. Coverage behind the camera is never seen, so it is spent on reach
    // in front; below 1 so the camera stays inside every cascade.
    static constexpr double k_cascadePush{0.9};

    // Places this frame's cascades. camForward is the unit view direction in world
    // axes. Separate from beginShadowPass because what the cascades cover has to be
    // known before anything selects against them, which is well before the pass
    // that draws into them binds anything.
    void updateCascades(const glm::dvec3& lightDir, const glm::dvec3& camForward,
                        uint64_t frameNum);

    void beginShadowPass();
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

    // What each cascade can be shadowed by, in camera-relative world axes and in
    // the order the cascades are drawn. A caster stands up-light of what it
    // shadows, so these run a full caster reach toward the light -- the reason a
    // caller is handed these rather than left to measure a cascade for itself.
    //
    // Round rather than square, and so unaffected by the roll of the light basis:
    // the lighting pass takes each cascade out to an inscribed sphere, and under
    // light this parallel a caster outside that radius can only shadow points
    // outside it too.
    const std::vector<Cylinder>& getCasterVolumes() const { return m_casterVolumes; }

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
    std::vector<Cylinder> m_casterVolumes;
    // How far toward the light a caster may stand from the camera and still be
    // recorded, in metres. An occluder lies along the light axis from whatever it
    // shadows, so nothing but this bounds how distant it may be. Shared by every
    // cascade: a cascade's width bounds the receivers it covers, never how far
    // off the thing shadowing them is allowed to stand.
    //
    // Negative until setupShadowMaps names it: no reach is a length, so an
    // unconfigured renderer holds a value no scene could have asked for rather
    // than a small plausible one.
    double m_casterReach{-1.0};
    
    // Private methods
    void cleanupShadowMap();
    
    // Prevent copying
    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;
};
