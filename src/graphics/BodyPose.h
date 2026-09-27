// BodyPose.h
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct MeshTransform;

// Where a body stands this frame, and where the camera stands in its frame.
//
// The rotation is held at the width it will be uploaded at, and the camera is
// placed by inverting that narrowed rotation rather than the exact one. The
// vertex stage undoes the placement with the same matrix, so what it undoes is
// what was done: the body's world position drops out exactly instead of leaving
// a part in ten million of the camera's distance from the body's centre, which
// on a planet is most of a metre and moves whenever the body turns.
struct BodyPose {
    glm::dmat3 m_bodyRotation{1.0};
    glm::dvec3 m_cameraBodyPosition{0.0};
    // The inverse rather than the transpose, for the reason bodyRenderPose gives
    // where it takes it; kept because that is where it was already paid for. Takes
    // a camera-relative direction into the body's own frame.
    glm::dmat3 m_inverseBodyRotation{1.0};
    // The body's scale, so a length carried into that frame can be divided by what
    // the vertex stage will multiply it back by.
    glm::dvec3 m_scale{1.0};
};

// Rebuilds the interpolated pose the body will be drawn at this frame and puts
// the camera into that body's own frame, the same pose every renderer placing
// something on the body measures against. time is the frame's physics tick,
// timeRemainder how far into the next one it is, camPos the camera in world space.
BodyPose bodyRenderPose(const MeshTransform& transform, uint64_t time, double timeRemainder,
                        const glm::dvec3& camPos);
