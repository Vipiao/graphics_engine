#include "DeferredRenderer.h"
#include "math/BlueNoise.h"
#include "math/DekkerArithmetic.h"
#include "utils/HashFunctions.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

DeferredRenderer::DeferredRenderer() {
    // Load lighting pass shaders
    m_lightingShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/lighting_vertex_shader.vert");
    m_lightingShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/lighting_fragment_shader.frag");
    m_lightingShaderProgram.linkShaders();

    // Load post-processing pass shaders (fullscreen triangle vertex shader is shared)
    m_postProcessShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/lighting_vertex_shader.vert");
    m_postProcessShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/post_processing_fragment_shader.frag");
    m_postProcessShaderProgram.linkShaders();

    // Load OIT composite shaders (fullscreen triangle vertex shader is shared)
    m_oitCompositeShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/lighting_vertex_shader.vert");
    m_oitCompositeShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/oit_composite_fragment_shader.frag");
    m_oitCompositeShaderProgram.linkShaders();

    // Setup lighting VAO (for fullscreen triangle)
    glGenVertexArrays(1, &m_lightingVAO);
    // No vertex buffer needed - geometry generated in vertex shader

    // Tileable blue noise threshold map, generated once (deterministic) and
    // shared by the lighting pass (sample jitter) and the post-processing
    // pass (dithering). Fetched with texelFetch, so filtering is irrelevant.
    std::vector<uint8_t> blueNoise{ BlueNoise::generate(s_blueNoiseSize) };
    glGenTextures(1, &m_blueNoiseTexture);
    glBindTexture(GL_TEXTURE_2D, m_blueNoiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, s_blueNoiseSize, s_blueNoiseSize, 0,
                 GL_RED, GL_UNSIGNED_BYTE, blueNoise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    generateSSAOKernel();
}

DeferredRenderer::~DeferredRenderer() {
    cleanupGBuffer();
    glDeleteTextures(1, &m_blueNoiseTexture);
    glDeleteVertexArrays(1, &m_lightingVAO);
}

void DeferredRenderer::generateSSAOKernel() {
    m_ssaoKernel.clear();
    m_ssaoKernel.reserve(m_ssaoSettings.sampleCount);
    
    int seed = 0;
    for (int i = 0; i < m_ssaoSettings.sampleCount; ++i) {
        glm::vec3 sample;
        
        // Generate point in unit cube, reject if outside unit sphere
        do {
            glm::dvec3 rand3 = Hash::pcgUnit3(static_cast<uint64_t>(seed++));
            double x = rand3.x * 2.0 - 1.0;
            double y = rand3.y * 2.0 - 1.0;
            double z = rand3.z * 1.1 - 0.1; // Keep z positive for hemisphere
            sample = glm::vec3(x, y, z);
        } while (glm::length(sample) > 1.0);
        
        // Normalize and scale by random factor
        sample = glm::normalize(sample);
        double scale = Hash::pcgUnit(static_cast<uint64_t>(i + 1000));
        
        // Scale samples to be more concentrated near center
        scale = 0.1 + scale * 0.9; // Range [0.1, 1.0]
        //scale = scale * scale; // Square to concentrate near center
        
        sample *= static_cast<float>(scale);
        m_ssaoKernel.push_back(sample);
    }
}

// Toroidal shift of the blue noise tile for this frame. The R2 sequence
// (powers of the plastic number's reciprocal) is a 2D low-discrepancy
// sequence: consecutive frames land far apart in the tile and any run of
// frames covers it evenly, so per-pixel noise averages out over time without
// visible scrolling or a short repeat cycle.
void DeferredRenderer::setBlueNoiseOffset(unsigned int programID, uint64_t frame) {
    GLint offsetLoc = glGetUniformLocation(programID, "u_blueNoiseOffset");
    if (offsetLoc == -1) {
        return;
    }
    double frameDouble{ static_cast<double>(frame) };
    double x{ std::fmod(frameDouble * 0.7548776662466927, 1.0) };
    double y{ std::fmod(frameDouble * 0.5698402909980532, 1.0) };
    glUniform2i(offsetLoc,
                static_cast<int>(x * s_blueNoiseSize),
                static_cast<int>(y * s_blueNoiseSize));
}

void DeferredRenderer::setSSAOSettings(const SSAOSettings& settings) {
    m_ssaoSettings = settings;
    generateSSAOKernel(); // Regenerate kernel if sample count changed
}

void DeferredRenderer::setupGBuffer(unsigned int width, unsigned int height) {
    // Clean up and skip G-buffer creation for invalid dimensions (e.g., when window is minimized)
    if (width == 0 || height == 0) {
        cleanupGBuffer();
        return;
    }
    if (m_gbufferInitialized) {
        cleanupGBuffer();
    }
    
    m_gbufferWidth = width;
    m_gbufferHeight = height;
    
    // Create framebuffer
    glGenFramebuffers(1, &m_gbufferFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gbufferFBO);
    
    // Create G-buffer textures
    glGenTextures(1, &m_gbufferAlbedo);
    glGenTextures(1, &m_gbufferNormal);
    glGenTextures(1, &m_gbufferMaterial);
    glGenTextures(1, &m_gbufferDepth);
    
    // Setup albedo texture (RT0) (R G B + Metallic)
    glBindTexture(GL_TEXTURE_2D, m_gbufferAlbedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gbufferAlbedo, 0);
    
    // Setup normal texture (RT1) (Nx Ny Nz Roguhness)
    glBindTexture(GL_TEXTURE_2D, m_gbufferNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gbufferNormal, 0);
    
    // Setup material texture (RT2) (Emissiveness + has geometry + occlusion + alpha)
    glBindTexture(GL_TEXTURE_2D, m_gbufferMaterial);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gbufferMaterial, 0);
    
    // Setup depth texture
    glBindTexture(GL_TEXTURE_2D, m_gbufferDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gbufferDepth, 0);
    
    // Set draw buffers
    unsigned int attachments[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, attachments);

    // Check framebuffer completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("G-buffer framebuffer not complete!");
    }

    // Setup scene color texture (lighting output + forward passes). Float
    // format keeps lighting gradients continuous until the post-processing
    // pass quantizes them to the 8-bit framebuffer (with dithering). Linear
    // filtering so the Panini resample interpolates smoothly.
    glGenTextures(1, &m_sceneColor);
    glBindTexture(GL_TEXTURE_2D, m_sceneColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Lighting scene FBO: color only, so the lighting shader may sample the
    // G-buffer depth texture without a framebuffer feedback loop.
    glGenFramebuffers(1, &m_sceneLightingFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColor, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Scene lighting framebuffer not complete!");
    }

    // Forward scene FBO: same color texture plus the G-buffer depth, so
    // transparent forward geometry depth-tests against the opaque scene.
    glGenFramebuffers(1, &m_sceneForwardFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneForwardFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gbufferDepth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Scene forward framebuffer not complete!");
    }

    // OIT accumulation targets. Float accum for HDR additive blending; single
    // channel revealage holds the product of transparencies. Both share the
    // G-buffer depth so transparents test against opaque geometry (never write).
    glGenTextures(1, &m_oitAccum);
    glBindTexture(GL_TEXTURE_2D, m_oitAccum);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_oitRevealage);
    glBindTexture(GL_TEXTURE_2D, m_oitRevealage);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_oitFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_oitFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_oitAccum, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_oitRevealage, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gbufferDepth, 0);
    {
        unsigned int oitAttachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, oitAttachments);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("OIT framebuffer not complete!");
    }

    // Depth-less variant sharing the same accumulation targets. The ray-volume
    // sub-pass binds this so it can sample the G-buffer depth as a texture.
    glGenFramebuffers(1, &m_oitNoDepthFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_oitNoDepthFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_oitAccum, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_oitRevealage, 0);
    {
        unsigned int oitAttachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, oitAttachments);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("OIT depth-less framebuffer not complete!");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gbufferInitialized = true;
}

void DeferredRenderer::resizeGBuffer(unsigned int width, unsigned int height) {
    // Clean up and skip resize for invalid dimensions (e.g., when window is minimized)  
    if (width == 0 || height == 0) {
        cleanupGBuffer();
        return;
    }
    setupGBuffer(width, height);
}

void DeferredRenderer::cleanupGBuffer() {
    if (m_gbufferInitialized) {
        glDeleteTextures(1, &m_gbufferAlbedo);
        glDeleteTextures(1, &m_gbufferNormal);
        glDeleteTextures(1, &m_gbufferMaterial);
        glDeleteTextures(1, &m_gbufferDepth);
        glDeleteTextures(1, &m_sceneColor);
        glDeleteTextures(1, &m_oitAccum);
        glDeleteTextures(1, &m_oitRevealage);
        glDeleteFramebuffers(1, &m_gbufferFBO);
        glDeleteFramebuffers(1, &m_sceneLightingFBO);
        glDeleteFramebuffers(1, &m_sceneForwardFBO);
        glDeleteFramebuffers(1, &m_oitFBO);
        glDeleteFramebuffers(1, &m_oitNoDepthFBO);
        m_gbufferInitialized = false;
    }
}

void DeferredRenderer::beginGeometryPass() {
    // Skip geometry pass if G-buffer is not initialized (e.g., window minimized)
    if (!m_gbufferInitialized) {
        return;
    }
    if (!m_gbufferInitialized) {
        throw std::runtime_error("G-buffer not initialized. Call setupGBuffer() first.");
    }
    
    // Bind G-buffer for rendering
    glBindFramebuffer(GL_FRAMEBUFFER, m_gbufferFBO);
    glViewport(0, 0, m_gbufferWidth, m_gbufferHeight);

    // Clear each buffer with appropriate values
    glClearBufferfv(GL_COLOR, 0, glm::value_ptr(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f))); // Albedo: black
    glClearBufferfv(GL_COLOR, 1, glm::value_ptr(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f))); // Normal: zero
    glClearBufferfv(GL_COLOR, 2, glm::value_ptr(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f))); // Material
    glClear(GL_DEPTH_BUFFER_BIT); // Clear depth buffer
}

void DeferredRenderer::endGeometryPassAndRenderLighting(
    const FrameRenderParams& params,
    unsigned int numCascades,
    const std::vector<glm::dmat4>& cascadeMatrices,
    const std::vector<float>& cascadeBiasScales,
    const std::vector<double>& cascadeOrthoSizes,
    unsigned int shadowMapTexture,
    bool shadowsEnabled)
{
    // Skip lighting pass if G-buffer is not initialized (e.g., window minimized)
    if (!m_gbufferInitialized) {
        return;
    }
    // Switch to the offscreen scene target for the lighting pass. Cleared with
    // the frame's clear color so discarded (background) pixels show it.
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFBO);
    glViewport(0, 0, m_gbufferWidth, m_gbufferHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    
    // Use lighting shader
    m_lightingShaderProgram.use();
    unsigned int lightingProgramID = m_lightingShaderProgram.getID();

    // Set SSAO uniforms
    GLint ssaoEnabledLoc = glGetUniformLocation(lightingProgramID, "u_ssaoEnabled");
    if (ssaoEnabledLoc != -1) {
        glUniform1i(ssaoEnabledLoc, m_ssaoSettings.enabled ? 1 : 0);
    }
    
    GLint ssaoRadiusLoc = glGetUniformLocation(lightingProgramID, "u_ssaoRadius");
    if (ssaoRadiusLoc != -1) {
        glUniform1f(ssaoRadiusLoc, static_cast<float>(m_ssaoSettings.radius));
    }
    
    GLint ssaoBiasLoc = glGetUniformLocation(lightingProgramID, "u_ssaoBias");
    if (ssaoBiasLoc != -1) {
        glUniform1f(ssaoBiasLoc, static_cast<float>(m_ssaoSettings.bias));
    }

    // Blue noise drives the temporal sample jitter (SSAO kernel rotation,
    // shadow PCF offsets, SSR ray start offsets).
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_blueNoiseTexture);
    glUniform1i(glGetUniformLocation(lightingProgramID, "u_blueNoise"), 5);
    setBlueNoiseOffset(lightingProgramID, params.frame);

    // Set inverse projection matrix for position reconstruction
    glm::mat4 projectionFloat = glm::mat4(params.projection);
    glm::mat4 inverseProjection = glm::inverse(projectionFloat);
    GLint inverseProjectionLoc = glGetUniformLocation(lightingProgramID, "u_inverseProjection");
    if (inverseProjectionLoc != -1) {
        glUniformMatrix4fv(inverseProjectionLoc, 1, GL_FALSE, glm::value_ptr(inverseProjection));
    }
    
    // Convert double precision matrices to float for OpenGL
    GLint projectionLightingLoc = glGetUniformLocation(lightingProgramID, "u_projection");
    if (projectionLightingLoc != -1) {
        glUniformMatrix4fv(projectionLightingLoc, 1, GL_FALSE, glm::value_ptr(projectionFloat));
    }

    // Set screen size for position reconstruction
    GLint screenSizeLoc = glGetUniformLocation(lightingProgramID, "u_screenSize");
    if (screenSizeLoc != -1) {
        glUniform2f(screenSizeLoc, static_cast<float>(m_gbufferWidth), static_cast<float>(m_gbufferHeight));
    }
    
    // Set SSAO kernel samples
    for (int i = 0; i < m_ssaoSettings.sampleCount && i < 32; ++i) {
        std::string uniformName = "u_ssaoSamples[" + std::to_string(i) + "]";
        GLint sampleLoc = glGetUniformLocation(lightingProgramID, uniformName.c_str());
        if (sampleLoc != -1) {
            glUniform3fv(sampleLoc, 1, glm::value_ptr(m_ssaoKernel[i]));
        }
    }

    // Set shadows enabled uniform
    GLint shadowsEnabledLoc = glGetUniformLocation(lightingProgramID, "u_shadowsEnabled");
    if (shadowsEnabledLoc != -1) {
        glUniform1i(shadowsEnabledLoc, shadowsEnabled ? 1 : 0);
    }
    
    // Bind G-buffer textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gbufferAlbedo);
    glUniform1i(glGetUniformLocation(lightingProgramID, "gAlbedo"), 0);
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gbufferNormal);
    glUniform1i(glGetUniformLocation(lightingProgramID, "gNormal"), 1);
    
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_gbufferMaterial);
    glUniform1i(glGetUniformLocation(lightingProgramID, "gMaterial"), 2);
    
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_gbufferDepth);
    glUniform1i(glGetUniformLocation(lightingProgramID, "gDepth"), 3);
    
    // Bind shadow map texture
    if (shadowsEnabled && shadowMapTexture != 0 && numCascades > 0) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMapTexture);
        glUniform1i(glGetUniformLocation(lightingProgramID, "u_shadowMap"), 4);

        // Pass number of cascades
        GLint numCascadesLoc = glGetUniformLocation(lightingProgramID, "u_numCascades");
        if (numCascadesLoc != -1) {
            glUniform1i(numCascadesLoc, static_cast<int>(numCascades));
        }
        
        // Pass cascade light space matrices array
        GLint lightSpaceMatricesLoc = glGetUniformLocation(lightingProgramID, "u_lightSpaceMatrices");
        if (lightSpaceMatricesLoc != -1 && cascadeMatrices.size() > 0) {
            // Convert double precision matrices to float for OpenGL
            std::vector<glm::mat4> cascadeMatricesFloat(cascadeMatrices.size());
            for (size_t i = 0; i < cascadeMatrices.size(); ++i) {
                cascadeMatricesFloat[i] = glm::mat4(cascadeMatrices[i]);
            }
            glUniformMatrix4fv(lightSpaceMatricesLoc, static_cast<GLsizei>(cascadeMatricesFloat.size()),
                             GL_FALSE, glm::value_ptr(cascadeMatricesFloat[0]));
        }

        // Pass cascade bias scales array
        GLint cascadeBiasScalesLoc = glGetUniformLocation(lightingProgramID, "u_cascadeBiasScales");
        if (cascadeBiasScalesLoc != -1 && cascadeBiasScales.size() > 0) {
            // Ensure we don't exceed shader array size
            size_t numScales = std::min(cascadeBiasScales.size(), static_cast<size_t>(4));
            glUniform1fv(cascadeBiasScalesLoc, static_cast<GLsizei>(numScales),
                        cascadeBiasScales.data());
        }

        // Pass cascade ortho sizes array
        GLint cascadeOrthoSizesLoc = glGetUniformLocation(lightingProgramID, "u_cascadeOrthoSizes");
        if (cascadeOrthoSizesLoc != -1 && cascadeOrthoSizes.size() > 0) {
            // Convert double to float for OpenGL
            std::vector<float> orthoSizesFloat(cascadeOrthoSizes.begin(), cascadeOrthoSizes.end());
            size_t numSizes = std::min(orthoSizesFloat.size(), static_cast<size_t>(4));
            glUniform1fv(cascadeOrthoSizesLoc, static_cast<GLsizei>(numSizes),
                        orthoSizesFloat.data());
        }
    }

    // Set lighting uniforms
    GLint lightDirLoc = glGetUniformLocation(lightingProgramID, "u_lightDir");
    if (lightDirLoc != -1) {
        // Transform light direction to view space
        glm::mat4 viewFloat = glm::mat4(params.view);
        glm::vec4 lightDirView = viewFloat * glm::vec4(glm::vec3(params.lightDir), 0.0f);
        glm::vec3 lightDirFloat(lightDirView.x, lightDirView.y, lightDirView.z);
        glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDirFloat));
    }
    
    // Render fullscreen triangle
    glBindVertexArray(m_lightingVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Re-enable depth testing for subsequent rendering
    glEnable(GL_DEPTH_TEST);

    // Leave the scene target bound with the G-buffer depth attached, so forward
    // passes (transparents) render into the scene and depth-test against it.
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneForwardFBO);
}

void DeferredRenderer::beginOITPass() {
    if (!m_gbufferInitialized) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_oitFBO);
    glViewport(0, 0, m_gbufferWidth, m_gbufferHeight);

    // Accumulation starts empty; revealage (fraction of background surviving)
    // starts fully opaque-free at 1 and is multiplied down per fragment.
    glClearBufferfv(GL_COLOR, 0, glm::value_ptr(glm::vec4(0.0f)));
    glClearBufferfv(GL_COLOR, 1, glm::value_ptr(glm::vec4(1.0f)));

    // Depth test against opaque geometry, but never write: transparent
    // fragments must not occlude each other (order independence).
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // Per-target blend: accum accumulates additively, revealage multiplies by
    // (1 - alpha). Requires indexed blend (GL 4.0+).
    glEnable(GL_BLEND);
    glBlendFunci(0, GL_ONE, GL_ONE);
    glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
}

void DeferredRenderer::compositeOIT() {
    if (!m_gbufferInitialized) {
        return;
    }

    // Blend the resolved transparency over the lit scene color. The color-only
    // scene FBO avoids a feedback loop with the shared G-buffer depth.
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFBO);
    glViewport(0, 0, m_gbufferWidth, m_gbufferHeight);
    glDisable(GL_DEPTH_TEST);

    // Non-indexed call resets blend for all draw buffers, undoing the per-target
    // functions from beginOITPass. Expands to avgColor*(1-reveal) + scene*reveal.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);

    m_oitCompositeShaderProgram.use();
    unsigned int programID = m_oitCompositeShaderProgram.getID();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_oitAccum);
    glUniform1i(glGetUniformLocation(programID, "u_accum"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_oitRevealage);
    glUniform1i(glGetUniformLocation(programID, "u_revealage"), 1);

    glBindVertexArray(m_lightingVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Restore default state for the following passes and the next frame's depth
    // clear (which needs depth writes enabled).
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void DeferredRenderer::beginRayVolumeSubPass() {
    if (!m_gbufferInitialized) {
        return;
    }

    // Same accumulation targets as the OIT pass, but the depth-less FBO so the
    // volume shader can sample the G-buffer depth. Not cleared: the ordinary
    // transparents already accumulated here this frame.
    glBindFramebuffer(GL_FRAMEBUFFER, m_oitNoDepthFBO);
    glViewport(0, 0, m_gbufferWidth, m_gbufferHeight);

    // Match the WBOIT blend: additive accum, multiplicative revealage.
    glEnable(GL_BLEND);
    glBlendFunci(0, GL_ONE, GL_ONE);
    glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);

    // Render back faces only, with no hardware depth test: the shader discards
    // and soft-clamps against the sampled G-buffer depth instead, so the volume
    // survives the camera being inside it and never hard-clips against surfaces.
    glDisable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);
}

void DeferredRenderer::endRayVolumeSubPass() {
    if (!m_gbufferInitialized) {
        return;
    }

    // Restore the engine defaults (back-face culling, depth test on) that
    // compositeOIT and later passes expect.
    glCullFace(GL_BACK);
    glEnable(GL_DEPTH_TEST);
}

void DeferredRenderer::renderPostProcessing(const FrameRenderParams& params) {
    // Skip if the scene target is not initialized (e.g., window minimized)
    if (!m_gbufferInitialized) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_gbufferWidth, m_gbufferHeight);
    glDisable(GL_DEPTH_TEST);

    m_postProcessShaderProgram.use();
    unsigned int postProcessProgramID = m_postProcessShaderProgram.getID();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColor);
    glUniform1i(glGetUniformLocation(postProcessProgramID, "u_sceneColor"), 0);

    // Projection is needed to convert between screen and tan-space coordinates
    GLint projectionLoc = glGetUniformLocation(postProcessProgramID, "u_projection");
    if (projectionLoc != -1) {
        glm::mat4 projectionFloat{ params.projection };
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projectionFloat));
    }

    // Panini strengths (0 = off, the shader falls back to a plain copy)
    GLint paniniHorizontalLoc = glGetUniformLocation(postProcessProgramID, "u_paniniHorizontal");
    if (paniniHorizontalLoc != -1) {
        glUniform1f(paniniHorizontalLoc, static_cast<float>(params.paniniHorizontal));
    }
    GLint paniniVerticalLoc = glGetUniformLocation(postProcessProgramID, "u_paniniVertical");
    if (paniniVerticalLoc != -1) {
        glUniform1f(paniniVerticalLoc, static_cast<float>(params.paniniVertical));
    }

    // Fit scale: zooms the output just enough that the distorted image fills
    // the screen without sampling outside the rendered frustum.
    GLint fitScaleLoc = glGetUniformLocation(postProcessProgramID, "u_paniniFitScale");
    if (fitScaleLoc != -1) {
        glUniform1f(fitScaleLoc, static_cast<float>(params.paniniFitScale));
    }

    // Blue noise dither of the final 8-bit quantization.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_blueNoiseTexture);
    glUniform1i(glGetUniformLocation(postProcessProgramID, "u_blueNoise"), 1);
    setBlueNoiseOffset(postProcessProgramID, params.frame);
    GLint ditherStrengthLoc = glGetUniformLocation(postProcessProgramID, "u_ditherStrength");
    if (ditherStrengthLoc != -1) {
        glUniform1f(ditherStrengthLoc, static_cast<float>(params.ditherStrength));
    }

    // Render fullscreen triangle
    glBindVertexArray(m_lightingVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Re-enable depth testing for subsequent rendering
    glEnable(GL_DEPTH_TEST);
}

std::pair<bool, std::string> DeferredRenderer::reloadShaders() {
   auto [lightingSuccess, lightingError] = m_lightingShaderProgram.reloadShaders();
   auto [postProcessSuccess, postProcessError] = m_postProcessShaderProgram.reloadShaders();
   auto [oitSuccess, oitError] = m_oitCompositeShaderProgram.reloadShaders();

   bool success = lightingSuccess && postProcessSuccess && oitSuccess;
   std::string errors;
   if (!lightingSuccess) { errors += "DeferredRenderer lighting shader: " + lightingError + "\n"; }
   if (!postProcessSuccess) {
      errors += "DeferredRenderer post-processing shader: " + postProcessError + "\n";
   }
   if (!oitSuccess) { errors += "DeferredRenderer OIT composite shader: " + oitError + "\n"; }

   return {success, success ?
      "DeferredRenderer shaders reloaded successfully" : errors};
}