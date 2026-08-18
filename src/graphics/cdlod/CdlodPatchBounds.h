// CdlodPatchBounds.h
#pragma once

#include <glm/glm.hpp>

// Where one patch sits, in the body's own frame and in metres: a centre and two
// half-edge axes, so the patch spans centre +- uAxis +- vAxis and its corners are
// the four sign combinations. Splitting halves both axes and steps the centre by
// half of each, which is all the renderer needs to know about shape. What solid
// the roots came off is the caller's, and is never asked.
struct CdlodPatchFrame {
    glm::dvec3 m_centre{0.0};
    glm::dvec3 m_uAxis{0.0};
    glm::dvec3 m_vAxis{0.0};
};

// A sphere containing a patch as it renders, in the body's own frame and in
// metres, and how the frame scaled on its way there.
struct CdlodPatchBounds {
    glm::dvec3 m_centre{0.0};
    // Metres, measured where the patch is drawn, so the shape's compression is
    // already in it. Scaling by m_frameScale would apply that twice.
    double m_radius{0.0};
    // Drawn size over frame size, dimensionless. The radius above already carries
    // it; the frame's axes do not, and the renderer sizes patches by those.
    //
    // Must be honest, unlike the radius: the level and the morph are chosen by it,
    // so neighbours agreeing here is what closes the seam between them. Left at
    // one, patches are sized by their frames and only detail suffers.
    double m_frameScale{1.0};
};

/**
 * @brief Where a body's patches land once drawn.
 *
 * The one thing the renderer asks about a body's geometry: it subdivides frames
 * without knowing what solid they came off and draws them through a snippet it
 * does not read, so this is what turns a square into somewhere measurable.
 *
 * The bound must contain every point of the patch as the snippet draws it,
 * displacement included. Too small reopens the seams morphing exists to close;
 * too large only costs triangles.
 *
 * Nothing else is required of the sphere. The shape may stretch, fold or
 * displace by any amount, and may bound itself however it likes.
 *
 * A body that compresses its frames unevenly should say so through m_frameScale,
 * or the renderer sizes its patches by frames that no longer describe them.
 */
class ICdlodPatchBounds {
public:
    virtual ~ICdlodPatchBounds() = default;

    virtual CdlodPatchBounds patchBounds(const CdlodPatchFrame& frame) const = 0;
};
