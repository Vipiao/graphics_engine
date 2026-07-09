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

    // Weighted Blended OIT. beginOITPass binds the accumulation targets (sharing
    // the G-buffer depth for testing against opaque geometry, with depth writes
    // and per-target blend configured for accumulation). Transparent geometry is
    // drawn between the two calls; compositeOIT resolves the result over the lit
    // scene and restores default blend/depth state.
    void beginOITPass();
    void compositeOIT();

    // Ray-volume sub-pass: proxy-geometry volumetric effects that accumulate into
    // the same WBOIT targets as beginOITPass, drawn after the ordinary
    // transparents and before compositeOIT. Binds a depth-less variant of the OIT
    // framebuffer so the volume shader may sample the G-buffer depth (bound as a
    // texture) without a feedback loop; disables hardware depth testing (the
    // shader clamps against the sampled depth) and culls front faces so the
    // volume stays visible with the camera inside it. end restores the default
    // cull face and depth test for compositeOIT.
    void beginRayVolumeSubPass();
    void endRayVolumeSubPass();

    // Exposed for the ray-volume sub-pass, whose shader samples scene depth and
    // reconstructs view-space position per pixel. The scene color holds the lit
    // opaque result at sub-pass time (transparents are not composited yet), so a
    // body may read the underlying opaque color to emulate blend modes.
    unsigned int getGBufferDepthTexture() const { return m_gbufferDepth; }
    unsigned int getSceneColorTexture() const { return m_sceneColor; }
    unsigned int getGBufferWidth() const { return m_gbufferWidth; }
    unsigned int getGBufferHeight() const { return m_gbufferHeight; }
    bool isGBufferInitialized() const { return m_gbufferInitialized; }

    // Final post-processing pass: resample the finished scene color to the
    // default framebuffer, applying the Panini distortion (a plain copy when
    // both strengths are 0) and blue-noise dithering of the 8-bit quantization.
    void renderPostProcessing(const FrameRenderParams& params);
    
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

    // Weighted Blended OIT accumulation targets, sharing the G-buffer depth.
    unsigned int m_oitFBO{};
    unsigned int m_oitAccum{};      // RGBA16F: Σ(color·alpha·weight), Σ(alpha·weight)
    unsigned int m_oitRevealage{};  // R16F:    Π(1 - alpha)
    // Same two accumulation targets with no depth attachment, so the ray-volume
    // sub-pass can sample the G-buffer depth as a texture without a feedback loop.
    unsigned int m_oitNoDepthFBO{};

    // Shaders
    ShaderProgram m_lightingShaderProgram{};
    ShaderProgram m_postProcessShaderProgram{};
    ShaderProgram m_oitCompositeShaderProgram{};

    // Lighting pass resources
    unsigned int m_lightingVAO{};      // For fullscreen triangle

    // Tileable blue noise threshold map shared by the lighting pass (sample
    // jitter) and the post-processing pass (dithering).
    static constexpr int s_blueNoiseSize{ 64 };
    unsigned int m_blueNoiseTexture{};

    // SSAO
    SSAOSettings m_ssaoSettings{};
    std::vector<glm::vec3> m_ssaoKernel{};

    // Private methods
    void cleanupGBuffer();
    void generateSSAOKernel();
    void setBlueNoiseOffset(unsigned int programID, uint64_t frame);
    
    // Prevent copying
    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;
};