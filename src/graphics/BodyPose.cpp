// BodyPose.cpp
#include "BodyPose.h"
#include "SSBOManager.h"
#include <cstdint>
#include <glm/gtc/quaternion.hpp>

// Selection has to measure against the pose the geometry actually lands at, and
// the vertex stage has to undo exactly the placement made here, so this is where
// both are decided rather than a duplicate of a decision made in the shader.
//
// All double here, so unlike the mesh stages this needs no Dekker split:
// subtracting two world positions of similar magnitude is exact enough on its
// own. The camera is placed with the exact rotation and scale, so the
// planet-sized distance from the body's centre never meets a float. The vertex
// stage applies its float copies only to the vertex's offset from the camera,
// where their last bits cost a part in ten million of that offset.
BodyPose bodyRenderPose(const MeshTransform& transform, uint64_t time, double timeRemainder,
                        const glm::dvec3& camPos) {
    // Signed, so a body stamped with a time ahead of the frame's extrapolates
    // backwards instead of wrapping into an enormous forward step.
    const double stepDelta{static_cast<double>(
        static_cast<int64_t>(time) - static_cast<int64_t>(transform.time))};
    const double deltaTime{stepDelta + timeRemainder};

    const glm::dvec3 bodyPosition{transform.position + transform.velocity * deltaTime};
    const glm::dquat spin{
        glm::angleAxis(transform.angVel * deltaTime, transform.angVelAxis)};
    const glm::dmat3 bodyRotation{glm::mat3_cast(spin * transform.orientation)};
    const glm::dmat3 inverseBodyRotation{glm::transpose(bodyRotation)};

    // Undoes the vertex stage's steps in reverse: world offset, then the
    // rotation about the centre of rotation, then the scale.
    const glm::dvec3 relative{camPos - bodyPosition - transform.centerOfRotation};
    const glm::dvec3 rotated{inverseBodyRotation * relative};

    return BodyPose{bodyRotation, (rotated + transform.centerOfRotation) / transform.scale,
                    inverseBodyRotation, transform.scale};
}
