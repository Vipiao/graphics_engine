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
    void setupShadowMaps(unsigned int width, unsigned int height, 
                         unsigned int numCascades = 3,
                         const std::vector<double>& orthoSizes = {50.0, 200.0, 800.0});
    void resizeShadowMaps(unsigned int width, unsigned int height);
    
    // Shadow pass rendering
    void beginShadowPass(const glm::dvec3& lightDir, const glm::dvec3& camPos, uint64_t frameNum);
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
    double m_shadowDistance{1000.0};
    
    // Private methods
    void cleanupShadowMap();
    
    // Prevent copying
    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;
};
