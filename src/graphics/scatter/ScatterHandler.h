// ScatterHandler.h
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "ScatterConfig.h"
#include "ScatterMeshes.h"
#include "../impostor/ImpostorBaker.h"

class ShaderProgram;
class SSBOManager;
class Texture;
class Texture2D;
class TextureStore;
struct FrameRenderParams;
struct ScatterSet;
struct ScatterInstance;

// The C++ structs below mirror the structs of the same name in scatter_data.glsl.

// Rebuilt every frame. The rotation comes from the CPU, which already has it from
// bodyRenderPose, so items turn by exactly the ground's rotation.
struct ScatterInstanceData {
    // Body -> world, as std430 lays out a mat3
    glm::mat3x4 m_bodyRotation{1.0f};
    // xyz: the body's scale
    glm::vec4 m_bodyScale{1.0f};
};
static_assert(sizeof(ScatterInstanceData) == 64,
              "ScatterInstanceData must match the 64-byte std430 layout the scatter shaders read");

// Rebuilt every frame.
struct ScatterPatchData {
    // Patch origin minus camera, in the body's frame. Subtracted in double, so
    // it is exact near the camera.
    glm::vec3 m_cameraOffset{0.0f};
    uint32_t m_slot{0};
    // Index into the frame's instance list
    uint32_t m_instance{0};
    uint32_t m_padding[3]{};
};
static_assert(sizeof(ScatterPatchData) == 32,
              "ScatterPatchData must match the 32-byte std430 layout the scatter shaders read");

struct ScatterKindData {
    glm::vec4 m_color{1.0f};
    // Colour, normal, material, mask units among ScatterSet::m_textureUnits; -1 for none
    glm::ivec4 m_textureUnits{-1};
    // LOD n is mesh m_firstMesh + n
    uint32_t m_firstMesh{0};
    uint32_t m_lodCount{0};
    float m_boundingRadius{0.0f};
    // Drawn past the last LOD, or ScatterMeshes::k_noMesh
    uint32_t m_impostorMesh{ScatterMeshes::k_noMesh};
};
static_assert(sizeof(ScatterKindData) == 48,
              "ScatterKindData must match the 48-byte std430 layout the scatter shaders read");

// One per kind, zeroed for kinds without an impostor.
struct ScatterImpostorData {
    // The sphere the views frame, in the mesh's frame: centre, radius
    glm::vec4 m_centreRadius{0.0f};
    // Colour unit, normal unit, views per side, pixels per view
    glm::ivec4 m_atlas{0};
    // x: border pixels
    glm::ivec4 m_border{0};
    // Far edge band: x start metres, y width metres (0 for none)
    glm::vec4 m_fadeBand{0.0f};
    // xyz: ScatterImpostor::m_fadeOffsetMetres
    glm::vec4 m_fadeOffset{0.0f};
};
static_assert(sizeof(ScatterImpostorData) == 80,
              "ScatterImpostorData must match the 80-byte std430 layout the scatter shaders read");

// GL's DrawElementsIndirectCommand, one per mesh. The culling pass counts
// m_instanceCount up.
struct ScatterDrawCommand {
    uint32_t m_count{0};
    uint32_t m_instanceCount{0};
    uint32_t m_firstIndex{0};
    int32_t m_baseVertex{0};
    // Start of the mesh's part of the visible list
    uint32_t m_baseInstance{0};
};
static_assert(sizeof(ScatterDrawCommand) == 20,
              "ScatterDrawCommand must match GL's DrawElementsIndirectCommand");

// A texture the set's snippet samples through the sampler it declares.
struct ScatterSnippetTexture {
    std::string m_samplerName;
    std::weak_ptr<Texture> m_texture;
};

struct ScatterPatch {
    ScatterInstance* m_instance{nullptr};
    // Block k of the item buffer starts at item k * itemsPerPatch
    uint32_t m_slot{0};
    // In the body's frame. Double: on a planet it is millions of metres out.
    glm::dvec3 m_origin{0.0};
};

// A set placed on one body.
struct ScatterInstance {
    // The body's transform in the shared SSBO, the same its other renderers use
    int m_ssboIndex{-1};
    ScatterSet* m_set{nullptr};
    std::vector<std::shared_ptr<ScatterPatch>> m_patches;
};

// Kinds, their meshes, and one item buffer shared by every instance.
struct ScatterSet {
    ScatterSet(const ScatterSetConfig& config, TextureStore& textureStore);
    ~ScatterSet();

    ScatterSet(const ScatterSet&) = delete;
    ScatterSet& operator=(const ScatterSet&) = delete;

    ScatterSetConfig m_config;
    ScatterMeshes m_meshes;

    // A texture's place here is its unit in the draws. The set is one draw, so
    // all its kinds share 32 units.
    std::vector<std::weak_ptr<Texture2D>> m_textureUnits;

    // Asked again on every shader reload
    std::function<std::string()> m_readSnippet;
    std::unique_ptr<ShaderProgram> m_generateProgram;
    // A texture's place here is its unit in the generation pass
    std::vector<ScatterSnippetTexture> m_snippetTextures;

    // One per kind, empty without an impostor
    std::vector<std::optional<ImpostorAtlas>> m_impostors;
    // ScatterImpostorData per kind
    GLuint m_impostorBuffer{0};

    // m_maxPatches blocks of m_itemsPerPatch ScatterItems
    GLuint m_itemBuffer{0};
    // uvec4 payload per block
    GLuint m_payloadBuffer{0};
    // Item count per block, counted up on the GPU. Items past it are stale.
    GLuint m_countBuffer{0};

    // ScatterKindData per kind, and the max distance per mesh
    GLuint m_kindTableBuffer{0};
    GLuint m_lodDistanceBuffer{0};

    // With every count at 0, uploaded each frame before culling
    std::vector<ScatterDrawCommand> m_drawCommands;
    GLuint m_commandBuffer{0};

    // uvec2 (item, patch list index). One part per mesh, each with room for every
    // item, so no part can overflow.
    GLuint m_visibleBuffer{0};

    // This frame's lists. Patches name their instance by its index in the list.
    std::vector<ScatterInstanceData> m_instanceList;
    std::vector<ScatterPatchData> m_patchList;
    GLuint m_instanceListBuffer{0};
    GLuint m_patchListBuffer{0};

    std::vector<uint32_t> m_freeSlots;

    std::vector<std::shared_ptr<ScatterInstance>> m_instances;
};

/**
 * @brief Renderer for large numbers of small objects placed in patches.
 *
 * The handler owns sets, a set its instances, an instance its patches; callers
 * hold weak_ptrs, which expire with their object.
 *
 * Items are made on the GPU when a patch is added. Each frame, update culls and
 * picks each item's mesh; renderGeometry draws each set with two multi-draws, one
 * for the LOD meshes and one for the impostors.
 */
class ScatterHandler {
public:
    ScatterHandler(SSBOManager* ssboManager, TextureStore* textureStore);
    ~ScatterHandler();

    ScatterHandler(const ScatterHandler&) = delete;
    ScatterHandler& operator=(const ScatterHandler&) = delete;

    // Throws if the config is invalid, a file fails to load, the kinds need more
    // than 32 textures, or the snippet does not compile.
    std::weak_ptr<ScatterSet> createSet(const ScatterSetConfig& config,
                                        std::function<std::string()> readSnippet);
    void removeSet(std::weak_ptr<ScatterSet> set);
    // Sets or replaces by name; patches already made keep what they were made
    // with. Throws if the set has expired or a 33rd name is given.
    void setSnippetTexture(std::weak_ptr<ScatterSet> set, const std::string& samplerName,
                           std::weak_ptr<Texture> texture);

    // Throws if the set has expired.
    std::weak_ptr<ScatterInstance> createInstance(std::weak_ptr<ScatterSet> set,
                                                  int ssboIndex);
    void removeInstance(std::weak_ptr<ScatterInstance> instance);

    // Queues the generation of the patch's items without waiting for it. Throws
    // if the instance has expired or the set has no free block.
    std::weak_ptr<ScatterPatch> addPatch(std::weak_ptr<ScatterInstance> instance,
                                         const glm::dvec3& origin,
                                         const glm::uvec4& payload);
    void removePatch(std::weak_ptr<ScatterPatch> patch);

    // Once per frame, before drawing.
    void update(const FrameRenderParams& params);

    // Into the bound G-buffer, after update.
    void renderGeometry(const FrameRenderParams& params);

    // As impostor_<kind>_*.png in directory. False if any could not be written.
    bool writeImpostorImages(std::weak_ptr<ScatterSet> set,
                             const std::filesystem::path& directory) const;

    // A program that no longer compiles is kept as it was.
    std::pair<bool, std::string> reloadShaders();

private:
    SSBOManager* m_ssboManager{nullptr};
    TextureStore* m_textureStore{nullptr};
    std::vector<std::shared_ptr<ScatterSet>> m_sets;

    std::unique_ptr<ShaderProgram> m_cullProgram;
    std::unique_ptr<ShaderProgram> m_gbufferProgram;
    std::unique_ptr<ShaderProgram> m_impostorProgram;
    std::unique_ptr<ImpostorBaker> m_impostorBaker;

    // Bakes from each kind's LOD 0 and adds the atlases to the set's units.
    void bakeImpostors(ScatterSet& set);

    void buildFrameLists(ScatterSet& set, const FrameRenderParams& params) const;
    void cull(const ScatterSet& set) const;
    void bindTextures(const ScatterSet& set, const ShaderProgram& program) const;

    // Throws std::invalid_argument describing the first problem found.
    static void validateConfig(const ScatterSetConfig& config);
    static std::unique_ptr<ShaderProgram> buildGenerateProgram(const std::string& snippet);
    // One thread per candidate.
    void generateBlock(const ScatterSet& set, const ScatterPatch& patch) const;
    static void releaseSlot(const ScatterPatch& patch);
};
