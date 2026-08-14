// CdlodTree.cpp
#include "CdlodTree.h"
#include <cassert>
#include <cmath>
#include <utility>

CdlodTree::CdlodTree(const CdlodConfig& config, std::vector<CdlodPatchFrame> rootFrames,
                     std::shared_ptr<const ICdlodPatchBounds> bounds)
    : m_config{config}, m_rootFrames{std::move(rootFrames)},
      m_bounds{std::move(bounds)}, m_nodes(m_rootFrames.size()) {
    assert(!m_rootFrames.empty() &&
           "A tree with no roots selects nothing, and draws nothing, silently");
    assert(m_bounds && "Without bounds a patch has no place, and no distance to measure");
    // The bound this rests on is derived in CdlodConfig: at or below sqrt(2) a
    // neighbour two levels finer becomes reachable and the cracks double.
    assert(m_config.m_lodRangeFactor > std::sqrt(2.0) &&
           "lodRangeFactor at or below sqrt(2) allows two-level seams");

    for (const CdlodPatchFrame& frame : m_rootFrames) {
        const double uLength{glm::length(frame.m_uAxis)};
        const double vLength{glm::length(frame.m_vAxis)};
        assert(uLength > 0.0 && vLength > 0.0 &&
               "A root with a zero axis has no edge length, so it can never split");

        // The split range here and the morph in cdlod_patch.glsl both take the
        // patch's size from its u axis alone, which describes the patch only while
        // the two axes match. Splitting halves both, so a root that is not square
        // stays that way all the way down, and its detail would be chosen for a
        // size it does not have.
        assert(glm::abs(uLength - vLength) <=
                   k_squareTolerance * glm::max(uLength, vLength) &&
               "Patch frames must be square: size is measured along the u axis alone");
    }
}

int32_t CdlodTree::allocateChildren() {
    if (!m_freeBlocks.empty()) {
        const int32_t blockStart{m_freeBlocks.back()};
        m_freeBlocks.pop_back();
        assert(blockStart >= 0 &&
               static_cast<size_t>(blockStart) + k_childCount <= m_nodes.size() &&
               "The free list holds a block that is not in the pool");

        for (int childIndex{0}; childIndex < k_childCount; ++childIndex) {
            m_nodes[blockStart + childIndex].m_firstChild = -1;
        }
        return blockStart;
    }

    const int32_t blockStart{static_cast<int32_t>(m_nodes.size())};
    m_nodes.resize(m_nodes.size() + k_childCount);
    return blockStart;
}

void CdlodTree::freeSubtree(int32_t blockStart) {
    assert(blockStart >= 0 &&
           static_cast<size_t>(blockStart) + k_childCount <= m_nodes.size() &&
           "Recycling a block that is not in the pool");
    assert(static_cast<size_t>(blockStart) >= m_rootFrames.size() &&
           "Roots are nobody's children and must never be recycled");

    for (int childIndex{0}; childIndex < k_childCount; ++childIndex) {
        const int32_t grandChildren{m_nodes[blockStart + childIndex].m_firstChild};
        if (grandChildren >= 0) {
            m_nodes[blockStart + childIndex].m_firstChild = -1;
            freeSubtree(grandChildren);
        }
    }
    m_freeBlocks.push_back(blockStart);
}

CdlodPatchFrame CdlodTree::childFrame(const CdlodPatchFrame& frame, int childIndex) {
    const glm::dvec3 uHalf{frame.m_uAxis * 0.5};
    const glm::dvec3 vHalf{frame.m_vAxis * 0.5};
    const double uSign{(childIndex % 2) == 0 ? -1.0 : 1.0};
    const double vSign{(childIndex / 2) == 0 ? -1.0 : 1.0};

    return CdlodPatchFrame{frame.m_centre + uHalf * uSign + vHalf * vSign, uHalf, vHalf};
}

double CdlodTree::distanceToNode(const CdlodPatchFrame& frame,
                                 const glm::dvec3& point) const {
    // The bound contains the patch as drawn, so subtracting its radius gives a
    // distance no point of it is nearer than -- which is what keeps neighbouring
    // leaves within one level of each other.
    const CdlodPatchBounds bounds{m_bounds->patchBounds(frame)};

    return glm::max(0.0, glm::length(point - bounds.m_centre) - bounds.m_radius);
}

void CdlodTree::updateAndSelect(const glm::dvec3& cameraBodyPosition,
                                std::vector<CdlodLeaf>& leaves) {
    // Every split test compares against this, and every comparison with a NaN is
    // false, so a non-finite camera does not fail: it quietly holds the whole body
    // at its roots. Caught here, at the boundary, rather than at the symptom.
    assert(std::isfinite(cameraBodyPosition.x) && std::isfinite(cameraBodyPosition.y) &&
           std::isfinite(cameraBodyPosition.z) &&
           "Non-finite camera position: the body would stay at its roots forever");

    // Counted from where this body's own selection starts, so sharing the buffer
    // with other bodies does not spend their nodes out of this body's budget.
    m_selectionCeiling = leaves.size() + k_maxSelectedNodes;

    for (size_t rootIndex{0}; rootIndex < m_rootFrames.size(); ++rootIndex) {
        // A root's node index is its position in m_rootFrames; see m_nodes.
        visitNode(static_cast<int32_t>(rootIndex), m_rootFrames[rootIndex], 0,
                  cameraBodyPosition, leaves);
    }
}

void CdlodTree::visitNode(int32_t nodeIndex, const CdlodPatchFrame& frame, int depth,
                          const glm::dvec3& cameraBodyPosition,
                          std::vector<CdlodLeaf>& leaves) {
    assert(nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < m_nodes.size() &&
           "Traversing a node that is not in the pool");
    assert(depth <= k_maxDepth && "Traversal went deeper than the depth limit allows");

    // The distance at which this node's resolution stops being enough, taken from
    // the frame rather than from where it renders: splitting halves the axes
    // exactly, and that exact halving is what the one-level neighbour bound needs.
    const double nodeEdge{2.0 * glm::length(frame.m_uAxis)};
    const double splitRange{m_config.m_lodRangeFactor * nodeEdge};
    const double distance{distanceToNode(frame, cameraBodyPosition)};

    // Splitting and merging share one threshold, so the tree's shape is a pure
    // function of where the camera is rather than of how it got there. The band
    // of hysteresis this used to carry existed to stop a camera parked on the
    // threshold from flickering between levels; morphing makes a patch identical
    // to its parent by exactly that distance, so there is nothing left to see
    // flicker, and delaying the merge instead pushed it out to where the parent
    // had begun morphing on its own -- which does step visibly.
    const bool wantsChildren{depth < k_maxDepth && distance < splitRange};

    bool hasChildren{m_nodes[nodeIndex].m_firstChild >= 0};
    if (hasChildren && !wantsChildren) {
        const int32_t children{m_nodes[nodeIndex].m_firstChild};
        m_nodes[nodeIndex].m_firstChild = -1;
        freeSubtree(children);
        hasChildren = false;
    } else if (!hasChildren && wantsChildren &&
               leaves.size() + k_childCount <= m_selectionCeiling) {
        // Running out of budget only blocks new splits. It never forces a merge,
        // which would free a subtree just to rebuild it on the next frame, and it
        // never stops a leaf being emitted -- so the selection can pass the
        // ceiling by whatever the tree already held. See k_maxSelectedNodes.
        const int32_t children{allocateChildren()};
        m_nodes[nodeIndex].m_firstChild = children;
        hasChildren = true;
    }

    if (!hasChildren) {
        leaves.push_back(CdlodLeaf{frame, depth});
        return;
    }

    const int32_t firstChild{m_nodes[nodeIndex].m_firstChild};
    for (int childIndex{0}; childIndex < k_childCount; ++childIndex) {
        visitNode(firstChild + childIndex, childFrame(frame, childIndex), depth + 1,
                  cameraBodyPosition, leaves);
    }
}
