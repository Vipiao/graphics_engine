// CasterTiers.h
#pragma once

#include <cstddef>
#include <vector>
#include <glm/glm.hpp>
#include "BodyPose.h"
#include "math/Cylinder.h"

// Grouping a handler's pieces by the shadow cascades they can cast into, so
// cascade k draws a prefix: tiers 0 through k. The volumes are
// ShadowRenderer::getCasterVolumes, handed over by whoever owns both.

// A caster volume as the body's own frame sees it. Few volumes against many
// pieces, so it is the volumes that are carried across rather than the bounds.
Cylinder casterVolumeInBodySpace(const Cylinder& volume, const BodyPose& pose);

// The innermost volume a sphere can cast into, or one past the last if it casts
// into none. They only nearly nest -- a shared caster reach runs from centres that
// do not coincide -- so every volume is tested, not just the outermost.
unsigned int casterTier(const std::vector<Cylinder>& bodyVolumes, const glm::dvec3& centre,
                        double radius);

// Pieces with every tier's together, innermost first, and where each tier's block
// ends: the count a draw of tiers 0 through k asks for.
template <typename T>
struct TierGrouping {
    std::vector<T> m_pieces;
    std::vector<size_t> m_tierEnds;
};

// tiers is parallel to pieces, tierCount one more than the volumes. Counting sort,
// so the result is fixed by the tiers alone.
template <typename T>
TierGrouping<T> groupByTier(const std::vector<T>& pieces, const std::vector<unsigned int>& tiers,
                            size_t tierCount) {
    std::vector<size_t> cursor(tierCount, 0);
    for (unsigned int tier : tiers) {
        ++cursor[tier];
    }

    // The running sum leaves the cursor at each tier's block start and the end at
    // its end
    TierGrouping<T> grouped{std::vector<T>(pieces.size()), std::vector<size_t>(tierCount, 0)};
    size_t start{0};
    for (size_t tier{0}; tier < tierCount; ++tier) {
        const size_t count{cursor[tier]};
        cursor[tier] = start;
        start += count;
        grouped.m_tierEnds[tier] = start;
    }

    for (size_t piece{0}; piece < pieces.size(); ++piece) {
        grouped.m_pieces[cursor[tiers[piece]]++] = pieces[piece];
    }
    return grouped;
}
