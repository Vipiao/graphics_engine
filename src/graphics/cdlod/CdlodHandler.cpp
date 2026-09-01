// CdlodHandler.cpp
#include "CdlodHandler.h"
#include "CdlodPatchGeometry.h"
#include "../SSBOManager.h"
#include "../TextureStore.h"
#include "../InstanceFrameUniforms.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>
#include "math/DekkerArithmetic.h"

namespace {

constexpr const char* s_gbufferVertexPath =
    ENGINE_ASSET_DIR "/src/graphics/cdlod/cdlod_vertex_shader.vert";
constexpr const char* s_depthVertexPath =
    ENGINE_ASSET_DIR "/src/graphics/cdlod/cdlod_depth_vertex_shader.vert";
constexpr const char* s_gbufferFragmentPath =
    ENGINE_ASSET_DIR "/src/graphics/cdlod/cdlod_gbuffer_fragment_shader.frag";
constexpr const char* s_depthFragmentPath =
    ENGINE_ASSET_DIR "/src/graphics/shared_shaders/depth_fragment_shader.frag";
constexpr const char* s_surfaceMarker = "__CDLOD_SURFACE_BODY__";

void spliceSurfaceBody(std::string& source, const std::string& body) {
    const size_t markerPos{source.find(s_surfaceMarker)};
    if (markerPos == std::string::npos) {
        throw std::runtime_error("CDLOD stage is missing the surface marker");
    }
    source.replace(markerPos, std::char_traits<char>::length(s_surfaceMarker), body);
}

// Rebuilds the interpolated pose the body will be drawn at this frame and puts
// the camera into that body's own frame. Selection has to measure against the
// pose the geometry actually lands at, and the vertex stage has to undo exactly
// the placement made here, so this is where both are decided rather than a
// duplicate of a decision made in the shader.
//
// All double here, so unlike the mesh stages this needs no Dekker split:
// subtracting two world positions of similar magnitude is exact enough on its
// own. What is narrowed is the rotation, and deliberately: it is inverted at the
// width it will be uploaded at, so the shader's forward rotation and this
// backward one are true inverses. Inverting the exact rotation instead would
// leave them a part in ten million apart, and a part in ten million of the
// camera's distance from a planet's centre is most of a metre.
CdlodBodyPose bodyRenderPose(const MeshTransform& transform,
                             const FrameRenderParams& params) {
    // Signed, so a body stamped with a time ahead of the frame's extrapolates
    // backwards instead of wrapping into an enormous forward step.
    const double stepDelta{static_cast<double>(
        static_cast<int64_t>(params.time) - static_cast<int64_t>(transform.time))};
    const double deltaTime{stepDelta + params.timeRemainder};

    const glm::dvec3 bodyPosition{transform.position + transform.velocity * deltaTime};
    const glm::dquat spin{
        glm::angleAxis(transform.angVel * deltaTime, transform.angVelAxis)};
    const glm::dmat3 bodyRotation{glm::mat3{glm::mat3_cast(spin * transform.orientation)}};

    // Scale comes back from float too: the vertex stage reads the copy in the
    // shared SSBO, and a scale that differed in its last bit would tilt the
    // whole cancellation by that much of the body's radius.
    const glm::dvec3 scale{glm::vec3{transform.scale}};

    // Undoes the vertex stage's steps in reverse: world offset, then the
    // rotation about the centre of rotation, then the scale. The centre of
    // rotation appears on both sides of that stage and cancels, so its own width
    // never enters.
    const glm::dvec3 relative{params.camPos - bodyPosition - transform.centerOfRotation};
    // A true inverse, not a transpose: narrowing to float leaves the rotation a
    // part in ten million off orthonormal, worth half a metre of camera position
    // at a planet's radius.
    const glm::dmat3 inverseBodyRotation{glm::inverse(bodyRotation)};
    const glm::dvec3 rotated{inverseBodyRotation * relative};

    return CdlodBodyPose{bodyRotation, (rotated + transform.centerOfRotation) / scale,
                         inverseBodyRotation, scale};
}

// Splits a body-sized value into the float pair the vertex stage reads as one
// wide float. The same split every other camera-relative buffer in this renderer
// is filled with, and the same one shared_shaders/dekker_arithmetic.glsl adds
// back on the far side.
std::pair<glm::vec3, glm::vec3> splitWide(const glm::dvec3& value) {
    using DekkerFloat = DekkerArithmetic<float>;
    const DekkerFloat::DekkerNumber x{value.x};
    const DekkerFloat::DekkerNumber y{value.y};
    const DekkerFloat::DekkerNumber z{value.z};

    return {glm::vec3{x.main, y.main, z.main}, glm::vec3{x.error, y.error, z.error}};
}

// How much a patch's bounding sphere is widened before it is measured against a
// caster volume. A body bounds its patches on the shape they stand on rather than
// the shape they are drawn as, so terrain wandering across a patch can leave its
// own ball; the margin is proportional because that wandering scales with the
// patch. Too small drops a caster and loses a shadow the cascade fade will not
// cover -- that band blends receivers, and the loss is up-light of them. Too large
// only draws patches that clip away.
constexpr double k_casterBoundsMargin{1.5};

// A caster volume as the body's own frame sees it. Four of these against thousands
// of patches, so it is the volumes that are carried across rather than the bounds.
Cylinder volumeInBodySpace(const Cylinder& volume, const CdlodBodyPose& pose) {
    assert(glm::all(glm::epsilonEqual(pose.m_scale, glm::dvec3{pose.m_scale.x}, 1e-12)) &&
           "Non-uniform scale: a bounding sphere would not stay one in the body's frame");
    const double scale{pose.m_scale.x};

    return Cylinder{glm::normalize(pose.m_inverseBodyRotation * volume.m_axis),
                    pose.m_cameraBodyPosition +
                        (pose.m_inverseBodyRotation * volume.m_centre) / scale,
                    volume.m_radius / scale, volume.m_axialMin / scale,
                    volume.m_axialMax / scale};
}

// The innermost volume a patch can cast into, or one past the last if it casts into
// none. They only nearly nest -- a shared caster reach runs from centres that do not
// coincide -- so every volume is tested, not just the outermost.
unsigned int casterTier(const std::vector<Cylinder>& bodyVolumes, const glm::dvec3& centre,
                        double radius) {
    for (size_t tier{0}; tier < bodyVolumes.size(); ++tier) {
        if (intersectsSphere(bodyVolumes[tier], centre, radius)) {
            return static_cast<unsigned int>(tier);
        }
    }
    return static_cast<unsigned int>(bodyVolumes.size());
}

// A tree's leaf as the vertex stages read it. The narrowing to float happens
// here rather than in the tree, which works in double throughout and has no
// business knowing what an attribute can hold.
CdlodPatch makePatch(const CdlodLeaf& leaf, int32_t instanceIndex) {
    const auto [centreHigh, centreLow] = splitWide(leaf.m_frame.m_centre);

    return CdlodPatch{centreHigh,   centreLow,     glm::vec3{leaf.m_frame.m_uAxis},
                      glm::vec3{leaf.m_frame.m_vAxis}, leaf.m_level, instanceIndex,
                      static_cast<float>(leaf.m_frameScale)};
}

// A mat3 in the column-per-vec4 shape std430 gives it, so the struct's own
// layout matches what the shader reads without a rule about padding.
glm::mat3x4 paddedColumns(const glm::dmat3& rotation) {
    const glm::mat3 narrow{rotation};
    return glm::mat3x4{glm::vec4{narrow[0], 0.0f}, glm::vec4{narrow[1], 0.0f},
                       glm::vec4{narrow[2], 0.0f}};
}

CdlodInstanceData makeInstanceData(const CdlodInstance& instance) {
    const auto [cameraHigh, cameraLow] = splitWide(instance.m_pose.m_cameraBodyPosition);

    return CdlodInstanceData{glm::vec4{instance.m_config.m_baseColor},
                             glm::vec4{cameraHigh, 0.0f},
                             glm::vec4{cameraLow, 0.0f},
                             paddedColumns(instance.m_pose.m_bodyRotation),
                             static_cast<float>(instance.m_config.m_lodRangeFactor),
                             instance.m_ssboIndex,
                             {0, 0}};
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
                          (void*)offsetof(CdlodPatch, m_centreHigh));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(CdlodPatch),
                          (void*)offsetof(CdlodPatch, m_centreLow));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(CdlodPatch),
                          (void*)offsetof(CdlodPatch, m_uAxis));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(CdlodPatch),
                          (void*)offsetof(CdlodPatch, m_vAxis));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);

    glVertexAttribIPointer(8, 1, GL_INT, sizeof(CdlodPatch),
                           (void*)offsetof(CdlodPatch, m_level));
    glEnableVertexAttribArray(8);
    glVertexAttribDivisor(8, 1);

    glVertexAttribIPointer(9, 1, GL_INT, sizeof(CdlodPatch),
                           (void*)offsetof(CdlodPatch, m_instanceIndex));
    glEnableVertexAttribArray(9);
    glVertexAttribDivisor(9, 1);

    glVertexAttribPointer(10, 1, GL_FLOAT, GL_FALSE, sizeof(CdlodPatch),
                          (void*)offsetof(CdlodPatch, m_frameScale));
    glEnableVertexAttribArray(10);
    glVertexAttribDivisor(10, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &m_instanceDataBuffer);
}

CdlodSurface::~CdlodSurface() {
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_patchVBO != 0) glDeleteBuffers(1, &m_patchVBO);
    if (m_instanceDataBuffer != 0) glDeleteBuffers(1, &m_instanceDataBuffer);
}

CdlodHandler::CdlodHandler(SSBOManager* ssboManager, TextureStore* textureStore)
    : m_ssboManager{ssboManager}, m_textureStore{textureStore} {
    if (!m_ssboManager) {
        throw std::runtime_error("SSBOManager cannot be null");
    }
    if (!m_textureStore) {
        throw std::runtime_error("TextureStore cannot be null");
    }

    m_patchGeometry = std::make_unique<CdlodPatchGeometry>();
}

CdlodHandler::~CdlodHandler() {
    // Surfaces hold GL handles into the shared patch geometry and must go first.
    m_surfaces.clear();
    m_patchGeometry.reset();
}

void CdlodHandler::setPatchQuads(int patchQuads) {
    m_patchGeometry->setPatchQuads(patchQuads);
}

int CdlodHandler::getPatchQuads() const {
    return m_patchGeometry->getPatchQuads();
}

std::string CdlodHandler::buildStageSource(const char* stagePath,
                                           const std::string& snippetPath) {
    // Expands the patch include, which is where the marker lives, so every stage
    // that includes it picks the snippet up without naming it.
    std::string source{ShaderProgram::loadTextFileFromPath(stagePath)};
    spliceSurfaceBody(source, ShaderProgram::loadTextFileFromPath(snippetPath));
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

void CdlodHandler::setSurfaceTexture(std::weak_ptr<CdlodSurface> surfaceWeak,
                                     const std::string& samplerName,
                                     const TextureSpec& spec) {
    const std::shared_ptr<CdlodSurface> surface{surfaceWeak.lock()};
    if (!surface) {
        throw std::runtime_error("CdlodHandler::setSurfaceTexture: surface has expired");
    }

    adoptSurfaceTexture(*surface, samplerName, m_textureStore->create(spec));
}

void CdlodHandler::setSurfaceCubeTexture(std::weak_ptr<CdlodSurface> surfaceWeak,
                                         const std::string& samplerName,
                                         const CubeTextureSpec& spec) {
    const std::shared_ptr<CdlodSurface> surface{surfaceWeak.lock()};
    if (!surface) {
        throw std::runtime_error("CdlodHandler::setSurfaceCubeTexture: surface has expired");
    }

    adoptSurfaceTexture(*surface, samplerName, m_textureStore->createCube(spec));
}

void CdlodHandler::adoptSurfaceTexture(CdlodSurface& surface,
                                       const std::string& samplerName,
                                       std::weak_ptr<Texture> texture) {
    for (CdlodSurfaceTexture& existing : surface.m_textures) {
        if (existing.m_samplerName == samplerName) {
            // Keeps the unit, so replacing a texture cannot renumber the ones
            // set after it. The old one goes back to the store rather than
            // lingering unreferenced.
            m_textureStore->remove(existing.m_texture);
            existing.m_texture = std::move(texture);
            return;
        }
    }

    // Units are handed out in order and never reused: nothing else binds a
    // texture during a CDLOD draw, so this surface has all of them to itself.
    surface.m_textures.push_back(CdlodSurfaceTexture{
        samplerName, std::move(texture), static_cast<int>(surface.m_textures.size())});
}

void CdlodHandler::setSurfaceUniform(std::weak_ptr<CdlodSurface> surfaceWeak,
                                     const std::string& name, float value) {
    const std::shared_ptr<CdlodSurface> surface{surfaceWeak.lock()};
    if (!surface) {
        throw std::runtime_error("CdlodHandler::setSurfaceUniform: surface has expired");
    }

    for (CdlodSurfaceUniform& existing : surface->m_uniforms) {
        if (existing.m_name == name) {
            existing.m_value = value;
            return;
        }
    }
    surface->m_uniforms.push_back(CdlodSurfaceUniform{name, value});
}

void CdlodHandler::removeSurface(std::weak_ptr<CdlodSurface> surfaceWeak) {
    const std::shared_ptr<CdlodSurface> surface{surfaceWeak.lock()};
    if (!surface) return;

    for (auto it{m_surfaces.begin()}; it != m_surfaces.end(); ++it) {
        if (*it == surface) {
            // The store owns what the snippet sampled, so hand it back rather
            // than leaving it loaded for a surface that no longer exists.
            for (const CdlodSurfaceTexture& texture : surface->m_textures) {
                m_textureStore->remove(texture.m_texture);
            }
            m_surfaces.erase(it);
            return;
        }
    }
}

std::weak_ptr<CdlodInstance> CdlodHandler::createInstance(
    int ssboIndex, const CdlodConfig& config, std::vector<CdlodPatchFrame> rootFrames,
    std::shared_ptr<const ICdlodPatchBounds> bounds,
    std::weak_ptr<CdlodSurface> surfaceWeak) {
    const std::shared_ptr<CdlodSurface> surface{surfaceWeak.lock()};
    if (!surface) {
        throw std::runtime_error("CdlodHandler::createInstance: surface has expired");
    }
    if (!bounds) {
        throw std::runtime_error("CdlodHandler::createInstance: bounds cannot be null");
    }

    surface->m_instances.push_back(std::make_shared<CdlodInstance>(
        ssboIndex, config, std::move(rootFrames), std::move(bounds), surface.get()));
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

void CdlodHandler::update(const FrameRenderParams& params,
                          const std::vector<Cylinder>& casterVolumes) {
    for (const std::shared_ptr<CdlodSurface>& surface : m_surfaces) {
        selectVisibleNodes(*surface, params, casterVolumes);
        sortPatchesByTier(*surface, casterVolumes.size());
        uploadSelection(*surface);
    }
}

void CdlodHandler::selectVisibleNodes(CdlodSurface& surface, const FrameRenderParams& params,
                                      const std::vector<Cylinder>& casterVolumes) {
    surface.m_selectedLeaves.clear();
    surface.m_selectedPatches.clear();
    surface.m_patchTiers.clear();
    surface.m_instanceData.assign(surface.m_instances.size(), CdlodInstanceData{});

    // Every instance appends its own leaves to the one buffer this surface
    // draws from, and is stamped with where its parameters sit alongside them.
    for (size_t instanceIndex{0}; instanceIndex < surface.m_instances.size();
         ++instanceIndex) {
        CdlodInstance& instance{*surface.m_instances[instanceIndex]};

        const MeshTransform& transform{
            m_ssboManager->getMeshTransform(instance.m_ssboIndex)};
        instance.m_pose = bodyRenderPose(transform, params);

        // Into the body's frame once rather than per patch: the frame the tree
        // already works in.
        m_bodyCylinders.clear();
        for (const Cylinder& cylinder : casterVolumes) {
            m_bodyCylinders.push_back(volumeInBodySpace(cylinder, instance.m_pose));
        }

        // The tree describes leaves, not who owns them: the ones it appended are
        // stamped here with this instance's parameter slot and its caster tier.
        const size_t firstLeaf{surface.m_selectedLeaves.size()};
        instance.m_tree.updateAndSelect(instance.m_pose.m_cameraBodyPosition,
                                        surface.m_selectedLeaves);
        for (size_t leaf{firstLeaf}; leaf < surface.m_selectedLeaves.size(); ++leaf) {
            const CdlodLeaf& selected{surface.m_selectedLeaves[leaf]};
            surface.m_selectedPatches.push_back(
                makePatch(selected, static_cast<int32_t>(instanceIndex)));
            surface.m_patchTiers.push_back(
                casterTier(m_bodyCylinders, selected.m_boundsCentre,
                           selected.m_boundsRadius * k_casterBoundsMargin));
        }

        surface.m_instanceData[instanceIndex] = makeInstanceData(instance);
    }
}

void CdlodHandler::sortPatchesByTier(CdlodSurface& surface, size_t tierCount) {
    // One tier per volume, plus the one holding what no volume reaches.
    const size_t tiers{tierCount + 1};

    m_tierCursor.assign(tiers, 0);
    for (unsigned int tier : surface.m_patchTiers) {
        ++m_tierCursor[tier];
    }

    // The running sum leaves the cursor at each tier's block start and m_tierPrefix
    // at its end: the patch count a draw of that tier asks for.
    surface.m_tierPrefix.assign(tiers, 0);
    size_t start{0};
    for (size_t tier{0}; tier < tiers; ++tier) {
        const size_t count{m_tierCursor[tier]};
        m_tierCursor[tier] = start;
        start += count;
        surface.m_tierPrefix[tier] = start;
    }

    surface.m_sortedPatches.resize(surface.m_selectedPatches.size());
    for (size_t patch{0}; patch < surface.m_selectedPatches.size(); ++patch) {
        surface.m_sortedPatches[m_tierCursor[surface.m_patchTiers[patch]]++] =
            surface.m_selectedPatches[patch];
    }
    surface.m_selectedPatches.swap(surface.m_sortedPatches);
}

void CdlodHandler::uploadSelection(CdlodSurface& surface) {
    uploadDynamicBuffer(GL_ARRAY_BUFFER, surface.m_patchVBO,
                        sizeof(CdlodPatch), surface.m_selectedPatches.size(),
                        surface.m_selectedPatches.data(), surface.m_patchCapacity);
    uploadDynamicBuffer(GL_SHADER_STORAGE_BUFFER, surface.m_instanceDataBuffer,
                        sizeof(CdlodInstanceData), surface.m_instanceData.size(),
                        surface.m_instanceData.data(), surface.m_instanceDataCapacity);
}

void CdlodHandler::applySurfaceInputs(const CdlodSurface& surface, unsigned int program) const {
    // A stage that does not mention a name has no location for it, which is the
    // ordinary case rather than an error: the depth pass reads whatever the
    // snippet's placement needs and nothing its shading needs.
    for (const CdlodSurfaceTexture& texture : surface.m_textures) {
        const GLint location{glGetUniformLocation(program, texture.m_samplerName.c_str())};
        if (location == -1) continue;

        const std::shared_ptr<Texture> bound{texture.m_texture.lock()};
        if (!bound) continue;

        m_textureStore->bindTexture(texture.m_unit, *bound);
        glUniform1i(location, texture.m_unit);
    }

    for (const CdlodSurfaceUniform& uniform : surface.m_uniforms) {
        const GLint location{glGetUniformLocation(program, uniform.m_name.c_str())};
        if (location != -1) glUniform1f(location, uniform.m_value);
    }
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

    // The passes between this one and the last bind their own targets over these
    // units, so what the store remembers about them no longer holds.
    m_textureStore->invalidateBindings();

    // One program bind and one draw per surface, however many instances wear it.
    for (const std::shared_ptr<CdlodSurface>& surface : m_surfaces) {
        assert(!surface->m_tierPrefix.empty() && "Surface drawn before update grouped it");
        // Only the sentinel may sit past the last tier. A real one that does means
        // the pass is indexing a grouping built from a different set of volumes,
        // which the clamp below would otherwise hide as a full draw.
        assert((params.casterTier == ~0u || params.casterTier < surface->m_tierPrefix.size())
               && "Caster tier past the grouping: cascades and caster volumes disagree");

        // A tier past the last, an ungrouped pass among them, draws every patch.
        const size_t tier{
            std::min<size_t>(params.casterTier, surface->m_tierPrefix.size() - 1)};
        const GLsizei patchCount{static_cast<GLsizei>(surface->m_tierPrefix[tier])};
        if (patchCount == 0) continue;

        ShaderProgram& program{isGeometryPass ? *surface->m_gbufferProgram
                                              : *surface->m_depthProgram};
        program.use();
        const unsigned int activeProgram{program.getID()};

        // Shared with every other camera-relative instanced draw, though these
        // stages take only view and projection from it: a CDLOD vertex arrives
        // camera-relative already, placed against the pose selection measured
        // with, so the timing and the split camera have nothing left to do here.
        setInstanceFrameUniforms(activeProgram, params);

        const GLint patchVerticesLoc{glGetUniformLocation(activeProgram, "u_patchVertices")};
        if (patchVerticesLoc != -1) {
            glUniform1i(patchVerticesLoc, m_patchGeometry->getPatchVertices());
        }
        const GLint colorByLevelLoc{glGetUniformLocation(activeProgram, "u_colorByLevel")};
        if (colorByLevelLoc != -1) {
            glUniform1i(colorByLevelLoc, m_wireframe ? 1 : 0);
        }

        applySurfaceInputs(*surface, activeProgram);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, k_instanceDataBinding,
                         surface->m_instanceDataBuffer);
        glBindVertexArray(surface->m_VAO);
        glDrawElementsInstanced(GL_TRIANGLES, m_patchGeometry->getIndexCount(),
                                GL_UNSIGNED_SHORT, 0, patchCount);
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
