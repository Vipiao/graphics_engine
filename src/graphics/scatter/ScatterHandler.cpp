// ScatterHandler.cpp
#include "ScatterHandler.h"
#include "../BodyPose.h"
#include "../FrameRenderParams.h"
#include "../InstanceFrameUniforms.h"
#include "../SSBOManager.h"
#include "../ShaderProgram.h"
#include "../TextureStore.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>
#include "math/DekkerArithmetic.h"

namespace {

constexpr const char* s_generatePath =
    ENGINE_ASSET_DIR "/src/graphics/scatter/scatter_generate.comp";
constexpr const char* s_cullPath =
    ENGINE_ASSET_DIR "/src/graphics/scatter/scatter_cull.comp";
constexpr const char* s_vertexPath =
    ENGINE_ASSET_DIR "/src/graphics/scatter/scatter_vertex_shader.vert";
constexpr const char* s_gbufferFragmentPath =
    ENGINE_ASSET_DIR "/src/graphics/shared_shaders/gbuffer_fragment_shader.frag";
constexpr const char* s_impostorVertexPath =
    ENGINE_ASSET_DIR "/src/graphics/scatter/scatter_impostor.vert";
constexpr const char* s_impostorFragmentPath =
    ENGINE_ASSET_DIR "/src/graphics/scatter/scatter_impostor.frag";
constexpr const char* s_snippetMarker = "__SCATTER_CANDIDATE__";

// Binding 0 is the shared transform SSBO. The shaders get these from
// addScatterDefines.
constexpr GLuint s_itemBinding{1};
constexpr GLuint s_payloadBinding{2};
constexpr GLuint s_countBinding{3};
constexpr GLuint s_instanceBinding{4};
constexpr GLuint s_patchBinding{5};
constexpr GLuint s_kindBinding{6};
constexpr GLuint s_lodDistanceBinding{7};
constexpr GLuint s_commandBinding{8};
constexpr GLuint s_visibleBinding{9};
constexpr GLuint s_impostorBinding{10};
constexpr uint32_t s_generateGroupSize{64};

// Foliage is cut out where its alpha falls below this, in the bake as in the draw
constexpr float s_alphaCutoff{0.5f};

void addScatterDefines(ShaderProgram& program) {
    const std::pair<const char*, uint32_t> defines[]{
        {"SCATTER_ITEM_BINDING", s_itemBinding},
        {"SCATTER_PAYLOAD_BINDING", s_payloadBinding},
        {"SCATTER_COUNT_BINDING", s_countBinding},
        {"SCATTER_INSTANCE_BINDING", s_instanceBinding},
        {"SCATTER_PATCH_BINDING", s_patchBinding},
        {"SCATTER_KIND_BINDING", s_kindBinding},
        {"SCATTER_LOD_DISTANCE_BINDING", s_lodDistanceBinding},
        {"SCATTER_COMMAND_BINDING", s_commandBinding},
        {"SCATTER_VISIBLE_BINDING", s_visibleBinding},
        {"SCATTER_IMPOSTOR_BINDING", s_impostorBinding},
        {"SCATTER_NO_MESH", ScatterMeshes::k_noMesh},
        {"SCATTER_GENERATE_GROUP_SIZE", s_generateGroupSize}};
    for (const auto& [name, value] : defines) {
        program.addDefine(name, std::to_string(value));
    }
}

GLuint createStorageBuffer(size_t bytes) {
    GLuint buffer{0};
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return buffer;
}

template <typename T>
void uploadStaticList(GLuint buffer, const std::vector<T>& list) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(list.size() * sizeof(T)),
                 list.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Reallocates, so the driver need not wait for last frame's draw to finish
// reading the old contents.
template <typename T>
void uploadDynamicList(GLuint buffer, const std::vector<T>& list) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(list.size() * sizeof(T)),
                 list.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// The texture's new unit among units
int addTextureUnit(std::vector<std::weak_ptr<Texture>>& units, std::weak_ptr<Texture> texture) {
    if (units.size() >= static_cast<size_t>(ShaderProgram::s_maxTextureUnits)) {
        throw std::runtime_error("ScatterHandler::createSet: the kinds of a set and their "
                                 "impostors can use at most " +
                                 std::to_string(ShaderProgram::s_maxTextureUnits) +
                                 " textures");
    }
    units.push_back(std::move(texture));
    return static_cast<int>(units.size() - 1);
}

// A material without a texture of a role gets a layer that decodes as none would:
// white colour and mask, a flat normal, material_decode.glsl's k_defaultRoughness
constexpr std::array<std::array<uint8_t, 4>, 4> s_roleFills{{{255, 255, 255, 255},
                                                             {128, 128, 255, 255},
                                                             {0, 89, 0, 255},
                                                             {255, 255, 255, 255}}};

std::array<std::weak_ptr<Texture2DArray>, 4> createKindTextures(const ScatterKind& kind,
                                                                TextureStore& textureStore) {
    std::array<std::weak_ptr<Texture2DArray>, 4> textures;
    for (size_t role{0}; role < textures.size(); ++role) {
        std::vector<std::string> paths;
        for (const ScatterMaterial& material : kind.m_materials) {
            const std::string rolePaths[4]{material.m_colorTexturePath,
                                           material.m_normalTexturePath,
                                           material.m_materialTexturePath,
                                           material.m_maskTexturePath};
            paths.push_back(rolePaths[role]);
        }
        if (std::any_of(paths.begin(), paths.end(),
                        [](const std::string& path) { return !path.empty(); })) {
            textures[role] = textureStore.createArrayFromFiles(paths, s_roleFills[role]);
        }
    }
    return textures;
}
}

ScatterSet::ScatterSet(const ScatterSetConfig& config, TextureStore& textureStore)
    : m_config{config}, m_meshes{config.m_kinds} {
    const size_t itemCapacity{static_cast<size_t>(config.m_maxPatches) *
                              config.m_itemsPerPatch};

    m_itemBuffer = createStorageBuffer(itemCapacity * sizeof(ScatterItem));
    m_payloadBuffer = createStorageBuffer(config.m_maxPatches * sizeof(glm::uvec4));
    m_countBuffer = createStorageBuffer(config.m_maxPatches * sizeof(uint32_t));
    glGenBuffers(1, &m_instanceListBuffer);
    glGenBuffers(1, &m_patchListBuffer);

    std::vector<ScatterKindData> kindTable;
    std::vector<float> lodDistances;
    for (uint32_t kind{0}; kind < config.m_kinds.size(); ++kind) {
        const ScatterKind& kindConfig{config.m_kinds[kind]};
        const std::vector<ScatterLod>& lods{kindConfig.m_lods};
        m_kindTextures.push_back(createKindTextures(kindConfig, textureStore));
        glm::ivec4 textureUnits{-1};
        for (int role{0}; role < 4; ++role) {
            if (!m_kindTextures.back()[role].expired()) {
                textureUnits[role] = addTextureUnit(m_textureUnits, m_kindTextures.back()[role]);
            }
        }
        // Holds the impostor too: its quad frames the last LOD's sphere and moves
        // towards the fade offset
        float boundingRadius{m_meshes.getBoundingRadius(kind)};
        if (kindConfig.m_impostor) {
            const uint32_t lastLod{
                m_meshes.meshIndex(kind, static_cast<uint32_t>(lods.size()) - 1)};
            boundingRadius = std::max(
                {boundingRadius,
                 glm::length(m_meshes.getCentre(lastLod)) + m_meshes.getRadius(lastLod),
                 static_cast<float>(glm::length(kindConfig.m_impostor->m_fadeOffsetMetres))});
        }
        kindTable.push_back(ScatterKindData{glm::vec4{kindConfig.m_color}, textureUnits,
                                            m_meshes.meshIndex(kind, 0),
                                            static_cast<uint32_t>(lods.size()), boundingRadius,
                                            m_meshes.impostorMesh(kind)});
        for (const ScatterLod& lod : lods) {
            lodDistances.push_back(static_cast<float>(lod.m_maxDistance));
        }
    }
    // The impostor quads follow every LOD mesh, in kind order
    for (const ScatterKind& kindConfig : config.m_kinds) {
        if (kindConfig.m_impostor) {
            lodDistances.push_back(static_cast<float>(kindConfig.m_impostor->m_maxDistance));
        }
    }
    assert(lodDistances.size() == m_meshes.getMeshCount() && "one distance per mesh");
    glGenBuffers(1, &m_kindTableBuffer);
    uploadStaticList(m_kindTableBuffer, kindTable);
    glGenBuffers(1, &m_lodDistanceBuffer);
    uploadStaticList(m_lodDistanceBuffer, lodDistances);

    for (uint32_t mesh{0}; mesh < m_meshes.getMeshCount(); ++mesh) {
        const ScatterMeshRange& range{m_meshes.getRange(mesh)};
        m_drawCommands.push_back(ScatterDrawCommand{
            range.m_indexCount, 0, range.m_firstIndex, range.m_baseVertex,
            static_cast<uint32_t>(mesh * itemCapacity)});
    }
    glGenBuffers(1, &m_commandBuffer);
    m_visibleBuffer =
        createStorageBuffer(m_meshes.getMeshCount() * itemCapacity * sizeof(glm::uvec2));

    // Reversed, so block 0 is handed out first
    m_freeSlots.reserve(config.m_maxPatches);
    for (uint32_t slot{config.m_maxPatches}; slot > 0; --slot) {
        m_freeSlots.push_back(slot - 1);
    }
}

ScatterSet::~ScatterSet() {
    if (m_impostorBuffer != 0) glDeleteBuffers(1, &m_impostorBuffer);
    if (m_itemBuffer != 0) glDeleteBuffers(1, &m_itemBuffer);
    if (m_payloadBuffer != 0) glDeleteBuffers(1, &m_payloadBuffer);
    if (m_countBuffer != 0) glDeleteBuffers(1, &m_countBuffer);
    if (m_instanceListBuffer != 0) glDeleteBuffers(1, &m_instanceListBuffer);
    if (m_patchListBuffer != 0) glDeleteBuffers(1, &m_patchListBuffer);
    if (m_kindTableBuffer != 0) glDeleteBuffers(1, &m_kindTableBuffer);
    if (m_lodDistanceBuffer != 0) glDeleteBuffers(1, &m_lodDistanceBuffer);
    if (m_commandBuffer != 0) glDeleteBuffers(1, &m_commandBuffer);
    if (m_visibleBuffer != 0) glDeleteBuffers(1, &m_visibleBuffer);
}

ScatterHandler::ScatterHandler(SSBOManager* ssboManager, TextureStore* textureStore)
    : m_ssboManager{ssboManager}, m_textureStore{textureStore} {
    if (!m_ssboManager) {
        throw std::runtime_error("ScatterHandler: SSBOManager cannot be null");
    }
    if (!m_textureStore) {
        throw std::runtime_error("ScatterHandler: TextureStore cannot be null");
    }

    m_impostorBaker = std::make_unique<ImpostorBaker>(m_textureStore);

    m_cullProgram = std::make_unique<ShaderProgram>();
    addScatterDefines(*m_cullProgram);
    m_cullProgram->loadComputeShaderFromPath(s_cullPath);
    m_cullProgram->linkShaders();

    // Kind textures are arrays, a layer per material
    const auto buildMeshProgram{[](const char* passDefine) {
        auto program{std::make_unique<ShaderProgram>()};
        addScatterDefines(*program);
        program->addDefine(passDefine, "");
        program->addDefine("MATERIAL_TEXTURE_ARRAYS", "");
        program->loadVertexShaderFromPath(s_vertexPath);
        program->loadFragmentShaderFromPath(s_gbufferFragmentPath);
        program->linkShaders();
        return program;
    }};
    const auto buildImpostorProgram{[](bool depthOnly) {
        auto program{std::make_unique<ShaderProgram>()};
        addScatterDefines(*program);
        if (depthOnly) program->addDefine("DEPTH_ONLY", "");
        program->loadVertexShaderFromPath(s_impostorVertexPath);
        program->loadFragmentShaderFromPath(s_impostorFragmentPath);
        program->linkShaders();
        return program;
    }};
    m_gbufferDepthProgram = buildMeshProgram("DEPTH_ONLY");
    m_gbufferProgram = buildMeshProgram("EARLY_FRAGMENT_TESTS");
    m_impostorDepthProgram = buildImpostorProgram(true);
    m_impostorProgram = buildImpostorProgram(false);
}

// Here, where ShaderProgram is a complete type, so the unique_ptrs can delete it.
ScatterHandler::~ScatterHandler() = default;

std::weak_ptr<ScatterSet> ScatterHandler::createSet(
    const ScatterSetConfig& config, std::function<std::string()> readSnippet) {
    validateConfig(config);
    if (!readSnippet) {
        throw std::invalid_argument(
            "ScatterHandler::createSet: a set needs a snippet to make its items");
    }

    auto set{std::make_shared<ScatterSet>(config, *m_textureStore)};
    set->m_readSnippet = std::move(readSnippet);
    set->m_generateProgram = buildGenerateProgram(set->m_readSnippet());
    bakeImpostors(*set);
    m_sets.push_back(std::move(set));
    return m_sets.back();
}

void ScatterHandler::bakeImpostors(ScatterSet& set) {
    const std::vector<ScatterKind>& kinds{set.m_config.m_kinds};
    set.m_impostors.resize(kinds.size());

    for (uint32_t kind{0}; kind < kinds.size(); ++kind) {
        const ScatterKind& kindConfig{kinds[kind]};
        if (!kindConfig.m_impostor) continue;

        // The last LOD, the one the impostor takes over from
        assert(!kindConfig.m_lods.empty() && "validateConfig requires a LOD");
        const uint32_t lastLod{static_cast<uint32_t>(kindConfig.m_lods.size()) - 1};
        const uint32_t meshIndex{set.m_meshes.meshIndex(kind, lastLod)};
        const ScatterMeshRange& range{set.m_meshes.getRange(meshIndex)};
        const ImpostorMesh mesh{set.m_meshes.getVAO(), range.m_firstIndex, range.m_indexCount,
                                range.m_baseVertex, set.m_meshes.getCentre(meshIndex),
                                set.m_meshes.getRadius(meshIndex)};

        const ImpostorMaterial material{glm::vec4{kindConfig.m_color}, set.m_kindTextures[kind],
                                        s_alphaCutoff};

        set.m_impostors[kind] =
            m_impostorBaker->bake(mesh, material, kindConfig.m_impostor->m_bake);
    }

    // The atlases take the units after the kinds' own textures
    std::vector<ScatterImpostorData> impostorTable(kinds.size());
    for (size_t kind{0}; kind < kinds.size(); ++kind) {
        if (!set.m_impostors[kind]) continue;
        const ImpostorAtlas& atlas{*set.m_impostors[kind]};

        const int colorUnit{addTextureUnit(set.m_textureUnits, atlas.m_color)};
        const int normalUnit{addTextureUnit(set.m_textureUnits, atlas.m_normal)};

        const ScatterImpostor& config{*kinds[kind].m_impostor};
        impostorTable[kind] = ScatterImpostorData{
            glm::vec4{atlas.m_centre, atlas.m_radius},
            glm::ivec4{colorUnit, normalUnit, atlas.m_spec.m_viewsPerSide,
                       atlas.m_spec.m_viewPixels},
            glm::ivec4{atlas.m_spec.m_borderPixels, 0, 0, 0},
            glm::vec4{config.m_maxDistance - config.m_fadeBandMetres, config.m_fadeBandMetres,
                      0.0, 0.0},
            glm::vec4{glm::vec3{config.m_fadeOffsetMetres}, 0.0f}};
    }
    glGenBuffers(1, &set.m_impostorBuffer);
    uploadStaticList(set.m_impostorBuffer, impostorTable);
}

bool ScatterHandler::writeImpostorImages(std::weak_ptr<ScatterSet> setWeak,
                                         const std::filesystem::path& directory) const {
    const std::shared_ptr<ScatterSet> set{setWeak.lock()};
    if (!set) return false;

    bool written{true};
    for (size_t kind{0}; kind < set->m_impostors.size(); ++kind) {
        if (!set->m_impostors[kind]) continue;
        written = ImpostorBaker::writeImages(*set->m_impostors[kind], directory,
                                             "impostor_" + std::to_string(kind)) &&
                  written;
    }
    return written;
}

void ScatterHandler::removeSet(std::weak_ptr<ScatterSet> setWeak) {
    const std::shared_ptr<ScatterSet> set{setWeak.lock()};
    if (!set) return;

    for (const std::weak_ptr<Texture>& texture : set->m_textureUnits) {
        m_textureStore->remove(texture);
    }

    // Takes its instances and patches with it
    std::erase(m_sets, set);
}

void ScatterHandler::setSnippetTexture(std::weak_ptr<ScatterSet> setWeak,
                                       const std::string& samplerName,
                                       std::weak_ptr<Texture> texture) {
    const std::shared_ptr<ScatterSet> set{setWeak.lock()};
    if (!set) {
        throw std::runtime_error("ScatterHandler::setSnippetTexture: set has expired");
    }

    // Replacing keeps the unit, so the other textures keep theirs.
    for (ScatterSnippetTexture& existing : set->m_snippetTextures) {
        if (existing.m_samplerName == samplerName) {
            existing.m_texture = std::move(texture);
            return;
        }
    }

    if (set->m_snippetTextures.size() >= static_cast<size_t>(ShaderProgram::s_maxTextureUnits)) {
        throw std::runtime_error("ScatterHandler::setSnippetTexture: a snippet can sample at most " +
                                 std::to_string(ShaderProgram::s_maxTextureUnits) +
                                 " textures");
    }
    set->m_snippetTextures.push_back(ScatterSnippetTexture{samplerName, std::move(texture)});
}

std::weak_ptr<ScatterInstance> ScatterHandler::createInstance(
    std::weak_ptr<ScatterSet> setWeak, int ssboIndex) {
    const std::shared_ptr<ScatterSet> set{setWeak.lock()};
    if (!set) {
        throw std::runtime_error("ScatterHandler::createInstance: set has expired");
    }

    auto instance{std::make_shared<ScatterInstance>()};
    instance->m_ssboIndex = ssboIndex;
    instance->m_set = set.get();
    set->m_instances.push_back(instance);
    return instance;
}

void ScatterHandler::removeInstance(std::weak_ptr<ScatterInstance> instanceWeak) {
    const std::shared_ptr<ScatterInstance> instance{instanceWeak.lock()};
    if (!instance) return;

    // The blocks belong to the set
    for (const std::shared_ptr<ScatterPatch>& patch : instance->m_patches) {
        releaseSlot(*patch);
    }
    std::erase(instance->m_set->m_instances, instance);
}

std::weak_ptr<ScatterPatch> ScatterHandler::addPatch(
    std::weak_ptr<ScatterInstance> instanceWeak, const glm::dvec3& origin,
    const glm::uvec4& payload) {
    const std::shared_ptr<ScatterInstance> instance{instanceWeak.lock()};
    if (!instance) {
        throw std::runtime_error("ScatterHandler::addPatch: instance has expired");
    }
    ScatterSet& set{*instance->m_set};

    if (set.m_freeSlots.empty()) {
        throw std::runtime_error("ScatterHandler::addPatch: all " +
                                 std::to_string(set.m_config.m_maxPatches) +
                                 " patches of the set are in use");
    }

    auto patch{std::make_shared<ScatterPatch>()};
    patch->m_instance = instance.get();
    patch->m_slot = set.m_freeSlots.back();
    patch->m_origin = origin;
    set.m_freeSlots.pop_back();
    assert(patch->m_slot < set.m_config.m_maxPatches && "slot out of range");

    // The count back to 0, whatever the block's previous patch left
    const uint32_t emptyCount{0};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, set.m_payloadBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    static_cast<GLintptr>(patch->m_slot * sizeof(glm::uvec4)),
                    sizeof(glm::uvec4), &payload);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, set.m_countBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    static_cast<GLintptr>(patch->m_slot * sizeof(uint32_t)),
                    sizeof(uint32_t), &emptyCount);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    generateBlock(set, *patch);

    instance->m_patches.push_back(patch);
    return patch;
}

void ScatterHandler::removePatch(std::weak_ptr<ScatterPatch> patchWeak) {
    const std::shared_ptr<ScatterPatch> patch{patchWeak.lock()};
    if (!patch) return;

    releaseSlot(*patch);
    std::erase(patch->m_instance->m_patches, patch);
}

void ScatterHandler::update(const FrameRenderParams& params) {
    // One barrier for every generation pass since last frame: they write separate
    // blocks, so need none between them
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    for (const std::shared_ptr<ScatterSet>& set : m_sets) {
        buildFrameLists(*set, params);
        cull(*set, params);
    }
}

void ScatterHandler::cull(const ScatterSet& set, const FrameRenderParams& params) const {
    if (set.m_patchList.empty()) return;

    m_cullProgram->use();
    setInstanceFrameUniforms(m_cullProgram->getID(), params);
    m_cullProgram->setUniformUInt32("u_itemsPerPatch", set.m_config.m_itemsPerPatch);
    m_cullProgram->setUniformUInt32("u_kindCount",
                                    static_cast<uint32_t>(set.m_config.m_kinds.size()));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_itemBinding, set.m_itemBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_countBinding, set.m_countBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_instanceBinding, set.m_instanceListBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_patchBinding, set.m_patchListBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_kindBinding, set.m_kindTableBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_lodDistanceBinding, set.m_lodDistanceBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_commandBinding, set.m_commandBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_visibleBinding, set.m_visibleBuffer);

    // One workgroup per patch
    glDispatchCompute(static_cast<GLuint>(set.m_patchList.size()), 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

void ScatterHandler::renderGeometry(const FrameRenderParams& params) {
    for (ShaderProgram* program : {m_gbufferDepthProgram.get(), m_gbufferProgram.get(),
                                   m_impostorDepthProgram.get(), m_impostorProgram.get()}) {
        program->use();
        setInstanceFrameUniforms(program->getID(), params);
        // Every frame, as a shader reload resets it
        program->setUniformFloat("u_alphaCutoff", s_alphaCutoff);
    }
    // Earlier passes bind these units directly
    m_textureStore->invalidateBindings();
    GLint depthFunc{GL_GEQUAL};
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    GLboolean depthMask{GL_FALSE};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    assert(depthMask && glIsEnabled(GL_DEPTH_TEST) && "the G-buffer pass tests and writes depth");

    for (const std::shared_ptr<ScatterSet>& set : m_sets) {
        if (set->m_patchList.empty()) continue;

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_itemBinding, set->m_itemBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_instanceBinding, set->m_instanceListBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_patchBinding, set->m_patchListBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_kindBinding, set->m_kindTableBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_visibleBinding, set->m_visibleBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_impostorBinding, set->m_impostorBuffer);
        glBindVertexArray(set->m_meshes.getVAO());
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, set->m_commandBuffer);

        // Depth first, then shade only what matches it: a pixel is shaded once
        // however many items overlap there
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        drawSet(*set, *m_gbufferDepthProgram, *m_impostorDepthProgram);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);
        drawSet(*set, *m_gbufferProgram, *m_impostorProgram);
        glDepthFunc(static_cast<GLenum>(depthFunc));
        glDepthMask(GL_TRUE);
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}

void ScatterHandler::drawSet(const ScatterSet& set, ShaderProgram& meshProgram,
                             ShaderProgram& impostorProgram) const {
    const GLsizei lodMeshes{static_cast<GLsizei>(set.m_meshes.getLodMeshCount())};
    meshProgram.use();
    bindTextures(set, meshProgram);
    // Both sides of a card, with the same normal
    const GLboolean culling{glIsEnabled(GL_CULL_FACE)};
    glDisable(GL_CULL_FACE);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, lodMeshes, 0);
    if (culling) glEnable(GL_CULL_FACE);

    const GLsizei impostors{static_cast<GLsizei>(set.m_drawCommands.size()) - lodMeshes};
    assert(impostors >= 0 && "the LOD meshes are a prefix of the commands");
    if (impostors > 0) {
        impostorProgram.use();
        bindTextures(set, impostorProgram);
        glMultiDrawElementsIndirect(
            GL_TRIANGLES, GL_UNSIGNED_INT,
            reinterpret_cast<void*>(static_cast<uintptr_t>(lodMeshes) *
                                    sizeof(ScatterDrawCommand)),
            impostors, 0);
    }
}

void ScatterHandler::bindTextures(const ScatterSet& set, const ShaderProgram& program) const {
    for (size_t unit{0}; unit < set.m_textureUnits.size(); ++unit) {
        const std::shared_ptr<Texture> texture{set.m_textureUnits[unit].lock()};
        if (!texture) continue;

        m_textureStore->bindTexture(static_cast<int>(unit), *texture);
        const GLint location{program.getTextureUnitLocation(static_cast<int>(unit))};
        if (location != -1) {
            glUniform1i(location, static_cast<GLint>(unit));
        }
    }
}

void ScatterHandler::buildFrameLists(ScatterSet& set, const FrameRenderParams& params) const {
    set.m_instanceList.clear();
    set.m_patchList.clear();

    for (const std::shared_ptr<ScatterInstance>& instance : set.m_instances) {
        // The pose CDLOD builds for the same body, so items and ground cannot drift
        // apart
        const MeshTransform& transform{m_ssboManager->getMeshTransform(instance->m_ssboIndex)};
        const BodyPose pose{
            bodyRenderPose(transform, params.time, params.timeRemainder, params.camPos)};

        const uint32_t instanceIndex{static_cast<uint32_t>(set.m_instanceList.size())};
        set.m_instanceList.push_back(ScatterInstanceData{paddedRotation(pose.m_bodyRotation),
                                                         glm::vec4{glm::vec3{pose.m_scale}, 0.0f}});

        for (const std::shared_ptr<ScatterPatch>& patch : instance->m_patches) {
            const glm::dvec3 cameraOffset{patch->m_origin - pose.m_cameraBodyPosition};
            set.m_patchList.push_back(
                ScatterPatchData{glm::vec3{cameraOffset}, patch->m_slot, instanceIndex, {}});
        }
    }

    uploadDynamicList(set.m_instanceListBuffer, set.m_instanceList);
    uploadDynamicList(set.m_patchListBuffer, set.m_patchList);
    // Every count back to 0
    uploadDynamicList(set.m_commandBuffer, set.m_drawCommands);
}

std::pair<bool, std::string> ScatterHandler::reloadShaders() {
    std::string allErrors;
    bool allSuccess{true};

    for (ShaderProgram* program :
         {m_cullProgram.get(), m_gbufferDepthProgram.get(), m_gbufferProgram.get(),
          m_impostorDepthProgram.get(), m_impostorProgram.get()}) {
        const auto [success, error] = program->reloadShaders();
        if (!success) {
            allSuccess = false;
            allErrors += error + "\n";
        }
    }

    // A generation program is built from a spliced string, with no path to reload
    // from
    for (const std::shared_ptr<ScatterSet>& set : m_sets) {
        try {
            set->m_generateProgram = buildGenerateProgram(set->m_readSnippet());
        } catch (const std::exception& e) {
            allSuccess = false;
            allErrors += std::string{"Scatter set reload failed: "} + e.what() + "\n";
        }
    }

    return {allSuccess,
            allSuccess ? "All ScatterHandler shaders reloaded successfully" : allErrors};
}

std::unique_ptr<ShaderProgram> ScatterHandler::buildGenerateProgram(const std::string& snippet) {
    std::string source{ShaderProgram::loadTextFileFromPath(s_generatePath)};
    const size_t markerPos{source.find(s_snippetMarker)};
    if (markerPos == std::string::npos) {
        throw std::runtime_error("scatter_generate.comp is missing the snippet marker");
    }
    source.replace(markerPos, std::char_traits<char>::length(s_snippetMarker), snippet);

    auto program{std::make_unique<ShaderProgram>()};
    addScatterDefines(*program);
    program->loadComputeShader(source);
    program->linkShaders();
    return program;
}

void ScatterHandler::generateBlock(const ScatterSet& set, const ScatterPatch& patch) const {
    assert(set.m_generateProgram && "the set's program is built at creation");
    ShaderProgram& program{*set.m_generateProgram};
    program.use();
    program.setUniformUInt32("u_slot", patch.m_slot);
    program.setUniformUInt32("u_itemsPerPatch", set.m_config.m_itemsPerPatch);
    const auto [originHigh, originLow] = splitDekker(patch.m_origin);
    program.setUniformVec3("u_patchOriginHigh", originHigh);
    program.setUniformVec3("u_patchOriginLow", originLow);

    // Other passes bind textures directly
    m_textureStore->invalidateBindings();
    for (size_t unit{0}; unit < set.m_snippetTextures.size(); ++unit) {
        const ScatterSnippetTexture& snippetTexture{set.m_snippetTextures[unit]};
        const std::shared_ptr<Texture> texture{snippetTexture.m_texture.lock()};
        if (!texture) continue;
        m_textureStore->bindTexture(static_cast<int>(unit), *texture);
        program.setUniformInt(snippetTexture.m_samplerName, static_cast<int>(unit));
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_itemBinding, set.m_itemBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_payloadBinding, set.m_payloadBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s_countBinding, set.m_countBuffer);

    const uint32_t groups{(set.m_config.m_itemsPerPatch + s_generateGroupSize - 1) /
                          s_generateGroupSize};
    // update's barrier waits for it
    glDispatchCompute(groups, 1, 1);
}

void ScatterHandler::validateConfig(const ScatterSetConfig& config) {
    const std::string prefix{"ScatterHandler::createSet: "};
    if (config.m_kinds.empty()) {
        throw std::invalid_argument(prefix + "a set needs at least one kind");
    }
    if (config.m_maxPatches == 0 || config.m_itemsPerPatch == 0) {
        throw std::invalid_argument(prefix + "m_maxPatches and m_itemsPerPatch must be above 0");
    }
    size_t meshCount{0};
    for (size_t kind{0}; kind < config.m_kinds.size(); ++kind) {
        const std::vector<ScatterLod>& lods{config.m_kinds[kind].m_lods};
        if (lods.empty()) {
            throw std::invalid_argument(prefix + "kind " + std::to_string(kind) +
                                        " has no LODs");
        }
        // Each LOD starts where the previous ends
        double previousDistance{0.0};
        for (const ScatterLod& lod : lods) {
            if (lod.m_maxDistance <= previousDistance) {
                throw std::invalid_argument(prefix + "kind " + std::to_string(kind) +
                                            " has LOD distances that do not increase");
            }
            previousDistance = lod.m_maxDistance;
        }
        const std::optional<ScatterImpostor>& impostor{config.m_kinds[kind].m_impostor};
        if (impostor && impostor->m_maxDistance <= previousDistance) {
            throw std::invalid_argument(prefix + "kind " + std::to_string(kind) +
                                        " has an impostor distance within its LODs'");
        }
        if (impostor && (impostor->m_fadeBandMetres < 0.0 ||
                         impostor->m_fadeBandMetres > impostor->m_maxDistance - previousDistance)) {
            throw std::invalid_argument(prefix + "kind " + std::to_string(kind) +
                                        " has an impostor fade band wider than its range");
        }
        meshCount += lods.size() + (impostor ? 1 : 0);
    }
    // The visible list is indexed by uint in the shaders
    const size_t visibleEntries{meshCount * config.m_maxPatches * config.m_itemsPerPatch};
    if (visibleEntries > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument(prefix + "meshes x patches x items exceeds 2^32");
    }
}

void ScatterHandler::releaseSlot(const ScatterPatch& patch) {
    std::vector<uint32_t>& freeSlots{patch.m_instance->m_set->m_freeSlots};
    assert(std::find(freeSlots.begin(), freeSlots.end(), patch.m_slot) == freeSlots.end() &&
           "slot released twice");
    freeSlots.push_back(patch.m_slot);
}
