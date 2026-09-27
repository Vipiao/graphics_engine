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
// own. What is narrowed is the rotation, and deliberately: it is inverted at the
// width it will be uploaded at, so the shader's forward rotation and this
// backward one are true inverses. Inverting the exact rotation instead would
// leave them a part in ten million apart, and a part in ten million of the
// camera's distance from a planet's centre is most of a metre.
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
    const glm::dmat3 bodyRotation{glm::mat3{glm::mat3_cast(spin * transform.orientation)}};

    // Scale comes back from float too: the vertex stage reads the copy in the
    // shared SSBO, and a scale that differed in its last bit would tilt the
    // whole cancellation by that much of the body's radius.
    const glm::dvec3 scale{glm::vec3{transform.scale}};

    // Undoes the vertex stage's steps in reverse: world offset, then the
    // rotation about the centre of rotation, then the scale. The centre of
    // rotation appears on both sides of that stage and cancels, so its own width
    // never enters.
    const glm::dvec3 relative{camPos - bodyPosition - transform.centerOfRotation};
    // A true inverse, not a transpose: narrowing to float leaves the rotation a
    // part in ten million off orthonormal, worth half a metre of camera position
    // at a planet's radius.
    const glm::dmat3 inverseBodyRotation{glm::inverse(bodyRotation)};
    const glm::dvec3 rotated{inverseBodyRotation * relative};

    return BodyPose{bodyRotation, (rotated + transform.centerOfRotation) / scale,
                    inverseBodyRotation, scale};
}
