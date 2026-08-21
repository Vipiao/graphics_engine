// RayVolumeHandler.cpp
#include "RayVolumeHandler.h"
#include "../InstanceFrameUniforms.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace {
constexpr const char* s_vertexShaderPath =
    ENGINE_ASSET_DIR "/src/graphics/rayVolume/ray_volume_vertex_shader.vert";
constexpr const char* s_scaffoldPath =
    ENGINE_ASSET_DIR "/src/graphics/rayVolume/ray_volume_fragment_scaffold.frag";
constexpr const char* s_defaultBodyPath =
    ENGINE_ASSET_DIR "/src/graphics/rayVolume/ray_volume_default_body.glsl";
constexpr const char* s_bodyMarker = "__RAY_VOLUME_BODY__";
}  // namespace

RayVolumeHandler::RayVolumeHandler(SSBOManager* ssboManager)
    : m_ssboManager(ssboManager) {
    if (!m_ssboManager) {
        throw std::runtime_error("SSBOManager cannot be null");
    }
}

RayVolumeHandler::~RayVolumeHandler() {
    for (VolumeGeometry& volume : m_volumes) {
        if (volume.auxVBO != 0) {
            glDeleteBuffers(1, &volume.auxVBO);
        }
    }
}

std::string RayVolumeHandler::buildFragmentSource(const std::string& bodySnippetPath) {
    std::string source = ShaderProgram::loadTextFileFromPath(s_scaffoldPath);
    const std::string bodyPath =
        bodySnippetPath.empty() ? s_defaultBodyPath : bodySnippetPath;
    std::string body = ShaderProgram::loadTextFileFromPath(bodyPath);

    size_t markerPos = source.find(s_bodyMarker);
    if (markerPos == std::string::npos) {
        throw std::runtime_error("Ray-volume scaffold is missing the body marker");
    }
    source.replace(markerPos, std::char_traits<char>::length(s_bodyMarker), body);
    return source;
}

void RayVolumeHandler::buildMaterialProgram(Material& material) {
    material.program.loadVertexShaderFromPath(s_vertexShaderPath);
    material.program.loadFragmentShader(buildFragmentSource(material.bodySnippetPath));
    material.program.linkShaders();
}

size_t RayVolumeHandler::createMaterial(const std::string& bodySnippetPath) {
    auto material = std::make_unique<Material>();
    material->bodySnippetPath = bodySnippetPath;

    buildMaterialProgram(*material);
    m_materials.push_back(std::move(material));
    return m_materials.size() - 1;
}

std::weak_ptr<Geometry> RayVolumeHandler::createGeometry(const std::string& modelPath,
                                                         size_t materialIndex) {
    if (materialIndex >= m_materials.size()) {
        throw std::runtime_error("RayVolumeHandler::createGeometry: invalid material index");
    }

    VolumeGeometry volume;
    volume.geometry = Geometry::loadFromFile(modelPath);
    volume.materialIndex = materialIndex;
    setupAuxBuffer(volume);

    m_volumes.push_back(std::move(volume));
    return m_volumes.back().geometry;
}

void RayVolumeHandler::setupAuxBuffer(VolumeGeometry& volume) {
    // Bind the auxiliary value stream (state/velocity) into the geometry's VAO at
    // the ray-volume attribute locations, alongside the base instance buffer.
    glBindVertexArray(volume.geometry->m_VAO);
    glGenBuffers(1, &volume.auxVBO);
    glBindBuffer(GL_ARRAY_BUFFER, volume.auxVBO);

    glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, sizeof(RayVolumeAux),
                          (void*)offsetof(RayVolumeAux, state));
    glEnableVertexAttribArray(13);
    glVertexAttribDivisor(13, 1);

    glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, sizeof(RayVolumeAux),
                          (void*)offsetof(RayVolumeAux, velocity));
    glEnableVertexAttribArray(14);
    glVertexAttribDivisor(14, 1);

    glBindVertexArray(0);
}

RayVolumeHandler::VolumeGeometry* RayVolumeHandler::findVolume(
    const std::shared_ptr<Geometry>& geometry) {
    for (VolumeGeometry& volume : m_volumes) {
        if (volume.geometry->m_uniqueId == geometry->m_uniqueId) {
            return &volume;
        }
    }
    return nullptr;
}

void RayVolumeHandler::releaseGeometry(std::weak_ptr<Geometry> geometryWeak) {
    auto geometry = geometryWeak.lock();
    if (!geometry) return;

    for (auto it = m_volumes.begin(); it != m_volumes.end(); ++it) {
        if (it->geometry->m_uniqueId == geometry->m_uniqueId) {
            if (it->auxVBO != 0) {
                glDeleteBuffers(1, &it->auxVBO);
            }
            m_volumes.erase(it);
            return;
        }
    }
}

std::weak_ptr<Instance> RayVolumeHandler::addInstance(
    std::weak_ptr<Geometry> geometryWeak, int ssboIndex,
    const glm::dvec4& color, const glm::dvec4& state, const glm::dvec4& velocity) {
    auto geometry = geometryWeak.lock();
    if (!geometry) return {};
    VolumeGeometry* volume = findVolume(geometry);
    if (!volume) return {};

    // The base instance buffer holds transform/color; ray-volume materials carry
    // no per-instance texture (texture units unused). The caller sets the local
    // transform afterward via the Instance handle and Geometry::updateInstanceInBuffer,
    // exactly like the instance handler.
    std::weak_ptr<Instance> instanceWeak =
        volume->geometry->addInstance(ssboIndex, -1, -1, -1, color, -1);

    RayVolumeAux aux;
    aux.state = glm::vec4(state);
    aux.velocity = glm::vec4(velocity);
    volume->aux.push_back(aux);
    assert(volume->aux.size() == volume->geometry->m_instances.size()
        && "aux stream must stay in lockstep with the instance buffer");

    // Grow the auxiliary buffer in lockstep with the base instance buffer.
    glBindBuffer(GL_ARRAY_BUFFER, volume->auxVBO);
    if (volume->aux.size() > volume->auxCapacity) {
        volume->auxCapacity = std::max(static_cast<size_t>(1), volume->auxCapacity * 2);
        glBufferData(GL_ARRAY_BUFFER, volume->auxCapacity * sizeof(RayVolumeAux),
                     nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, volume->aux.size() * sizeof(RayVolumeAux),
                        volume->aux.data());
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, (volume->aux.size() - 1) * sizeof(RayVolumeAux),
                        sizeof(RayVolumeAux), &aux);
    }

    return instanceWeak;
}

void RayVolumeHandler::uploadAux(VolumeGeometry& volume, size_t index) {
    glBindBuffer(GL_ARRAY_BUFFER, volume.auxVBO);
    glBufferSubData(GL_ARRAY_BUFFER, index * sizeof(RayVolumeAux),
                    sizeof(RayVolumeAux), &volume.aux[index]);
}

void RayVolumeHandler::setInstanceValues(std::weak_ptr<Geometry> geometryWeak,
                                         std::weak_ptr<Instance> instanceWeak,
                                         const glm::dvec4& state,
                                         const glm::dvec4& velocity) {
    auto geometry = geometryWeak.lock();
    auto instance = instanceWeak.lock();
    if (!geometry || !instance) return;
    VolumeGeometry* volume = findVolume(geometry);
    if (!volume) return;

    uint32_t index = instance->m_bufferIndex;
    if (index >= volume->aux.size()) return;

    volume->aux[index].state = glm::vec4(state);
    volume->aux[index].velocity = glm::vec4(velocity);
    uploadAux(*volume, index);
}

void RayVolumeHandler::removeInstance(std::weak_ptr<Geometry> geometryWeak,
                                      std::weak_ptr<Instance> instanceWeak) {
    auto geometry = geometryWeak.lock();
    auto instance = instanceWeak.lock();
    if (!geometry || !instance) return;
    VolumeGeometry* volume = findVolume(geometry);
    if (!volume) return;

    uint32_t index = instance->m_bufferIndex;
    if (index >= volume->aux.size()) return;

    // Base removeInstance swaps the last instance into this slot; mirror the
    // same swap on the auxiliary array so the two buffers stay aligned.
    volume->geometry->removeInstance(instance);

    size_t last = volume->aux.size() - 1;
    if (index != last) {
        volume->aux[index] = volume->aux[last];
    }
    volume->aux.pop_back();
    if (index < volume->aux.size()) {
        uploadAux(*volume, index);
    }
    assert(volume->aux.size() == volume->geometry->m_instances.size()
        && "aux stream must stay in lockstep with the instance buffer");
}

void RayVolumeHandler::render(const FrameRenderParams& params,
                              unsigned int sceneDepthTexture,
                              unsigned int opaqueColorTexture,
                              unsigned int screenWidth, unsigned int screenHeight) {
    if (m_volumes.empty()) return;

    // Inverted at full width and narrowed after, as the lighting pass does: a
    // float inverse spends the precision reverse-Z was arranged to keep.
    const glm::mat4 inverseProjection{ glm::inverse(params.projection) };

    for (size_t materialIndex = 0; materialIndex < m_materials.size(); ++materialIndex) {
        // Skip materials with no visible instances this frame.
        bool anyInstances = false;
        for (const VolumeGeometry& volume : m_volumes) {
            if (volume.materialIndex == materialIndex && !volume.aux.empty()) {
                anyInstances = true;
                break;
            }
        }
        if (!anyInstances) continue;

        Material& material = *m_materials[materialIndex];
        material.program.use();
        unsigned int id = material.program.getID();

        // Shared per-frame camera/time uniforms (view, projection, Dekker camera).
        setInstanceFrameUniforms(id, params);

        // Ray-volume-specific uniforms.
        glUniformMatrix4fv(glGetUniformLocation(id, "u_inverseProjection"), 1, GL_FALSE,
                           glm::value_ptr(inverseProjection));
        glUniform2f(glGetUniformLocation(id, "u_screenSize"),
                    static_cast<float>(screenWidth), static_cast<float>(screenHeight));

        // Scene depth on unit 0, lit opaque color on unit 1 (for bodies that
        // read the underlying color, e.g. to emulate additive blending).
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
        glUniform1i(glGetUniformLocation(id, "u_sceneDepth"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, opaqueColorTexture);
        glUniform1i(glGetUniformLocation(id, "u_opaqueColor"), 1);

        for (const VolumeGeometry& volume : m_volumes) {
            if (volume.materialIndex != materialIndex) continue;
            const Geometry& geometry = *volume.geometry;
            if (geometry.m_instanceData.empty()) continue;

            glBindVertexArray(geometry.m_VAO);
            if (geometry.m_hasIndices) {
                glDrawElementsInstanced(
                    GL_TRIANGLES, geometry.m_indexCount, GL_UNSIGNED_INT, 0,
                    static_cast<GLsizei>(geometry.m_instanceData.size()));
            } else {
                glDrawArraysInstanced(
                    GL_TRIANGLES, 0, geometry.m_vertexCount,
                    static_cast<GLsizei>(geometry.m_instanceData.size()));
            }
        }
    }

    glBindVertexArray(0);
}

std::pair<bool, std::string> RayVolumeHandler::reloadShaders() {
    bool allSuccess = true;
    std::string errors;

    for (std::unique_ptr<Material>& materialPtr : m_materials) {
        // Rebuild into a fresh Material so a compile failure leaves the existing
        // program untouched; on success the old program is freed by its dtor.
        auto fresh = std::make_unique<Material>();
        fresh->bodySnippetPath = materialPtr->bodySnippetPath;
        try {
            buildMaterialProgram(*fresh);
            materialPtr = std::move(fresh);
        } catch (const std::exception& e) {
            allSuccess = false;
            errors += std::string("Ray-volume material reload failed: ") + e.what() + "\n";
        }
    }

    return {allSuccess,
            allSuccess ? "RayVolumeHandler shaders reloaded successfully" : errors};
}
