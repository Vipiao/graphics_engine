#include "ShadowRenderer.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include "utils/HashFunctions.h"
#include <stdexcept>

ShadowRenderer::ShadowRenderer() {
    // Constructor - shadow map will be initialized when setupShadowMap is called
}

ShadowRenderer::~ShadowRenderer() {
    cleanupShadowMap();
}

void ShadowRenderer::setupShadowMaps(unsigned int width, unsigned int height,
                                     unsigned int numCascades,
                                     const std::vector<double>& orthoSizes) {
    if (m_shadowMapInitialized) {
        cleanupShadowMap();
    }
    
    m_shadowMapWidth = width;
    m_shadowMapHeight = height;
    m_numCascades = numCascades;
    m_cascadeOrthoSizes = orthoSizes;
    
    // Ensure we have ortho sizes for all cascades
    if (m_cascadeOrthoSizes.size() < m_numCascades) {
        std::cerr << "Warning: Not enough ortho sizes provided, using defaults" << std::endl;
        m_cascadeOrthoSizes.resize(m_numCascades);
        for (unsigned int i = 0; i < m_numCascades; ++i) {
            m_cascadeOrthoSizes[i] = 50.0 * (1 << (i * 2)); // 50, 200, 800...
        }
    }

    // Calculate near and far planes
    double nearPlane = 0.1;
    double farPlane = m_shadowDistance * 2.0;
    
    // Pre-calculate projection matrices (these don't change between frames)
    m_cascadeProjectionMatrices.resize(m_numCascades);
    for (unsigned int i = 0; i < m_numCascades; ++i) {
        double orthoSize = m_cascadeOrthoSizes[i];
        m_cascadeProjectionMatrices[i] = glm::ortho(
            -orthoSize, orthoSize,
            -orthoSize, orthoSize,
            nearPlane, farPlane
        );
    }

    // Calculate depth range for bias calculations
    double depthRange = farPlane - nearPlane;

    // Pre-calculate bias scales for each cascade including depth range
    // Scale is (orthoSize / resolution) / depthRange
    m_cascadeBiasScales.resize(m_numCascades);
    for (unsigned int i = 0; i < m_numCascades; ++i) {
        double resolutionScale = static_cast<double>(width);
        double orthoScale = m_cascadeOrthoSizes[i];
        m_cascadeBiasScales[i] = static_cast<float>((orthoScale / resolutionScale) / depthRange);
    }
    
    // Initialize light space matrices storage
    m_lightSpaceMatrices.resize(m_numCascades);
    
    // Create framebuffer
    glGenFramebuffers(1, &m_shadowMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowMapFBO);
    
    // Create depth texture array
    glGenTextures(1, &m_shadowDepthTextureArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowDepthTextureArray);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, 
                 width, height, m_numCascades, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    //glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    
    // Set border color to maximum depth (white/far plane)
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
    
    // Attach depth texture array to framebuffer (base level)
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowDepthTextureArray, 0);
    
    // No color buffer needed for shadow mapping
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    
    // Check framebuffer completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Shadow map framebuffer not complete!");
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_shadowMapInitialized = true;
}

void ShadowRenderer::resizeShadowMaps(unsigned int width, unsigned int height) {
    // Preserve cascade configuration when resizing
    std::vector<double> orthoSizes = m_cascadeOrthoSizes;
    unsigned int numCascades = m_numCascades;
    setupShadowMaps(width, height, numCascades, orthoSizes);
}

void ShadowRenderer::cleanupShadowMap() {
    if (m_shadowMapInitialized) {
        glDeleteTextures(1, &m_shadowDepthTextureArray);
        glDeleteFramebuffers(1, &m_shadowMapFBO);
        m_shadowMapInitialized = false;
    }
}

void ShadowRenderer::beginShadowPass(const glm::dvec3& lightDir, const glm::dvec3& /* camPos */, uint64_t frameNum) {
    if (!m_shadowMapInitialized) {
        throw std::runtime_error("Shadow map not initialized. Call setupShadowMap() first.");
    }

    // Calculate light position in L-space (camera-relative coordinates)
    glm::dvec3 lightDirNormalized = glm::normalize(lightDir);

    // Generate deterministic jitter and project to plane perpendicular to light direction
    glm::dvec3 jitter3D = Hash::pcgUnit3(frameNum) - glm::dvec3(0.5); // Center around 0
    //glm::dvec3 jitterProjected = jitter3D - glm::dot(jitter3D, lightDirNormalized) * lightDirNormalized;
    //
    //// Scale by pixel size in finest cascade
    //double pixelSize = m_cascadeOrthoSizes[0] / static_cast<double>(m_shadowMapWidth);
    //glm::dvec3 jitter = jitterProjected * pixelSize;
    //
    glm::dvec3 lightPosL = -lightDirNormalized * m_shadowDistance;
    //lightPosL += jitter*100.;
    double ll = glm::dot(jitter3D, jitter3D);
    glm::dvec3 up = {0,1,0};
    if (ll > 0.)
    {
        up = jitter3D / glm::sqrt(ll);
    }
    

    // Create light view matrix in L-space
    glm::dmat4 lightView = glm::lookAt(
        lightPosL,
        glm::dvec3(0.0, 0.0, 0.0),  // Look at origin (camera in L-space)
        up
    );
    
    // Combine pre-calculated projections with view matrix
    for (unsigned int i = 0; i < m_numCascades; ++i) {
        m_lightSpaceMatrices[i] = m_cascadeProjectionMatrices[i] * lightView;
    }
    
    // Bind shadow map framebuffer for depth rendering
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowMapFBO);
    glViewport(0, 0, m_shadowMapWidth, m_shadowMapHeight);
    
    // Clear depth buffer
    glClear(GL_DEPTH_BUFFER_BIT);
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // Optional: Enable front face culling to reduce peter panning
    // (render back faces to shadow map to push shadows away from surfaces)
    glEnable(GL_CULL_FACE);
    //glCullFace(GL_FRONT);
    glCullFace(GL_BACK);
}

void ShadowRenderer::bindCascadeLayer(unsigned int cascadeIndex) {
    if (!m_shadowMapInitialized) {
        throw std::runtime_error("Shadow map not initialized");
    }
    if (cascadeIndex >= m_numCascades) {
        throw std::runtime_error("Cascade index out of range");
    }
    
    // Bind specific cascade layer to framebuffer
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
                              m_shadowDepthTextureArray, 0, cascadeIndex);
    
    // Clear this cascade's depth buffer
    glClear(GL_DEPTH_BUFFER_BIT);
}

std::vector<glm::dmat4> ShadowRenderer::getLightSpaceMatricesForViewSpace(const glm::dmat4& viewMatrix) const {
    // Transform cascade matrices from L-space to work with view-space fragment positions
    // Fragment positions in lighting shader are in view space, so we need:
    // cascadeMatrix * inverse(viewMatrix) to transform view-space -> L-space -> cascade-space
    glm::dmat4 inverseView = glm::inverse(viewMatrix);
    
    std::vector<glm::dmat4> viewSpaceMatrices;
    viewSpaceMatrices.reserve(m_lightSpaceMatrices.size());
    for (const auto& cascadeMatrix : m_lightSpaceMatrices) {
        viewSpaceMatrices.push_back(cascadeMatrix * inverseView);
    }
    return viewSpaceMatrices;
}

void ShadowRenderer::endShadowPass() {
    // Restore default settings
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::pair<bool, std::string> ShadowRenderer::reloadShaders() {
    // ShadowRenderer currently uses depth shaders from MeshHandler and InstanceHandler
    // If it gets its own shaders in the future, reload them here
    return {true, "ShadowRenderer: No shaders to reload (uses external depth shaders)"};
}