// CdlodHandler.h
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <glad/glad.h>
#include "../ShaderProgram.h"
#include "../FrameRenderParams.h"
#include "../Texture2D.h"
#include "CdlodConfig.h"
#include "CdlodPatchBounds.h"
#include "CdlodTree.h"

class SSBOManager;
class CdlodPatchGeometry;
class TextureStore;
struct CdlodSurface;

// Everything true of a whole body, as the vertex stages read it. Mirrors
// CdlodInstanceData in cdlod_patch.glsl, which the static_assert below pins to
// this layout.
//
// This is the GPU's view of CdlodConfig, not a second copy of it: the config is
// what the caller wrote, this is what one frame needs.
struct CdlodInstanceData {
    glm::vec4 m_baseColor{};
    // The camera in the body's frame, split so a float pair carries it: it is
    // body-sized, and the vertex stage measures every vertex against it.
    glm::vec4 m_cameraBodyPositionHigh{};
    glm::vec4 m_cameraBodyPositionLow{};
    // Body -> world rotation, in the std430 column padding a mat3 takes. Sent
    // rather than rebuilt on the GPU because the camera above is its inverse
    // applied to the eye, and the two have to be exact inverses for the body's
    // world position to cancel rather than nearly cancel.
    glm::mat3x4 m_bodyRotation{1.0f};
    float m_lodRangeFactor{};
    int32_t m_ssboIndex{-1};
    int32_t m_padding[2]{};
};
static_assert(sizeof(CdlodInstanceData) == 112,
              "CdlodInstanceData must match the 112-byte std430 layout in cdlod_patch.glsl");

// One selected leaf as the vertex stages read it, fetched per instance at
// divisor 1. The tree's leaves are in double; narrowing them is a decision about
// what the attributes can carry, so it belongs here with the attribute
// declarations that take their stride and offsets from this struct.
//
// The centre is split across two floats rather than rounded into one. It is the
// only body-sized term a patch carries -- the axes are half a patch edge, which
// shrinks with the patch -- so it is the only one whose last bits would be
// metres on the ground.
struct CdlodPatch {
    glm::vec3 m_centreHigh{0.0f};
    glm::vec3 m_centreLow{0.0f};
    glm::vec3 m_uAxis{0.0f};
    glm::vec3 m_vAxis{0.0f};
    int32_t m_level{0};
    // Position in the surface's m_instanceData, so a patch can reach everything
    // true of the whole body it belongs to.
    int32_t m_instanceIndex{-1};
    // What the tree sized this patch by, so the morph sizes it the same way.
    // Near one, so unlike the centre it loses nothing to a float.
    float m_frameScale{1.0f};
};

// Where a body stands this frame, as both the tree and the vertex stage need it.
//
// The rotation is held at the width it will be uploaded at, and the camera is
// placed by inverting that narrowed rotation rather than the exact one. The
// vertex stage undoes the placement with the same matrix, so what it undoes is
// what was done: the body's world position drops out exactly instead of leaving
// a part in ten million of the camera's distance from the body's centre, which
// on a planet is most of a metre and moves whenever the body turns.
struct CdlodBodyPose {
    glm::dmat3 m_bodyRotation{1.0};
    glm::dvec3 m_cameraBodyPosition{0.0};
};

/**
 * @brief One placed CDLOD body: its quadtree, and where it sits.
 *
 * The handle callers hold. The world transform is not here; it lives in the
 * shared SSBO under m_ssboIndex, so anything else placed on this body can cite
 * the same index and move with it.
 *
 * Owned by the surface that draws it, and drawn as part of that surface's single
 * instanced call.
 */
struct CdlodInstance {
    CdlodInstance(int ssboIndex, const CdlodConfig& config,
                  std::vector<CdlodPatchFrame> rootFrames,
                  std::shared_ptr<const ICdlodPatchBounds> bounds, CdlodSurface* surface)
        : m_ssboIndex{ssboIndex}, m_config{config}, m_surface{surface},
          m_tree{config, std::move(rootFrames), std::move(bounds)} {}

    int m_ssboIndex{-1};
    CdlodConfig m_config{};
    // Non-owning, and cannot dangle: the surface owns this instance, so it
    // outlives it. Owning it would be a cycle, and nothing would ever be freed.
    CdlodSurface* m_surface{nullptr};
    CdlodTree m_tree;
    // The pose this frame's selection was made against. The vertex stage morphs
    // against the same camera, so the morph cannot complete at a different
    // distance than the one the merge was decided at, and turns the vertex by
    // the same rotation the camera was placed with.
    CdlodBodyPose m_pose{};
};

// A texture the snippet samples, and the sampler it reads it through. The unit
// is assigned when the texture is set and holds for the surface's life.
struct CdlodSurfaceTexture {
    std::string m_samplerName;
    // Owned by the shared store, not here, so the same generated map can be
    // reached from anywhere else that wants it.
    std::weak_ptr<Texture2D> m_texture;
    int m_unit{0};
};

// A scalar the snippet reads. Enough to hand a snippet the constants its data
// was generated against, which is the whole reason it exists: a tile size or a
// height range written into the GLSL by hand is a second source of truth for a
// number the caller already has.
struct CdlodSurfaceUniform {
    std::string m_name;
    float m_value{0.0f};
};

/**
 * @brief One surface shape, and everything drawn with it.
 *
 * The unit of batching: a surface owns the programs its snippet was compiled
 * into, the instances wearing that shape, and the buffers they select into. All
 * of them are drawn in one instanced call, so a thousand asteroids sharing a
 * snippet cost one draw and a planet with its own snippet costs a second.
 *
 * Both vertex stages carry the same snippet: the depth pass has to displace a
 * vertex to the exact place the G-buffer pass does, or a body's shadow parts
 * company with the body. The programs are held by pointer so a reload can
 * replace them without replacing the surface the instances belong to.
 */
struct CdlodSurface {
    // Builds the VAO over the shared patch indices and this surface's own
    // buffers. The index buffer is the whole of the per-vertex input; the node
    // record is per instance, at divisor 1.
    explicit CdlodSurface(GLuint patchIndexBuffer);
    ~CdlodSurface();

    CdlodSurface(const CdlodSurface&) = delete;
    CdlodSurface& operator=(const CdlodSurface&) = delete;

    std::string m_snippetPath;
    std::unique_ptr<ShaderProgram> m_gbufferProgram;
    std::unique_ptr<ShaderProgram> m_depthProgram;

    // What the snippet reads beyond the patch itself. Opaque here: the names are
    // the caller's, and nothing in this renderer knows what any of them mean.
    std::vector<CdlodSurfaceTexture> m_textures;
    std::vector<CdlodSurfaceUniform> m_uniforms;

    std::vector<std::shared_ptr<CdlodInstance>> m_instances;

    GLuint m_VAO{0};
    GLuint m_patchVBO{0};
    GLuint m_instanceDataBuffer{0};
    size_t m_patchCapacity{0};
    size_t m_instanceDataCapacity{0};

    // This frame's selection, as the trees produced it and as the GPU takes it.
    // Every instance appends into the first; the second is the same leaves
    // narrowed and stamped with which instance each came from. Kept across
    // frames only so their storage is reused.
    std::vector<CdlodLeaf> m_selectedLeaves;
    std::vector<CdlodPatch> m_selectedPatches;
    // Indexed by position in m_instances, which is the index each patch carries.
    std::vector<CdlodInstanceData> m_instanceData;
};

/**
 * @brief Renderer for continuous-distance level-of-detail bodies.
 *
 * An instance is a set of caller-supplied patch frames, each the root of a
 * quadtree. Nothing here knows what solid they came off or what surface they map
 * to: an asteroid, a planet and a ring station are the same object at this level,
 * and what makes them differ is the snippet injected into the vertex stage and
 * the bounds that say where its patches land.
 *
 * There is no geometry level between the surface and the instance, unlike the
 * instanced-geometry renderers: a subdivided body has no mesh to share, since
 * its triangles are reselected from its own quadtree every frame.
 *
 * Instances are placed in the world through the shared transform SSBO, like
 * every other instanced renderer: the caller allocates an index and drives it
 * with SSBOManager::updateMeshTransform, so anything else riding on the same
 * body can cite that index and move with it.
 *
 * Ownership runs one way: the handler owns the surfaces, a surface owns its
 * instances. Removing a surface therefore removes what it draws, and the
 * caller's instance handles expire with it.
 *
 * Draws opaquely into the G-buffer and casts shadows through the ordinary depth
 * pass. Pass state belongs to DeferredRenderer and ShadowRenderer; this binds
 * programs and issues draws.
 */
class CdlodHandler {
public:
    CdlodHandler(SSBOManager* ssboManager, TextureStore* textureStore);
    ~CdlodHandler();

    CdlodHandler(const CdlodHandler&) = delete;
    CdlodHandler& operator=(const CdlodHandler&) = delete;

    // Builds the vertex programs from the patch scaffold with the given surface
    // snippet injected. The snippet decides the body's shape, so there is no
    // default: a body without one has nowhere to put its vertices.
    std::weak_ptr<CdlodSurface> createSurface(const std::string& snippetPath);
    // Destroys the surface and every instance drawn with it.
    void removeSurface(std::weak_ptr<CdlodSurface> surface);

    // Gives the surface's snippet something to sample or read, under a name the
    // snippet declares. Set or replace by name; both are ignored by a snippet
    // that never mentions them. Nothing here interprets the data -- what a
    // texture holds and how it is projected are decided entirely in the snippet.
    void setSurfaceTexture(std::weak_ptr<CdlodSurface> surface,
                           const std::string& samplerName, const TextureSpec& spec);
    void setSurfaceUniform(std::weak_ptr<CdlodSurface> surface, const std::string& name,
                           float value);

    // The surface owns the instance; the caller holds a weak handle that expires
    // when it is removed, so a stale handle can be recognised rather than
    // silently naming someone else's body. Throws if the surface has expired.
    // rootFrames are the quadtree's starting squares, in the body's own frame;
    // bounds says where any frame derived from them renders.
    std::weak_ptr<CdlodInstance> createInstance(int ssboIndex, const CdlodConfig& config,
                                                std::vector<CdlodPatchFrame> rootFrames,
                                                std::shared_ptr<const ICdlodPatchBounds> bounds,
                                                std::weak_ptr<CdlodSurface> surface);
    void removeInstance(std::weak_ptr<CdlodInstance> instance);

    // Re-selects every instance's visible nodes for this frame and uploads them.
    // Must run before the passes below so they draw one consistent selection.
    void update(const FrameRenderParams& params);

    // G-buffer pass.
    void renderGeometry(const FrameRenderParams& params);
    // Shadow-cascade depth pass. Always solid, whatever the debug view is set to.
    void renderDepth(const FrameRenderParams& params);

    // Debug view: draws the patches as wireframe and tints each quadtree level,
    // so the state of the subdivision is visible. Scoped to this renderer, unlike
    // GraphicsEngineBase's global triangle mode.
    void setWireframe(bool wireframe) { m_wireframe = wireframe; }
    bool getWireframe() const { return m_wireframe; }

    // Quads along one patch edge, shared by every body since they are all drawn
    // from one index buffer. See CdlodPatchGeometry::setPatchQuads.
    void setPatchQuads(int patchQuads);
    int getPatchQuads() const;

    std::pair<bool, std::string> reloadShaders();

private:
    // Binding point of the instance buffer; mirrors the binding declared in
    // cdlod_patch.glsl. Binding 0 is the shared transform SSBO.
    static constexpr GLuint k_instanceDataBinding{1};

    SSBOManager* m_ssboManager{nullptr};
    TextureStore* m_textureStore{nullptr};
    std::unique_ptr<CdlodPatchGeometry> m_patchGeometry;
    // The only owner of the surfaces, and the order they are drawn in.
    std::vector<std::shared_ptr<CdlodSurface>> m_surfaces;

    bool m_wireframe{false};

    // The stage at stagePath with the generated face table and the surface
    // snippet spliced in at their markers. Throws if either is missing.
    static std::string buildStageSource(const char* stagePath,
                                        const std::string& snippetPath);
    // Compiles both programs and installs them, replacing any the surface has.
    // Throws without touching the surface if anything fails to compile, so a
    // broken snippet leaves a working surface working.
    static void buildSurfacePrograms(CdlodSurface& surface);

    // Fills the surface's selection and uploads it.
    void selectVisibleNodes(CdlodSurface& surface, const FrameRenderParams& params);
    static void uploadSelection(CdlodSurface& surface);

    // Binds the surface's textures and pushes its scalars into the bound
    // program. Done per draw rather than once at link time, so a shader reload
    // cannot leave a surface pointing its samplers at units nobody filled.
    void applySurfaceInputs(const CdlodSurface& surface, unsigned int program) const;

    // Draws every surface that selected anything, in the caller's render state.
    void renderAllSurfaces(const FrameRenderParams& params, bool isGeometryPass);
};
