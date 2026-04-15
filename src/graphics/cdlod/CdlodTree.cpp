// CdlodTree.cpp
#include "CdlodTree.h"

namespace {

// Mirrors k_faceOrigin / k_faceUAxis / k_faceVAxis in cdlod_patch.glsl. The two
// must describe the same cube: this table decides where the CPU thinks a node
// is, that one decides where the vertex stage puts it, and a disagreement shows
// up as level-of-detail chosen for the wrong part of the body.
const glm::dvec3 k_faceOrigin[6]{
    { 1.0, -1.0, -1.0},  // +X
    {-1.0, -1.0, -1.0},  // -X
    {-1.0,  1.0, -1.0},  // +Y
    {-1.0, -1.0, -1.0},  // -Y
    {-1.0, -1.0,  1.0},  // +Z
    {-1.0, -1.0, -1.0}   // -Z
};
const glm::dvec3 k_faceUAxis[6]{
    {0.0, 2.0, 0.0}, {0.0, 0.0, 2.0}, {0.0, 0.0, 2.0},
    {2.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}
};
const glm::dvec3 k_faceVAxis[6]{
    {0.0, 0.0, 2.0}, {0.0, 2.0, 0.0}, {2.0, 0.0, 0.0},
    {0.0, 0.0, 2.0}, {0.0, 2.0, 0.0}, {2.0, 0.0, 0.0}
};

}  // namespace

CdlodTree::CdlodTree(const CdlodTreeParams& params)
    : m_params{params}, m_nodes(k_cubeFaceCount) {
}

int32_t CdlodTree::allocateChildren() {
    if (!m_freeBlocks.empty()) {
        const int32_t blockStart{m_freeBlocks.back()};
        m_freeBlocks.pop_back();
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
    for (int childIndex{0}; childIndex < k_childCount; ++childIndex) {
        const int32_t grandChildren{m_nodes[blockStart + childIndex].m_firstChild};
        if (grandChildren >= 0) {
            m_nodes[blockStart + childIndex].m_firstChild = -1;
            freeSubtree(grandChildren);
        }
    }
    m_freeBlocks.push_back(blockStart);
}

glm::dvec3 CdlodTree::projectedPoint(int faceIndex, const glm::dvec2& faceUv) const {
    const glm::dvec3 cubePoint{k_faceOrigin[faceIndex] + k_faceUAxis[faceIndex] * faceUv.x +
                               k_faceVAxis[faceIndex] * faceUv.y};
    // Mirrors cdlodDisplace in cdlod_patch.glsl.
    return glm::normalize(cubePoint) * m_params.m_halfExtent;
}

double CdlodTree::distanceToNode(int faceIndex, const glm::dvec2& offset, double size,
                                 const glm::dvec3& point) const {
    // A node is a patch of the sphere, so bound it with a sphere of its own:
    // centred on the projected patch centre and reaching its projected corners.
    // The corners are the points of a convex patch furthest from its centre, so
    // this encloses the whole patch. An axis-aligned box fits a curved patch
    // badly, which is what made it the wrong choice only while the body was flat.
    const glm::dvec3 centre{projectedPoint(faceIndex, offset + glm::dvec2{size * 0.5})};

    double radiusSquared{0.0};
    for (int cornerIndex{0}; cornerIndex < 4; ++cornerIndex) {
        const glm::dvec2 cornerUv{offset.x + (cornerIndex % 2) * size,
                                  offset.y + (cornerIndex / 2) * size};
        const glm::dvec3 toCorner{projectedPoint(faceIndex, cornerUv) - centre};
        radiusSquared = glm::max(radiusSquared, glm::dot(toCorner, toCorner));
    }

    return glm::max(0.0, glm::length(point - centre) - glm::sqrt(radiusSquared));
}

void CdlodTree::updateAndSelect(const glm::dvec3& cameraBodyPosition, int ssboIndex,
                                std::vector<CdlodPatchInstance>& instances) {
    instances.clear();
    for (int faceIndex{0}; faceIndex < k_cubeFaceCount; ++faceIndex) {
        // A face root's node index is its face index; see m_nodes.
        visitNode(faceIndex, faceIndex, glm::dvec2{0.0}, 1.0, 0,
                  cameraBodyPosition, ssboIndex, instances);
    }
}

void CdlodTree::visitNode(int32_t nodeIndex, int faceIndex, const glm::dvec2& offset,
                          double size, int depth, const glm::dvec3& cameraBodyPosition,
                          int ssboIndex, std::vector<CdlodPatchInstance>& instances) {
    // The distance at which this node's own resolution stops being enough,
    // derived from its world edge length so it halves in step with the nodes.
    // Measured before projection, which runs about a quarter long against the
    // arc the node actually covers; that splits marginally early, and the exact
    // halving per level is what the one-level neighbour bound depends on.
    const double nodeEdge{2.0 * m_params.m_halfExtent * size};
    const double splitRange{m_params.m_lodRangeFactor * nodeEdge};
    const double distance{distanceToNode(faceIndex, offset, size, cameraBodyPosition)};

    // Splitting and merging share one threshold, so the tree's shape is a pure
    // function of where the camera is rather than of how it got there. The band
    // of hysteresis this used to carry existed to stop a camera parked on the
    // threshold from flickering between levels; morphing makes a patch identical
    // to its parent by exactly that distance, so there is nothing left to see
    // flicker, and delaying the merge instead pushed it out to where the parent
    // had begun morphing on its own -- which does step visibly.
    const bool wantsChildren{depth < m_params.m_maxDepth && distance < splitRange};

    bool hasChildren{m_nodes[nodeIndex].m_firstChild >= 0};
    if (hasChildren && !wantsChildren) {
        const int32_t children{m_nodes[nodeIndex].m_firstChild};
        m_nodes[nodeIndex].m_firstChild = -1;
        freeSubtree(children);
        hasChildren = false;
    } else if (!hasChildren && wantsChildren &&
               instances.size() + k_childCount <= k_maxSelectedNodes) {
        // Running out of budget only blocks new splits. It never forces a merge,
        // which would free a subtree just to rebuild it on the next frame.
        const int32_t children{allocateChildren()};
        m_nodes[nodeIndex].m_firstChild = children;
        hasChildren = true;
    }

    if (!hasChildren) {
        instances.push_back(CdlodPatchInstance{
            glm::vec2{offset}, static_cast<float>(size), faceIndex, depth, ssboIndex});
        return;
    }

    const int32_t firstChild{m_nodes[nodeIndex].m_firstChild};
    const double childSize{size * 0.5};
    for (int childIndex{0}; childIndex < k_childCount; ++childIndex) {
        const glm::dvec2 childOffset{offset.x + (childIndex % 2) * childSize,
                                     offset.y + (childIndex / 2) * childSize};
        visitNode(firstChild + childIndex, faceIndex, childOffset, childSize, depth + 1,
                  cameraBodyPosition, ssboIndex, instances);
    }
}
