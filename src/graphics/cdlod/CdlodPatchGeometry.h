// CdlodPatchGeometry.h
#pragma once

#include <cstdint>
#include <vector>
#include <glad/glad.h>

/**
 * @brief The index buffer every CDLOD patch is drawn with.
 *
 * A patch is a square grid of quads. Its vertices carry no attributes at all:
 * the vertex stage derives a grid coordinate from gl_VertexID, which under
 * glDrawElements is the value fetched from this buffer. So the indices are the
 * only per-vertex data that exists, they are built once here, and every node of
 * every body is drawn from them.
 *
 * One instance of this is shared by all bodies; a body binds the buffer into its
 * own VAO alongside its own instance attributes. The grid size is therefore one
 * decision for the whole engine rather than a property of a body.
 */
class CdlodPatchGeometry {
public:
    // Quads along one patch edge unless the caller asks for another size.
    static constexpr int k_defaultPatchQuads{64};
    // A patch's vertices are addressed by 16-bit indices, and the count must be
    // even, so this is the largest grid that can be indexed at all.
    static constexpr int k_maxPatchQuads{254};

    explicit CdlodPatchGeometry(int patchQuads = k_defaultPatchQuads);
    ~CdlodPatchGeometry();

    CdlodPatchGeometry(const CdlodPatchGeometry&) = delete;
    CdlodPatchGeometry& operator=(const CdlodPatchGeometry&) = delete;

    /**
     * @brief Rebuilds the indices at a new grid size.
     *
     * Refills the existing buffer, so vertex arrays that already bound it stay
     * correct and no surface has to be told anything.
     *
     * The size sets the surface's own approximation error: a quad spanning arc q
     * on a body of radius R cuts the corner by about q^2 / 8R, and q is inversely
     * proportional to this, so seams at a level boundary grow with its square. On
     * a 60 m body 64 leaves millimetre seams where 8 leaves tens of centimetres;
     * a planet carries the same angular quad at far less error.
     *
     * Lowering it buys less than it looks like: quartering a patch's triangles
     * lets the tree split one level deeper to hold the same angular quad size.
     *
     * Must be even, since morphing slides odd-indexed vertices onto even ones.
     * Throws if it cannot be used.
     */
    void setPatchQuads(int patchQuads);

    int getPatchQuads() const { return m_patchQuads; }
    // Vertices along one patch edge. The vertex stage reads this back as a
    // uniform rather than keeping a copy that could disagree with the indices.
    int getPatchVertices() const { return m_patchQuads + 1; }

    GLuint getIndexBuffer() const { return m_indexBuffer; }
    GLsizei getIndexCount() const { return m_indexCount; }

private:
    // Widest block the traversal will use; see buildIndices. A patch narrower
    // than this is simply one block.
    static constexpr int k_maxBlockQuads{16};

    GLuint m_indexBuffer{0};
    GLsizei m_indexCount{0};
    int m_patchQuads{k_defaultPatchQuads};

    // Quads along the edge of one cache-locality block: the widest that divides
    // the patch evenly, so the grid need not be a multiple of a fixed width.
    int blockQuads() const;

    // Fills the triangle list for one patch. The traversal order is chosen for
    // post-transform vertex reuse; the set of triangles it emits does not depend
    // on it, only the order they appear in.
    void buildIndices(std::vector<uint16_t>& indices) const;
    // Two counter-clockwise triangles for the quad whose lower corner is at
    // (quadX, quadY), sharing the lower-left to upper-right diagonal.
    void appendQuad(std::vector<uint16_t>& indices, int quadX, int quadY) const;
};
