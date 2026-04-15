// CdlodTree.h
#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

// One selected quadtree node, as the vertex stage consumes it. The node is
// located entirely in its cube face's own [0, 1] square, so a patch vertex maps
// to the body with nothing but this record and a table of face bases.
struct CdlodPatchInstance {
    glm::vec2 m_nodeOffset{0.0f};  // node's lower corner in face-local space
    float m_nodeSize{1.0f};        // node's edge length in face-local space
    int32_t m_faceIndex{0};        // 0..5, selects the cube face basis
    int32_t m_nodeLevel{0};        // 0 = root; depth in the quadtree
    int32_t m_ssboIndex{-1};       // body world transform, shared with the SSBO
};

struct CdlodTreeParams {
    double m_halfExtent{100.0};
    int m_maxDepth{0};
    double m_lodRangeFactor{2.0};
};

/**
 * @brief The subdivision quadtree of one body: six face roots over a node pool.
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
 * Holds no GL state and knows nothing about the renderer.
 */
class CdlodTree {
public:
    explicit CdlodTree(const CdlodTreeParams& params);

    // Splits, merges, and appends the resulting leaves to instances, which is
    // cleared first. cameraBodyPosition is the camera in the body's own frame,
    // at the same interpolated pose the vertex stage will draw the body at.
    void updateAndSelect(const glm::dvec3& cameraBodyPosition, int ssboIndex,
                         std::vector<CdlodPatchInstance>& instances);

private:
    static constexpr int k_cubeFaceCount{6};
    static constexpr int k_childCount{4};
    // A ceiling on how much one body may select. Reaching it stops further
    // splitting, so an overrun costs detail rather than punching holes in the
    // surface. A guard against pathological cases, not a budget to plan against.
    static constexpr size_t k_maxSelectedNodes{8192};

    // Pool entry. Everything else about a node -- face, offset, size, depth --
    // is derived on the way down, since a child's is a halving of its parent's.
    struct CdlodNode {
        int32_t m_firstChild{-1};  // first of four contiguous children, -1 = leaf
    };

    CdlodTreeParams m_params{};
    // The first k_cubeFaceCount entries are the face roots, so a root's node
    // index is its face index.
    std::vector<CdlodNode> m_nodes;
    // Start indices of free four-node blocks. Index-based and LIFO, so reuse
    // order is reproducible.
    std::vector<int32_t> m_freeBlocks;

    int32_t allocateChildren();
    void freeSubtree(int32_t blockStart);

    void visitNode(int32_t nodeIndex, int faceIndex, const glm::dvec2& offset,
                   double size, int depth, const glm::dvec3& cameraBodyPosition,
                   int ssboIndex, std::vector<CdlodPatchInstance>& instances);

    // A face-space coordinate placed on the displaced surface, in body space.
    glm::dvec3 projectedPoint(int faceIndex, const glm::dvec2& faceUv) const;

    // Shortest distance from a point to the node's bounds, in body space.
    double distanceToNode(int faceIndex, const glm::dvec2& offset, double size,
                          const glm::dvec3& point) const;
};
