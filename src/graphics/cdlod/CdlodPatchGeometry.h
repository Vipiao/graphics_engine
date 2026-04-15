// CdlodPatchGeometry.h
#pragma once

#include <cstdint>
#include <vector>
#include <glad/glad.h>

/**
 * @brief The index buffer every CDLOD patch is drawn with.
 *
 * A patch is a fixed square grid of quads. Its vertices carry no attributes at
 * all: the vertex stage derives a grid coordinate from gl_VertexID, which under
 * glDrawElements is the value fetched from this buffer. So the indices are the
 * only per-vertex data that exists, they are built once here, and every node of
 * every body is drawn from them.
 *
 * One instance of this is shared by all bodies; a body binds the buffer into its
 * own VAO alongside its own instance attributes.
 */
class CdlodPatchGeometry {
public:
    // Quads along one patch edge, and the vertices that implies. The single
    // place this is decided: the vertex stage reads it back as a uniform rather
    // than keeping its own copy. 16-bit indices stay valid as long as the vertex
    // count is below 65536.
    //
    // It also sets how visible the surface's own approximation error is. A quad
    // spanning arc q on a body of radius R cuts the corner by about q^2 / 8R, and
    // q is inversely proportional to this, so the gap at a level boundary grows
    // with its square: on a 60 m body, 64 leaves millimetre seams where 8 would
    // leave tens of centimetres.
    //
    // Lowering it is the only way to make quads angularly coarser than about
    // 1 / (sqrt(2) * this), since the other half of that product,
    // CdlodConfig::m_lodRangeFactor, is floored at sqrt(2).
    static constexpr int k_patchQuads{64};
    static constexpr int k_patchVertices{k_patchQuads + 1};

    // Quads along the edge of one cache-locality block; see buildIndices.
    // Capped at the patch itself, so a small patch is simply one block and the
    // blocking quietly does nothing rather than failing to divide.
    static constexpr int k_blockQuads{k_patchQuads < 16 ? k_patchQuads : 16};
    static constexpr int k_blocksPerSide{k_patchQuads / k_blockQuads};

    static_assert(k_patchQuads % k_blockQuads == 0,
                  "The patch must divide evenly into blocks");
    static_assert(k_patchQuads % 2 == 0,
                  "Morphing slides odd-indexed vertices onto even ones, which "
                  "only halves the grid when the quad count is even");
    static_assert(k_patchVertices * k_patchVertices <= 65536,
                  "Patch vertices must be addressable by a 16-bit index");

    CdlodPatchGeometry();
    ~CdlodPatchGeometry();

    CdlodPatchGeometry(const CdlodPatchGeometry&) = delete;
    CdlodPatchGeometry& operator=(const CdlodPatchGeometry&) = delete;

    GLuint getIndexBuffer() const { return m_indexBuffer; }
    GLsizei getIndexCount() const { return m_indexCount; }

private:
    GLuint m_indexBuffer{0};
    GLsizei m_indexCount{0};

    // Fills the triangle list for one patch. The traversal order is chosen for
    // post-transform vertex reuse; the set of triangles it emits does not depend
    // on it, only the order they appear in.
    static void buildIndices(std::vector<uint16_t>& indices);
    // Two counter-clockwise triangles for the quad whose lower corner is at
    // (quadX, quadY), sharing the lower-left to upper-right diagonal.
    static void appendQuad(std::vector<uint16_t>& indices, int quadX, int quadY);
};
