// CdlodHandler.cpp
#include "CdlodHandler.h"
#include "CdlodBody.h"
#include "CdlodPatchGeometry.h"
#include "../SSBOManager.h"
#include "../InstanceFrameUniforms.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <glm/gtc/quaternion.hpp>

namespace {

constexpr const char* s_gbufferVertexPath =
    ENGINE_ASSET_DIR "/src/graphics/cdlod/cdlod_vertex_shader.vert";
constexpr const char* s_depthVertexPath =
    ENGINE_ASSET_DIR "/src/graphics/cdlod/cdlod_depth_vertex_shader.vert";
constexpr const char* s_gbufferFragmentPath =
    ENGINE_ASSET_DIR "/src/graphics/cdlod/cdlod_gbuffer_fragment_shader.frag";
constexpr const char* s_depthFragmentPath =
    ENGINE_ASSET_DIR "/src/graphics/shared_shaders/depth_fragment_shader.frag";
constexpr const char* s_defaultSurfacePath =
    ENGINE_ASSET_DIR "/src/graphics/cdlod/cdlod_default_surface.glsl";
constexpr const char* s_surfaceMarker = "__CDLOD_SURFACE_BODY__";

// CPU twin of the vertex-stage transform in shared_shaders/mesh_transform.glsl,
// run backwards: it rebuilds the interpolated pose the body will be drawn at
// this frame and puts the camera into that body's own frame. Selection has to
// measure against the pose the geometry actually lands at, so the two must stay
// in step; the forward direction lives in that shader.
//
// All double here, so unlike the shader this needs no Dekker split: subtracting
// two world positions of similar magnitude is exact enough on its own.
glm::dvec3 cameraInBodySpace(const MeshTransform& transform,
                             const FrameRenderParams& params) {
    // Signed, so a body stamped with a time ahead of the frame's extrapolates
    // backwards instead of wrapping into an enormous forward step.
    const double stepDelta{static_cast<double>(
        static_cast<int64_t>(params.time) - static_cast<int64_t>(transform.time))};
    const double deltaTime{stepDelta + params.timeRemainder};

    const glm::dvec3 bodyPosition{transform.position + transform.velocity * deltaTime};
    const glm::dquat spin{
        glm::angleAxis(transform.angVel * deltaTime, transform.angVelAxis)};
    const glm::dquat bodyOrientation{spin * transform.orientation};

    // Undoes the vertex stage's steps in reverse: world offset, then the
    // rotation about the centre of rotation, then the scale.
    const glm::dvec3 relative{params.camPos - bodyPosition - transform.centerOfRotation};
    const glm::dvec3 rotated{glm::conjugate(bodyOrientation) * relative};
    return (rotated + transform.centerOfRotation) / transform.scale;
}

}  // namespace

CdlodHandler::CdlodHandler(SSBOManager* ssboManager)
    : m_ssboManager{ssboManager} {
    if (!m_ssboManager) {
        throw std::runtime_error("SSBOManager cannot be null");
    }

    m_patchGeometry = std::make_unique<CdlodPatchGeometry>();

    // Surface 0 is the built-in sphere, so a body that asks for no particular
    // shape still has programs to draw with.
    createSurface();
}

std::string CdlodHandler::buildStageSource(const char* stagePath,
                                           const std::string& snippetPath) {
    // Expands the patch include, which is where the marker lives, so every
    // stage that includes it picks the snippet up without naming it.
    std::string source{ShaderProgram::loadTextFileFromPath(stagePath)};
    const std::string surfacePath{snippetPath.empty() ? s_defaultSurfacePath : snippetPath};
    const std::string body{ShaderProgram::loadTextFileFromPath(surfacePath)};

    const size_t markerPos{source.find(s_surfaceMarker)};
    if (markerPos == std::string::npos) {
        throw std::runtime_error("CDLOD stage is missing the surface marker");
    }
    source.replace(markerPos, std::char_traits<char>::length(s_surfaceMarker), body);
    return source;
}

void CdlodHandler::buildSurfacePrograms(Surface& surface) {
    // The snippet goes into all three stages that consult the surface: the two
    // vertex stages for where it is, the G-buffer fragment stage for how it
    // faces. The shadow pass shades nothing, so its fragment stage is shared.
    surface.gbufferProgram.loadVertexShader(
        buildStageSource(s_gbufferVertexPath, surface.snippetPath));
    surface.gbufferProgram.loadFragmentShader(
        buildStageSource(s_gbufferFragmentPath, surface.snippetPath));
    surface.gbufferProgram.linkShaders();

    surface.depthProgram.loadVertexShader(
        buildStageSource(s_depthVertexPath, surface.snippetPath));
    surface.depthProgram.loadFragmentShaderFromPath(s_depthFragmentPath);
    surface.depthProgram.linkShaders();
}

size_t CdlodHandler::createSurface(const std::string& snippetPath) {
    auto surface{std::make_unique<Surface>()};
    surface->snippetPath = snippetPath;

    buildSurfacePrograms(*surface);
    m_surfaces.push_back(std::move(surface));
    return m_surfaces.size() - 1;
}

CdlodHandler::~CdlodHandler() {
    // Bodies hold GL handles and must go before the shared patch geometry.
    m_bodies.clear();
    m_patchGeometry.reset();
}

size_t CdlodHandler::createBody(int ssboIndex, const CdlodConfig& config,
                                size_t surfaceIndex) {
    if (surfaceIndex >= m_surfaces.size()) {
        throw std::runtime_error("CdlodHandler::createBody: invalid surface index");
    }

    for (size_t ii{0}; ii < m_bodies.size(); ++ii) {
        if (!m_bodies[ii]) {
            m_bodies[ii] = std::make_unique<CdlodBody>(ssboIndex, config, surfaceIndex,
                                                       *m_patchGeometry);
            return ii;
        }
    }
    m_bodies.push_back(
        std::make_unique<CdlodBody>(ssboIndex, config, surfaceIndex, *m_patchGeometry));
    return m_bodies.size() - 1;
}

void CdlodHandler::removeBody(size_t bodyHandle) {
    if (bodyHandle >= m_bodies.size()) return;
    m_bodies[bodyHandle].reset();
}

void CdlodHandler::update(const FrameRenderParams& params) {
    for (const std::unique_ptr<CdlodBody>& body : m_bodies) {
        if (!body) continue;
        const MeshTransform& transform{
            m_ssboManager->getMeshTransform(body->getSsboIndex())};
        body->update(cameraInBodySpace(transform, params));
    }
}

void CdlodHandler::renderGeometry(const FrameRenderParams& params) {
    renderAllSurfaces(params, /*isGeometryPass=*/true);
}

void CdlodHandler::renderDepth(const FrameRenderParams& params) {
    renderAllSurfaces(params, /*isGeometryPass=*/false);
}

void CdlodHandler::renderAllSurfaces(const FrameRenderParams& params, bool isGeometryPass) {
    if (m_bodies.empty()) return;

    // Wireframe is a view of the surface, not of the shadow map: a wireframe
    // depth pass would punch holes in everything the body shadows. Set around
    // every surface rather than inside, so the mode is not toggled per program.
    const bool wireframe{m_wireframe && isGeometryPass};
    if (wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    // Each surface is its own program, so the bodies are drawn grouped by shape
    // rather than in creation order; one program bind per shape, not per body.
    for (size_t surfaceIndex{0}; surfaceIndex < m_surfaces.size(); ++surfaceIndex) {
        renderSurfaceBodies(*m_surfaces[surfaceIndex], surfaceIndex, params, isGeometryPass);
    }

    glBindVertexArray(0);

    if (wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

void CdlodHandler::renderSurfaceBodies(Surface& surface, size_t surfaceIndex,
                                       const FrameRenderParams& params, bool isGeometryPass) {
    const auto usesThisSurface = [&](const std::unique_ptr<CdlodBody>& body) {
        return body && body->getSurfaceIndex() == surfaceIndex &&
               body->getInstanceCount() > 0;
    };
    if (std::none_of(m_bodies.begin(), m_bodies.end(), usesThisSurface)) return;

    ShaderProgram& program{isGeometryPass ? surface.gbufferProgram : surface.depthProgram};
    program.use();
    const unsigned int activeProgram{program.getID()};

    // view, projection, u_time, u_timeRemainder and the Dekker-split camera,
    // shared with every other camera-relative instanced draw.
    setInstanceFrameUniforms(activeProgram, params);

    const GLint patchVerticesLoc{glGetUniformLocation(activeProgram, "u_patchVertices")};
    if (patchVerticesLoc != -1) {
        glUniform1i(patchVerticesLoc, CdlodPatchGeometry::k_patchVertices);
    }

    const GLint halfExtentLoc{glGetUniformLocation(activeProgram, "u_halfExtent")};
    const GLint rangeFactorLoc{glGetUniformLocation(activeProgram, "u_lodRangeFactor")};
    const GLint cameraBodyLoc{glGetUniformLocation(activeProgram, "u_cameraBodyPosition")};
    const GLint baseColorLoc{glGetUniformLocation(activeProgram, "u_baseColor")};
    const GLint colorByLevelLoc{glGetUniformLocation(activeProgram, "u_colorByLevel")};

    if (colorByLevelLoc != -1) {
        glUniform1i(colorByLevelLoc, m_wireframe ? 1 : 0);
    }

    for (const std::unique_ptr<CdlodBody>& body : m_bodies) {
        if (!usesThisSurface(body)) continue;

        const CdlodConfig& config{body->getConfig()};
        if (halfExtentLoc != -1) {
            glUniform1f(halfExtentLoc, static_cast<float>(config.m_halfExtent));
        }
        if (rangeFactorLoc != -1) {
            glUniform1f(rangeFactorLoc, static_cast<float>(config.m_lodRangeFactor));
        }
        if (cameraBodyLoc != -1) {
            const glm::vec3 cameraBody{body->getCameraBodyPosition()};
            glUniform3fv(cameraBodyLoc, 1, &cameraBody[0]);
        }
        if (baseColorLoc != -1) {
            const glm::vec4 baseColor{config.m_baseColor};
            glUniform4fv(baseColorLoc, 1, &baseColor[0]);
        }

        glBindVertexArray(body->getVAO());
        glDrawElementsInstanced(GL_TRIANGLES, m_patchGeometry->getIndexCount(),
                                GL_UNSIGNED_SHORT, 0, body->getInstanceCount());
    }
}

std::pair<bool, std::string> CdlodHandler::reloadShaders() {
    std::string allErrors;
    bool allSuccess{true};

    // Rebuilt from source rather than delegated to ShaderProgram::reloadShaders:
    // the vertex stages are compiled from a spliced string and so carry no path
    // to reload from. Each surface is rebuilt into a fresh one, so a snippet
    // that fails to compile leaves the working programs in place.
    for (std::unique_ptr<Surface>& surfacePtr : m_surfaces) {
        auto fresh{std::make_unique<Surface>()};
        fresh->snippetPath = surfacePtr->snippetPath;
        try {
            buildSurfacePrograms(*fresh);
            surfacePtr = std::move(fresh);
        } catch (const std::exception& e) {
            allSuccess = false;
            allErrors += std::string("CDLOD surface reload failed: ") + e.what() + "\n";
        }
    }

    return {allSuccess,
            allSuccess ? "All CdlodHandler shaders reloaded successfully" : allErrors};
}
