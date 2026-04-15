// CdlodPatchGeometry.cpp
#include "CdlodPatchGeometry.h"

CdlodPatchGeometry::CdlodPatchGeometry() {
    std::vector<uint16_t> indices;
    indices.reserve(static_cast<size_t>(k_patchQuads) * k_patchQuads * 6);
    buildIndices(indices);

    m_indexCount = static_cast<GLsizei>(indices.size());

    glGenBuffers(1, &m_indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

CdlodPatchGeometry::~CdlodPatchGeometry() {
    if (m_indexBuffer != 0) {
        glDeleteBuffers(1, &m_indexBuffer);
    }
}

void CdlodPatchGeometry::appendQuad(std::vector<uint16_t>& indices, int quadX, int quadY) {
    const uint16_t lowerLeft{static_cast<uint16_t>(quadY * k_patchVertices + quadX)};
    const uint16_t lowerRight{static_cast<uint16_t>(lowerLeft + 1)};
    const uint16_t upperLeft{static_cast<uint16_t>(lowerLeft + k_patchVertices)};
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

void CdlodPatchGeometry::buildIndices(std::vector<uint16_t>& indices) {
    // A patch row is 65 vertices wide, far more than the hardware keeps for
    // post-transform reuse, so sweeping full rows re-shades most of the shared
    // row every time. Two things fix that, and both cost nothing at draw time:
    //
    // Blocks. Quads are emitted in k_blockQuads-wide blocks, narrow enough that
    // a whole shared vertex row stays resident between one quad row and the next.
    //
    // Boustrophedon. Every alternating step reverses direction, so a traversal
    // never jumps back to the far edge of what it just walked. Within a block the
    // x direction flips each row, which consumes the shared row in the order the
    // previous row touched it -- exactly the order it would be evicted in. Across
    // blocks the y direction flips each column, and since a block's last vertex
    // row is literally the next block's first, the turn into the block below is
    // free.
    bool xForward{true};
    for (int blockX{0}; blockX < k_blocksPerSide; ++blockX) {
        const bool yForward{blockX % 2 == 0};
        for (int blockStep{0}; blockStep < k_blocksPerSide; ++blockStep) {
            const int blockY{yForward ? blockStep : k_blocksPerSide - 1 - blockStep};

            for (int rowStep{0}; rowStep < k_blockQuads; ++rowStep) {
                const int localY{yForward ? rowStep : k_blockQuads - 1 - rowStep};
                const int quadY{blockY * k_blockQuads + localY};

                for (int columnStep{0}; columnStep < k_blockQuads; ++columnStep) {
                    const int localX{xForward ? columnStep : k_blockQuads - 1 - columnStep};
                    appendQuad(indices, blockX * k_blockQuads + localX, quadY);
                }
                xForward = !xForward;
            }
        }
    }
}
