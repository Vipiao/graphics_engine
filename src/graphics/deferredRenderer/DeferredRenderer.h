#pragma once

#include "../ShaderProgram.h"
#include "../FrameRenderParams.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <glad/glad.h>

struct SSAOSettings {
   bool enabled = true;
   int sampleCount = 32;
   double radius = 0.5;
   double bias = 0.2;
   double ambientInfluence = 1.0;
   double diffuseInfluence = 0.2;
};

class DeferredRenderer {
public:
    DeferredRenderer();
    ~DeferredRenderer();
    
    // G-buffer management  
    void setupGBuffer(unsigned int width, unsigned int height);
    void resizeGBuffer(unsigned int width, unsigned int height);
    
    // Two-pass rendering coordination. The lighting pass renders into an
    // offscreen scene color target and leaves it bound (with the G-buffer depth
    // attached), so forward passes drawn afterwards land in the same image.
    void beginGeometryPass();
    void endGeometryPassAndRenderLighting(
        const FrameRenderParams& params,
        unsigned int numCascades,
        const std::vector<glm::dmat4>& cascadeMatrices,
        const std::vector<float>& cascadeBiasScales,
        const std::vector<double>& cascadeOrthoSizes,
        unsigned int shadowMapTexture = 0,
        bool shadowsEnabled = false);

    // Final pass: resample the finished scene color to the default framebuffer,
    // applying the Panini distortion (a plain copy when both strengths are 0).
    void renderSceneToScreen(const FrameRenderParams& params);
    
    // SSAO configuration
    SSAOSettings& getSSAOSettings() { return m_ssaoSettings; }
    const SSAOSettings& getSSAOSettings() const { return m_ssaoSettings; }
    void setSSAOSettings(const SSAOSettings& settings);

    // Shader reloading
    std::pair<bool, std::string> reloadShaders();
    
private:
    // G-buffer resources
    unsigned int m_gbufferFBO{};
    unsigned int m_gbufferAlbedo{};    // RT0: RGB=albedo, A=metallic
    unsigned int m_gbufferNormal{};    // RT1: RGB=normal, A=roughness  
    unsigned int m_gbufferMaterial{};  // RT2: Material flags + occlusion
    unsigned int m_gbufferDepth{};
    unsigned int m_gbufferWidth{};
    unsigned int m_gbufferHeight{};
    bool m_gbufferInitialized{false};

    // Offscreen scene color target (lighting + forward passes). Two FBOs share
    // the color texture: the lighting FBO has no depth attachment so the
    // lighting shader can sample the G-buffer depth without a feedback loop;
    // the forward FBO attaches the G-buffer depth for transparent depth testing.
    unsigned int m_sceneColor{};
    unsigned int m_sceneLightingFBO{};
    unsigned int m_sceneForwardFBO{};

    // Shaders
    ShaderProgram m_lightingShaderProgram{};
    ShaderProgram m_paniniShaderProgram{};
    
    // Lighting pass resources
    unsigned int m_lightingVAO{};      // For fullscreen triangle
    
    // SSAO
    SSAOSettings m_ssaoSettings{};
    std::vector<glm::vec3> m_ssaoKernel{};
    
    // Private methods
    void cleanupGBuffer();
    void generateSSAOKernel();
    
    // Prevent copying
    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;
};