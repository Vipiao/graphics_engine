// CdlodTree.h
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "CdlodConfig.h"
#include "CdlodPatchBounds.h"

// One leaf of a finished traversal: the square it covers, and how deep it sits.
// A node is the tree's own pool entry and persists between frames; a leaf is what
// one traversal decided to draw.
//
// Carries the frame whole, so a leaf can be handed straight back to the bounds it
// was measured against, and in double, since narrowing it is the consumer's
// choice rather than the tree's.
struct CdlodLeaf {
    CdlodPatchFrame m_frame{};
    int m_level{0};  // 0 = root; depth in the quadtree
    // Carried out so the vertex stage sizes the patch the way the split test did;
    // recovering it downstream would mean bounding the patch again.
    double m_frameScale{1.0};
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
 * Knows nothing about shapes, renderers or GL. Where a leaf ends up is asked of
 * the caller's ICdlodPatchBounds, so the only geometry here is the frames' own:
 * square, and halved by splitting.
 */
class CdlodTree {
public:
    // One root per patch of the solid being subdivided, already in metres: six
    // for a cube, one for a flat patch. The tree neither knows nor cares which.
    CdlodTree(const CdlodConfig& config, std::vector<CdlodPatchFrame> rootFrames,
              std::shared_ptr<const ICdlodPatchBounds> bounds);

    // Splits, merges, and appends what is left. Appends rather than clears, so
    // the bodies sharing a shader can select into one buffer and be drawn
    // together. cameraBodyPosition is the camera in the body's own frame, at the
    // same interpolated pose the vertex stage will draw the body at.
    void updateAndSelect(const glm::dvec3& cameraBodyPosition,
                         std::vector<CdlodLeaf>& leaves);

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
    // camera that reaches the surface itself. A guard on the recursion rather
    // than a quality setting -- a root halved this many times is a 524288th of
    // the body it came off.
    static constexpr int k_maxDepth{19};
    // How far a root's two axes may differ in length, relative to the longer, and
    // still count as square. Only rounding is expected: a cube's faces match
    // exactly, and splitting halves both axes together.
    static constexpr double k_squareTolerance{1e-9};

    // Pool entry. Frame and depth are derived on the way down, since a child's is
    // a halving of its parent's. The bounds are kept instead: asking the body
    // costs a surface evaluation, the traversal asks once per node per frame, and
    // the answer is fixed by where the node sits. Only recycling invalidates it.
    struct CdlodNode {
        int32_t m_firstChild{-1};  // first of four contiguous children, -1 = leaf
        CdlodPatchBounds m_bounds{};
        bool m_boundsKnown{false};
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
    // Leaf count this traversal may not exceed. Set on entry to updateAndSelect
    // and read only while it runs.
    size_t m_selectionCeiling{0};

    int32_t allocateChildren();
    void freeSubtree(int32_t blockStart);

    void visitNode(int32_t nodeIndex, const CdlodPatchFrame& frame, int depth,
                   const glm::dvec3& cameraBodyPosition,
                   std::vector<CdlodLeaf>& leaves);

    // The quarter of a frame a child occupies: both axes halved, the centre
    // stepped into one of the four corners.
    static CdlodPatchFrame childFrame(const CdlodPatchFrame& frame, int childIndex);

    // Where the node lands once drawn, off the pool entry if it has been asked
    // before and out of the body if not.
    const CdlodPatchBounds& nodeBounds(int32_t nodeIndex, const CdlodPatchFrame& frame);
};
