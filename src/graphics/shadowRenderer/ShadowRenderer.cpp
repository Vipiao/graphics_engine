#include "ShadowRenderer.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
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
                                     const std::vector<double>& orthoSizes,
                                     double casterReach) {
    if (m_shadowMapInitialized) {
        cleanupShadowMap();
    }
    
    m_shadowMapWidth = width;
    m_shadowMapHeight = height;
    m_numCascades = numCascades;
    m_cascadeOrthoSizes = orthoSizes;
    m_casterReach = casterReach;
    
    // Ensure we have ortho sizes for all cascades
    if (m_cascadeOrthoSizes.size() < m_numCascades) {
        std::cerr << "Warning: Not enough ortho sizes provided, using defaults" << std::endl;
        m_cascadeOrthoSizes.resize(m_numCascades);
        for (unsigned int i = 0; i < m_numCascades; ++i) {
            m_cascadeOrthoSizes[i] = 50.0 * (1 << (i * 2)); // 50, 200, 800...
        }
    }

    // Pre-calculate projection matrices (these don't change between frames)
    m_cascadeProjectionMatrices.resize(m_numCascades);
    m_cascadeBiasScales.resize(m_numCascades);
    for (unsigned int i = 0; i < m_numCascades; ++i) {
        const double orthoSize{m_cascadeOrthoSizes[i]};

        // The light stands at the near plane, one caster reach back, and a receiver
        // runs one cascade radius past the camera and no further. So only the far
        // plane follows the cascade; the near plane holds the shared reach, which
        // is what lets a distant occluder shadow what stands right at the camera
        // even in the finest cascade.
        const double farPlane{m_casterReach + orthoSize};

        // Near and far handed over swapped: that is the reverse-Z mapping. Depth
        // counts from 0 at the far plane up to 1 at the light, landing the camera
        // end of the slab -- all the fine cascades care about -- where float32
        // still has its exponent, and its precision.
        m_cascadeProjectionMatrices[i] = glm::orthoRH_ZO(
            -orthoSize, orthoSize,
            -orthoSize, orthoSize,
            farPlane, 0.0
        );

        // Half a texel of world distance, expressed in the depth units of this
        // cascade's own slab.
        const double depthRange{farPlane};
        m_cascadeBiasScales[i] =
            static_cast<float>((orthoSize / static_cast<double>(width)) / depthRange);
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
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    // The sampler compares before it filters. Read as plain depth, a linear tap
    // averages four texels and compares once, against a depth no surface in the
    // scene holds; comparing first and averaging the four outcomes is what a
    // percentage-closer filter means. It also makes every tap a hardware 2x2, so
    // the 3x3 grid below spans 4x4 texels at the cost of the 3x3. Greater-or-
    // equal keeps the reverse-Z sense: the reference passes, and the texel reads
    // as lit, unless something stands deeper than it toward the light.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);
    
    // Border reads as the far plane, which under reverse-Z is zero: a lookup
    // that lands outside the map finds nothing standing between it and the light.
    float borderColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
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

void ShadowRenderer::cleanupShadowMap() {
    if (m_shadowMapInitialized) {
        glDeleteTextures(1, &m_shadowDepthTextureArray);
        glDeleteFramebuffers(1, &m_shadowMapFBO);
        m_shadowMapInitialized = false;
    }
}

void ShadowRenderer::beginShadowPass(const glm::dvec3& lightDir, const glm::dvec3& camForward,
                                    uint64_t frameNum) {
    if (!m_shadowMapInitialized) {
        throw std::runtime_error("Shadow map not initialized. Call setupShadowMap() first.");
    }

    // Calculate light position in L-space (camera-relative coordinates)
    glm::dvec3 lightDirNormalized = glm::normalize(lightDir);

    // A deterministic up vector, rerolled per frame, rolls the light basis about
    // its own axis so the square rim of a cascade never sweeps the same texels
    // twice. Any direction serves; only degeneracy has to be excluded.
    const glm::dvec3 roll{Hash::pcgUnit3(frameNum) - glm::dvec3{0.5}};
    const double rollLengthSquared{glm::dot(roll, roll)};
    glm::dvec3 up{0.0, 1.0, 0.0};
    if (rollLengthSquared > 0.0) {
        up = roll / glm::sqrt(rollLengthSquared);
    }

    // One light view per cascade, aimed at that cascade's own centre. Aiming at
    // the centre rather than at the camera keeps every distance the projection
    // measures taken from the same point, so near, far and the bias all hold.
    const glm::dvec3 forward{glm::normalize(camForward)};
    for (unsigned int i = 0; i < m_numCascades; ++i) {
        const glm::dvec3 centre{forward * (k_cascadePush * m_cascadeOrthoSizes[i])};
        const glm::dmat4 lightView{
            glm::lookAt(centre - lightDirNormalized * m_casterReach, centre, up)};
        m_lightSpaceMatrices[i] = m_cascadeProjectionMatrices[i] * lightView;
    }
    
    // Bind shadow map framebuffer for depth rendering
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowMapFBO);
    glViewport(0, 0, m_shadowMapWidth, m_shadowMapHeight);

    // Each cascade is cleared as bindCascadeLayer attaches it, so nothing is
    // cleared here: the framebuffer still holds the whole array as one layered
    // attachment, and a clear against that would cost every cascade at once.
    glEnable(GL_DEPTH_TEST);

    // Casters record the faces they turn to the light, so a shadow begins at the
    // surface that cast it rather than however far past that the caster ends. What
    // it asks in return is a bias wide enough that a lit surface does not read as
    // its own occluder.
    glEnable(GL_CULL_FACE);
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
    
    // Clears to the far plane, which reverse-Z puts at zero: an untouched texel
    // holds nothing between it and the light.
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
    // Only the cull face was this pass's to change; the depth convention is the
    // context's and the camera passes want it exactly as it stands.
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::pair<bool, std::string> ShadowRenderer::reloadShaders() {
    // ShadowRenderer currently uses depth shaders from MeshHandler and InstanceHandler
    // If it gets its own shaders in the future, reload them here
    return {true, "ShadowRenderer: No shaders to reload (uses external depth shaders)"};
}