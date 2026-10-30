// CasterTiers.cpp
#include "CasterTiers.h"
#include <cassert>
#include <glm/gtc/epsilon.hpp>

Cylinder casterVolumeInBodySpace(const Cylinder& volume, const BodyPose& pose) {
    assert(glm::all(glm::epsilonEqual(pose.m_scale, glm::dvec3{pose.m_scale.x}, 1e-12)) &&
           "Non-uniform scale: a bounding sphere would not stay one in the body's frame");
    const double scale{pose.m_scale.x};

    return Cylinder{glm::normalize(pose.m_inverseBodyRotation * volume.m_axis),
                    pose.m_cameraBodyPosition +
                        (pose.m_inverseBodyRotation * volume.m_centre) / scale,
                    volume.m_radius / scale, volume.m_axialMin / scale,
                    volume.m_axialMax / scale};
}

unsigned int casterTier(const std::vector<Cylinder>& bodyVolumes, const glm::dvec3& centre,
                        double radius) {
    for (size_t tier{0}; tier < bodyVolumes.size(); ++tier) {
        if (intersectsSphere(bodyVolumes[tier], centre, radius)) {
            return static_cast<unsigned int>(tier);
        }
    }
    return static_cast<unsigned int>(bodyVolumes.size());
}
