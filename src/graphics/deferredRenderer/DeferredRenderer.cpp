#include "DeferredRenderer.h"
#include "math/DekkerArithmetic.h"
#include "utils/HashFunctions.h"
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

    // Load Panini post pass shaders (fullscreen triangle vertex shader is shared)
    m_paniniShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/lighting_vertex_shader.vert");
    m_paniniShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/deferredRenderer/panini_post_fragment_shader.frag");
    m_paniniShaderProgram.linkShaders();

    // Setup lighting VAO (for fullscreen triangle)
    glGenVertexArrays(1, &m_lightingVAO);
    // No vertex buffer needed - geometry generated in vertex shader

    generateSSAOKernel();
}

DeferredRenderer::~DeferredRenderer() {
    cleanupGBuffer();
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

    // Setup scene color texture (lighting output + forward passes). Linear
    // filtering so the Panini post pass resamples smoothly.
    glGenTextures(1, &m_sceneColor);
    glBindTexture(GL_TEXTURE_2D, m_sceneColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
        glDeleteFramebuffers(1, &m_gbufferFBO);
        glDeleteFramebuffers(1, &m_sceneLightingFBO);
        glDeleteFramebuffers(1, &m_sceneForwardFBO);
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
     
    GLint lightingTimeRemainderLoc = glGetUniformLocation(lightingProgramID, "u_timeRemainder");
    if (lightingTimeRemainderLoc != -1) {
        glUniform1f(lightingTimeRemainderLoc, static_cast<float>(params.timeRemainder));
    }

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

void DeferredRenderer::renderSceneToScreen(const FrameRenderParams& params) {
    // Skip if the scene target is not initialized (e.g., window minimized)
    if (!m_gbufferInitialized) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_gbufferWidth, m_gbufferHeight);
    glDisable(GL_DEPTH_TEST);

    m_paniniShaderProgram.use();
    unsigned int paniniProgramID = m_paniniShaderProgram.getID();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColor);
    glUniform1i(glGetUniformLocation(paniniProgramID, "u_sceneColor"), 0);

    // Projection is needed to convert between screen and tan-space coordinates
    GLint projectionLoc = glGetUniformLocation(paniniProgramID, "u_projection");
    if (projectionLoc != -1) {
        glm::mat4 projectionFloat{ params.projection };
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projectionFloat));
    }

    // Panini strengths (0 = off, the shader falls back to a plain copy)
    GLint paniniHorizontalLoc = glGetUniformLocation(paniniProgramID, "u_paniniHorizontal");
    if (paniniHorizontalLoc != -1) {
        glUniform1f(paniniHorizontalLoc, static_cast<float>(params.paniniHorizontal));
    }
    GLint paniniVerticalLoc = glGetUniformLocation(paniniProgramID, "u_paniniVertical");
    if (paniniVerticalLoc != -1) {
        glUniform1f(paniniVerticalLoc, static_cast<float>(params.paniniVertical));
    }

    // Fit scale: zooms the output just enough that the distorted image fills
    // the screen without sampling outside the rendered frustum.
    GLint fitScaleLoc = glGetUniformLocation(paniniProgramID, "u_paniniFitScale");
    if (fitScaleLoc != -1) {
        glUniform1f(fitScaleLoc, static_cast<float>(params.paniniFitScale));
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
   auto [paniniSuccess, paniniError] = m_paniniShaderProgram.reloadShaders();

   bool success = lightingSuccess && paniniSuccess;
   std::string errors;
   if (!lightingSuccess) { errors += "DeferredRenderer lighting shader: " + lightingError + "\n"; }
   if (!paniniSuccess) { errors += "DeferredRenderer panini shader: " + paniniError + "\n"; }

   return {success, success ?
      "DeferredRenderer shaders reloaded successfully" : errors};
}