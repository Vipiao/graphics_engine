// CdlodPatchGeometry.cpp
#include "CdlodPatchGeometry.h"
#include <stdexcept>
#include <string>

CdlodPatchGeometry::CdlodPatchGeometry(int patchQuads) {
    glGenBuffers(1, &m_indexBuffer);
    setPatchQuads(patchQuads);
}

CdlodPatchGeometry::~CdlodPatchGeometry() {
    if (m_indexBuffer != 0) {
        glDeleteBuffers(1, &m_indexBuffer);
    }
}

void CdlodPatchGeometry::setPatchQuads(int patchQuads) {
    if (patchQuads < 2 || patchQuads > k_maxPatchQuads) {
        throw std::runtime_error(
            "CdlodPatchGeometry: patch quads must be between 2 and "
            + std::to_string(k_maxPatchQuads) + ", got " + std::to_string(patchQuads));
    }
    if (patchQuads % 2 != 0) {
        throw std::runtime_error(
            "CdlodPatchGeometry: patch quads must be even, since morphing slides "
            "odd-indexed vertices onto even ones; got " + std::to_string(patchQuads));
    }

    m_patchQuads = patchQuads;

    std::vector<uint16_t> indices;
    indices.reserve(static_cast<size_t>(m_patchQuads) * m_patchQuads * 6);
    buildIndices(indices);

    m_indexCount = static_cast<GLsizei>(indices.size());

    // The element buffer binding belongs to whichever vertex array is bound, so
    // filling this with a surface's array bound would repoint its indices.
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

int CdlodPatchGeometry::blockQuads() const {
    const int widest{m_patchQuads < k_maxBlockQuads ? m_patchQuads : k_maxBlockQuads};
    for (int candidate{widest}; candidate > 1; --candidate) {
        if (m_patchQuads % candidate == 0) {
            return candidate;
        }
    }
    return 1;
}

void CdlodPatchGeometry::appendQuad(std::vector<uint16_t>& indices, int quadX,
                                    int quadY) const {
    const int patchVertices{getPatchVertices()};
    const uint16_t lowerLeft{static_cast<uint16_t>(quadY * patchVertices + quadX)};
    const uint16_t lowerRight{static_cast<uint16_t>(lowerLeft + 1)};
    const uint16_t upperLeft{static_cast<uint16_t>(lowerLeft + patchVertices)};
    const uint16_t upperRight{static_cast<uint16_t>(upperLeft + 1)};

    // Counter-clockwise in the patch's (u, v) space. The face basis maps +u
    // cross +v onto the outward normal, so this is counter-clockwise seen from
    // outside the body, which is what GL's default front-face winding wants.
    // Reordering the traversal below must never reorder these three: that would
    // flip the facing and the triangle would be culled away.
    indices.push_back(lowerLeft);
    indices.push_back(lowerRight);
    indices.push_back(upperRight);

    indices.push_back(lowerLeft);
    indices.push_back(upperRight);
    indices.push_back(upperLeft);
}

void CdlodPatchGeometry::buildIndices(std::vector<uint16_t>& indices) const {
    // A patch row runs to tens of vertices, far more than the hardware keeps for
    // post-transform reuse, so sweeping full rows re-shades most of the shared
    // row every time. Two things fix that, and both cost nothing at draw time:
    //
    // Blocks. Quads are emitted in blockQuads-wide blocks, narrow enough that a
    // whole shared vertex row stays resident between one quad row and the next.
    //
    // Boustrophedon. Every alternating step reverses direction, so a traversal
    // never jumps back to the far edge of what it just walked. Within a block the
    // x direction flips each row, which consumes the shared row in the order the
    // previous row touched it -- exactly the order it would be evicted in. Across
    // blocks the y direction flips each column, and since a block's last vertex
    // row is literally the next block's first, the turn into the block below is
    // free.
    const int blockSide{blockQuads()};
    const int blocksPerSide{m_patchQuads / blockSide};

    bool xForward{true};
    for (int blockX{0}; blockX < blocksPerSide; ++blockX) {
        const bool yForward{blockX % 2 == 0};
        for (int blockStep{0}; blockStep < blocksPerSide; ++blockStep) {
            const int blockY{yForward ? blockStep : blocksPerSide - 1 - blockStep};

            for (int rowStep{0}; rowStep < blockSide; ++rowStep) {
                const int localY{yForward ? rowStep : blockSide - 1 - rowStep};
                const int quadY{blockY * blockSide + localY};

                for (int columnStep{0}; columnStep < blockSide; ++columnStep) {
                    const int localX{xForward ? columnStep : blockSide - 1 - columnStep};
                    appendQuad(indices, blockX * blockSide + localX, quadY);
                }
                xForward = !xForward;
            }
        }
    }
}
