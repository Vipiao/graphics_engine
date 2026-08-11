// CdlodHandler.cpp
#include "CdlodHandler.h"
#include "CdlodPatchGeometry.h"
#include "../SSBOManager.h"
#include "../InstanceFrameUniforms.h"
#include <algorithm>
#include <cstddef>
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

void spliceSurfaceBody(std::string& source, const std::string& body) {
    const size_t markerPos{source.find(s_surfaceMarker)};
    if (markerPos == std::string::npos) {
        throw std::runtime_error("CDLOD stage is missing the surface marker");
    }
    source.replace(markerPos, std::char_traits<char>::length(s_surfaceMarker), body);
}

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

CdlodInstanceData makeInstanceData(const CdlodInstance& instance) {
    return CdlodInstanceData{glm::vec4{instance.m_config.m_baseColor},
                             glm::vec4{glm::vec3{instance.m_cameraBodyPosition}, 0.0f},
                             static_cast<float>(instance.m_config.m_halfExtent),
                             static_cast<float>(instance.m_config.m_lodRangeFactor),
                             instance.m_ssboIndex,
                             0};
}

// Rewrites a whole buffer, growing its storage geometrically. Everything CDLOD
// uploads is rebuilt from scratch every frame, so nothing is ever patched. The
// reallocation doubles as orphaning: the previous frame's draw may still be
// reading the old storage, and discarding it lets the driver hand back fresh
// memory instead of waiting for that draw to retire.
void uploadDynamicBuffer(GLenum target, GLuint buffer, size_t elementSize, size_t count,
                         const void* data, size_t& capacity) {
    if (count == 0) return;
    if (count > capacity) {
        capacity = std::max(count, capacity * 2);
    }

    glBindBuffer(target, buffer);
    glBufferData(target, capacity * elementSize, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(target, 0, count * elementSize, data);
    glBindBuffer(target, 0);
}

}  // namespace

CdlodSurface::CdlodSurface(GLuint patchIndexBuffer) {
    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    // The shared index buffer is the whole of the per-vertex input: the vertex
    // stage reconstructs the grid coordinate from gl_VertexID.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, patchIndexBuffer);

    glGenBuffers(1, &m_patchVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_patchVBO);

    // Locations start at 4, leaving 0..3 free so the layout stays readable
    // against the instanced-geometry shaders.
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(CdlodPatch),
                          (void*)offsetof(CdlodPatch, m_centre));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(CdlodPatch),
                          (void*)offsetof(CdlodPatch, m_uAxis));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(CdlodPatch),
                          (void*)offsetof(CdlodPatch, m_vAxis));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    glVertexAttribIPointer(7, 1, GL_INT, sizeof(CdlodPatch),
                           (void*)offsetof(CdlodPatch, m_level));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);

    glVertexAttribIPointer(8, 1, GL_INT, sizeof(CdlodPatch),
                           (void*)offsetof(CdlodPatch, m_instanceIndex));
    glEnableVertexAttribArray(8);
    glVertexAttribDivisor(8, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &m_instanceDataBuffer);
}

CdlodSurface::~CdlodSurface() {
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_patchVBO != 0) glDeleteBuffers(1, &m_patchVBO);
    if (m_instanceDataBuffer != 0) glDeleteBuffers(1, &m_instanceDataBuffer);
}

CdlodHandler::CdlodHandler(SSBOManager* ssboManager)
    : m_ssboManager{ssboManager} {
    if (!m_ssboManager) {
        throw std::runtime_error("SSBOManager cannot be null");
    }

    m_patchGeometry = std::make_unique<CdlodPatchGeometry>();

    // The built-in sphere, so an instance that asks for no particular shape
    // still has programs to draw with.
    m_defaultSurface = createSurface();
}

CdlodHandler::~CdlodHandler() {
    // Surfaces hold GL handles into the shared patch geometry and must go first.
    m_surfaces.clear();
    m_patchGeometry.reset();
}

std::string CdlodHandler::buildStageSource(const char* stagePath,
                                           const std::string& snippetPath) {
    // Expands the patch include, which is where the marker lives, so every stage
    // that includes it picks the snippet up without naming it.
    std::string source{ShaderProgram::loadTextFileFromPath(stagePath)};
    const std::string surfacePath{snippetPath.empty() ? s_defaultSurfacePath : snippetPath};

    spliceSurfaceBody(source, ShaderProgram::loadTextFileFromPath(surfacePath));
    return source;
}

void CdlodHandler::buildSurfacePrograms(CdlodSurface& surface) {
    // Compiled into fresh programs first: every load throws on a compile error,
    // so a snippet that does not build leaves the surface's own programs alone.
    //
    // The snippet goes into all three stages that consult the surface: the two
    // vertex stages for where it is, the G-buffer fragment stage for how it
    // faces. The shadow pass shades nothing, so its fragment stage is shared.
    auto gbufferProgram{std::make_unique<ShaderProgram>()};
    gbufferProgram->loadVertexShader(
        buildStageSource(s_gbufferVertexPath, surface.m_snippetPath));
    gbufferProgram->loadFragmentShader(
        buildStageSource(s_gbufferFragmentPath, surface.m_snippetPath));
    gbufferProgram->linkShaders();

    auto depthProgram{std::make_unique<ShaderProgram>()};
    depthProgram->loadVertexShader(
        buildStageSource(s_depthVertexPath, surface.m_snippetPath));
    depthProgram->loadFragmentShaderFromPath(s_depthFragmentPath);
    depthProgram->linkShaders();

    // Installed in place, so the instances this surface owns pick the new
    // programs up rather than staying with the ones they were created under.
    surface.m_gbufferProgram = std::move(gbufferProgram);
    surface.m_depthProgram = std::move(depthProgram);
}

std::weak_ptr<CdlodSurface> CdlodHandler::createSurface(const std::string& snippetPath) {
    auto surface{std::make_shared<CdlodSurface>(m_patchGeometry->getIndexBuffer())};
    surface->m_snippetPath = snippetPath;

    buildSurfacePrograms(*surface);
    m_surfaces.push_back(std::move(surface));
    return m_surfaces.back();
}

void CdlodHandler::removeSurface(std::weak_ptr<CdlodSurface> surfaceWeak) {
    const std::shared_ptr<CdlodSurface> surface{surfaceWeak.lock()};
    if (!surface) return;

    for (auto it{m_surfaces.begin()}; it != m_surfaces.end(); ++it) {
        if (*it == surface) {
            m_surfaces.erase(it);
            return;
        }
    }
}

std::weak_ptr<CdlodInstance> CdlodHandler::createInstance(
    int ssboIndex, const CdlodConfig& config, std::weak_ptr<CdlodSurface> surfaceWeak) {
    const std::shared_ptr<CdlodSurface> surface{surfaceWeak.lock()};
    if (!surface) {
        throw std::runtime_error("CdlodHandler::createInstance: surface has expired");
    }

    surface->m_instances.push_back(
        std::make_shared<CdlodInstance>(ssboIndex, config, surface.get()));
    return surface->m_instances.back();
}

void CdlodHandler::removeInstance(std::weak_ptr<CdlodInstance> instanceWeak) {
    const std::shared_ptr<CdlodInstance> instance{instanceWeak.lock()};
    if (!instance || !instance->m_surface) return;

    std::vector<std::shared_ptr<CdlodInstance>>& instances{instance->m_surface->m_instances};
    for (auto it{instances.begin()}; it != instances.end(); ++it) {
        if (*it == instance) {
            // Erasing renumbers the instances after this one, which is why the
            // index the patches carry is assigned per frame rather than kept.
            instances.erase(it);
            return;
        }
    }
}

void CdlodHandler::update(const FrameRenderParams& params) {
    for (const std::shared_ptr<CdlodSurface>& surface : m_surfaces) {
        selectVisibleNodes(*surface, params);
        uploadSelection(*surface);
    }
}

void CdlodHandler::selectVisibleNodes(CdlodSurface& surface,
                                      const FrameRenderParams& params) {
    surface.m_selectedPatches.clear();
    surface.m_instanceData.assign(surface.m_instances.size(), CdlodInstanceData{});

    // Every instance appends its own leaves to the one buffer this surface
    // draws from, and is stamped with where its parameters sit alongside them.
    for (size_t instanceIndex{0}; instanceIndex < surface.m_instances.size();
         ++instanceIndex) {
        CdlodInstance& instance{*surface.m_instances[instanceIndex]};

        const MeshTransform& transform{
            m_ssboManager->getMeshTransform(instance.m_ssboIndex)};
        instance.m_cameraBodyPosition = cameraInBodySpace(transform, params);

        // The tree describes patches, not who owns them, so the patches it just
        // appended are stamped here with where this instance's parameters sit.
        const size_t firstPatch{surface.m_selectedPatches.size()};
        instance.m_tree.updateAndSelect(instance.m_cameraBodyPosition,
                                        surface.m_selectedPatches);
        for (size_t patch{firstPatch}; patch < surface.m_selectedPatches.size(); ++patch) {
            surface.m_selectedPatches[patch].m_instanceIndex =
                static_cast<int32_t>(instanceIndex);
        }

        surface.m_instanceData[instanceIndex] = makeInstanceData(instance);
    }
}

void CdlodHandler::uploadSelection(CdlodSurface& surface) {
    uploadDynamicBuffer(GL_ARRAY_BUFFER, surface.m_patchVBO,
                        sizeof(CdlodPatch), surface.m_selectedPatches.size(),
                        surface.m_selectedPatches.data(), surface.m_patchCapacity);
    uploadDynamicBuffer(GL_SHADER_STORAGE_BUFFER, surface.m_instanceDataBuffer,
                        sizeof(CdlodInstanceData), surface.m_instanceData.size(),
                        surface.m_instanceData.data(), surface.m_instanceDataCapacity);
}

void CdlodHandler::renderGeometry(const FrameRenderParams& params) {
    renderAllSurfaces(params, /*isGeometryPass=*/true);
}

void CdlodHandler::renderDepth(const FrameRenderParams& params) {
    renderAllSurfaces(params, /*isGeometryPass=*/false);
}

void CdlodHandler::renderAllSurfaces(const FrameRenderParams& params, bool isGeometryPass) {
    // Wireframe is a view of the surface, not of the shadow map: a wireframe
    // depth pass would punch holes in everything the body shadows. Set around
    // every surface rather than inside, so the mode is not toggled per program.
    const bool wireframe{m_wireframe && isGeometryPass};
    if (wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    // One program bind and one draw per surface, however many instances wear it.
    for (const std::shared_ptr<CdlodSurface>& surface : m_surfaces) {
        if (surface->m_selectedPatches.empty()) continue;

        ShaderProgram& program{isGeometryPass ? *surface->m_gbufferProgram
                                              : *surface->m_depthProgram};
        program.use();
        const unsigned int activeProgram{program.getID()};

        // view, projection, u_time, u_timeRemainder and the Dekker-split camera,
        // shared with every other camera-relative instanced draw.
        setInstanceFrameUniforms(activeProgram, params);

        const GLint patchVerticesLoc{glGetUniformLocation(activeProgram, "u_patchVertices")};
        if (patchVerticesLoc != -1) {
            glUniform1i(patchVerticesLoc, CdlodPatchGeometry::k_patchVertices);
        }
        const GLint colorByLevelLoc{glGetUniformLocation(activeProgram, "u_colorByLevel")};
        if (colorByLevelLoc != -1) {
            glUniform1i(colorByLevelLoc, m_wireframe ? 1 : 0);
        }

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, k_instanceDataBinding,
                         surface->m_instanceDataBuffer);
        glBindVertexArray(surface->m_VAO);
        glDrawElementsInstanced(GL_TRIANGLES, m_patchGeometry->getIndexCount(),
                                GL_UNSIGNED_SHORT, 0,
                                static_cast<GLsizei>(surface->m_selectedPatches.size()));
    }

    glBindVertexArray(0);

    if (wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

std::pair<bool, std::string> CdlodHandler::reloadShaders() {
    std::string allErrors;
    bool allSuccess{true};

    // Rebuilt from source rather than delegated to ShaderProgram::reloadShaders:
    // the vertex stages are compiled from a spliced string and so carry no path
    // to reload from.
    for (const std::shared_ptr<CdlodSurface>& surface : m_surfaces) {
        try {
            buildSurfacePrograms(*surface);
        } catch (const std::exception& e) {
            allSuccess = false;
            allErrors += std::string("CDLOD surface reload failed: ") + e.what() + "\n";
        }
    }

    return {allSuccess,
            allSuccess ? "All CdlodHandler shaders reloaded successfully" : allErrors};
}
