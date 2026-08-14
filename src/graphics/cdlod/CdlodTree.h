// CdlodTree.h
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "CdlodConfig.h"
#include "CdlodPatchBounds.h"

// One selected patch, as the vertex stage consumes it: the frame of the leaf it
// came from at the precision the GPU reads it, plus its depth. A node is the
// tree's own entry and persists; a patch is the square it covers and is drawn.
//
// Purely per-patch. Everything true of the whole body -- its radius, its quality
// knob, where its transform lives -- is reached through m_instanceIndex, which
// the renderer stamps on; the tree leaves it alone.
struct CdlodPatch {
    glm::vec3 m_centre{0.0f};
    glm::vec3 m_uAxis{0.0f};
    glm::vec3 m_vAxis{0.0f};
    int32_t m_level{0};           // 0 = root; depth in the quadtree
    int32_t m_instanceIndex{-1};  // filled in by the renderer, not by the tree
};

/**
 * @brief The subdivision quadtree of one body: a pool of nodes over N roots.
 *
 * The tree is the answer, not a cache of it: what gets drawn is exactly its
 * leaves. One traversal per frame splits the leaves the camera has come close
 * to, merges the nodes it has moved away from, and emits what is left.
 *
 * Splitting and merging share one threshold, so the shape is a pure function of
 * where the camera is rather than of how it got there.
 *
 * Distances are measured to a node's bounds rather than its centre, which is what
 * keeps neighbouring leaves within one level of each other: a node that declines
 * to split is at least as far away as any node touching it, so its neighbours
 * decline too.
 *
 * Knows nothing about shapes, renderers or GL. Where a patch ends up is asked
 * of the caller's ICdlodPatchBounds, so the only geometry here is the frames'
 * own: square, and halved by splitting.
 */
class CdlodTree {
public:
    // One root per patch of the solid being subdivided, already in metres: six
    // for a cube, one for a flat patch. The tree neither knows nor cares which.
    CdlodTree(const CdlodConfig& config, std::vector<CdlodPatchFrame> rootFrames,
              std::shared_ptr<const ICdlodPatchBounds> bounds);

    // Splits, merges, and appends one patch per resulting leaf. Appends rather
    // than clears, so the bodies sharing a shader can select into one buffer and
    // be drawn together. cameraBodyPosition is the camera in the
    // body's own frame, at the same interpolated pose the vertex stage will draw
    // the body at.
    void updateAndSelect(const glm::dvec3& cameraBodyPosition,
                         std::vector<CdlodPatch>& patches);

private:
    static constexpr int k_childCount{4};
    // Where one body stops splitting further, counted from wherever in the shared
    // buffer its own selection began. A guard against pathological cases, not a
    // budget to plan against.
    //
    // It bounds splitting, not selection. Every leaf is emitted whatever the
    // count, since skipping one would punch a hole in the surface, so a tree that
    // already holds more leaves than this still emits all of them: with the
    // camera on the surface of a body subdivided to k_maxDepth the selection
    // settles a few dozen patches above the ceiling. Bounded and stable, but not
    // a hard cap.
    static constexpr size_t k_maxSelectedNodes{8192};
    // Hard floor on how far the tree may subdivide. Nothing configures the
    // depth: a node stops splitting once the camera is further than
    // m_lodRangeFactor of its edge lengths away, so the camera's own height
    // above the surface bounds the depth on its own, and this only catches a
    // camera that reaches the surface itself. A frame halved this many times
    // reaches the resolution of the 32-bit CdlodPatch record, which sets it.
    static constexpr int k_maxDepth{16};
    // How far a root's two axes may differ in length, relative to the longer, and
    // still count as square. Only rounding is expected: a cube's faces match
    // exactly, and splitting halves both axes together.
    static constexpr double k_squareTolerance{1e-9};

    // Pool entry. Everything else about a node -- its frame and its depth -- is
    // derived on the way down, since a child's is a halving of its parent's.
    struct CdlodNode {
        int32_t m_firstChild{-1};  // first of four contiguous children, -1 = leaf
    };

    // The body's config as given. CdlodConfig is where the dimensions and the
    // quality knob are decided, defaults included, so there is nothing here to
    // tune: every number the traversal uses is read back out of this.
    CdlodConfig m_config{};
    // The first m_rootFrames.size() pool entries are the roots, so a root's node
    // index is its position here.
    std::vector<CdlodPatchFrame> m_rootFrames;
    // Held rather than borrowed: a tree outlives the call that built it, and a
    // body whose bounds expired would silently stop subdividing.
    std::shared_ptr<const ICdlodPatchBounds> m_bounds;
    std::vector<CdlodNode> m_nodes;
    // Start indices of free four-node blocks. Index-based and LIFO, so reuse
    // order is reproducible.
    std::vector<int32_t> m_freeBlocks;
    // Patch count this traversal may not exceed. Set on entry to updateAndSelect
    // and read only while it runs.
    size_t m_selectionCeiling{0};

    int32_t allocateChildren();
    void freeSubtree(int32_t blockStart);

    void visitNode(int32_t nodeIndex, const CdlodPatchFrame& frame, int depth,
                   const glm::dvec3& cameraBodyPosition,
                   std::vector<CdlodPatch>& patches);

    // The quarter of a frame a child occupies: both axes halved, the centre
    // stepped into one of the four corners.
    static CdlodPatchFrame childFrame(const CdlodPatchFrame& frame, int childIndex);

    // Shortest distance from a point to the node as it renders, in body space.
    double distanceToNode(const CdlodPatchFrame& frame, const glm::dvec3& point) const;
};
