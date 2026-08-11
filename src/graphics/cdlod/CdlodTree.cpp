// CdlodTree.cpp
#include "CdlodTree.h"
#include <cassert>
#include <cmath>
#include <utility>

CdlodTree::CdlodTree(const CdlodConfig& config, std::vector<CdlodPatchFrame> rootFrames)
    : m_config{config}, m_rootFrames{std::move(rootFrames)}, m_nodes(m_rootFrames.size()) {
    assert(!m_rootFrames.empty() &&
           "A tree with no roots selects nothing, and draws nothing, silently");
    assert(m_config.m_halfExtent > 0.0 && "A body needs a radius to project its patches onto");
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

glm::dvec3 CdlodTree::projectedPoint(const glm::dvec3& basePoint) const {
    // Projecting the body's centre has no answer, and normalizing it yields NaN
    // rather than saying so. A single root centred on the origin would hit this.
    assert(glm::dot(basePoint, basePoint) > 0.0 &&
           "A patch may not reach the body's centre: it has no outward direction there");

    // Mirrors cdlodPatchPoint in cdlod_patch.glsl, which projects the same frame
    // the same way.
    return glm::normalize(basePoint) * m_config.m_halfExtent;
}

double CdlodTree::distanceToNode(const CdlodPatchFrame& frame,
                                 const glm::dvec3& point) const {
    // A node is a patch of the sphere, so bound it with a sphere of its own:
    // centred on the projected patch centre and reaching its projected corners.
    // The corners are the points of a convex patch furthest from its centre, so
    // this encloses the whole patch. An axis-aligned box fits a curved patch
    // badly, which is what made it the wrong choice only while the body was flat.
    const glm::dvec3 centre{projectedPoint(frame.m_centre)};

    double radiusSquared{0.0};
    for (int cornerIndex{0}; cornerIndex < 4; ++cornerIndex) {
        const double uSign{(cornerIndex % 2) == 0 ? -1.0 : 1.0};
        const double vSign{(cornerIndex / 2) == 0 ? -1.0 : 1.0};
        const glm::dvec3 corner{
            frame.m_centre + frame.m_uAxis * uSign + frame.m_vAxis * vSign};
        const glm::dvec3 toCorner{projectedPoint(corner) - centre};
        radiusSquared = glm::max(radiusSquared, glm::dot(toCorner, toCorner));
    }

    return glm::max(0.0, glm::length(point - centre) - glm::sqrt(radiusSquared));
}

void CdlodTree::updateAndSelect(const glm::dvec3& cameraBodyPosition,
                                std::vector<CdlodPatch>& patches) {
    // Every split test compares against this, and every comparison with a NaN is
    // false, so a non-finite camera does not fail: it quietly holds the whole body
    // at its roots. Caught here, at the boundary, rather than at the symptom.
    assert(std::isfinite(cameraBodyPosition.x) && std::isfinite(cameraBodyPosition.y) &&
           std::isfinite(cameraBodyPosition.z) &&
           "Non-finite camera position: the body would stay at its roots forever");

    // Counted from where this body's own selection starts, so sharing the buffer
    // with other bodies does not spend their nodes out of this body's budget.
    m_selectionCeiling = patches.size() + k_maxSelectedNodes;

    for (size_t rootIndex{0}; rootIndex < m_rootFrames.size(); ++rootIndex) {
        // A root's node index is its position in m_rootFrames; see m_nodes.
        visitNode(static_cast<int32_t>(rootIndex), m_rootFrames[rootIndex], 0,
                  cameraBodyPosition, patches);
    }
}

void CdlodTree::visitNode(int32_t nodeIndex, const CdlodPatchFrame& frame, int depth,
                          const glm::dvec3& cameraBodyPosition,
                          std::vector<CdlodPatch>& patches) {
    assert(nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < m_nodes.size() &&
           "Traversing a node that is not in the pool");
    assert(depth <= k_maxDepth && "Traversal went deeper than the depth limit allows");

    // The distance at which this node's own resolution stops being enough,
    // derived from its edge length so it halves in step with the nodes. Measured
    // before projection, which runs about a quarter long against the arc the node
    // actually covers; that splits marginally early, and the exact halving per
    // level is what the one-level neighbour bound depends on.
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
               patches.size() + k_childCount <= m_selectionCeiling) {
        // Running out of budget only blocks new splits. It never forces a merge,
        // which would free a subtree just to rebuild it on the next frame, and it
        // never stops a leaf being emitted -- so the selection can pass the
        // ceiling by whatever the tree already held. See k_maxSelectedNodes.
        const int32_t children{allocateChildren()};
        m_nodes[nodeIndex].m_firstChild = children;
        hasChildren = true;
    }

    if (!hasChildren) {
        patches.push_back(CdlodPatch{glm::vec3{frame.m_centre}, glm::vec3{frame.m_uAxis},
                                     glm::vec3{frame.m_vAxis}, depth, -1});
        return;
    }

    const int32_t firstChild{m_nodes[nodeIndex].m_firstChild};
    for (int childIndex{0}; childIndex < k_childCount; ++childIndex) {
        visitNode(firstChild + childIndex, childFrame(frame, childIndex), depth + 1,
                  cameraBodyPosition, patches);
    }
}
