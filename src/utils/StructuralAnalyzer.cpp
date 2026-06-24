// StructuralAnalyzer.cpp
#include "StructuralAnalyzer.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

StructuralAnalyzer::StructuralAnalyzer(const StructuralAnalysisParams& params)
    : m_params(params)
{
}

double StructuralAnalyzer::manhattanDistance(const glm::ivec3& a, const glm::ivec3& b) const
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
}

bool StructuralAnalyzer::isWithinCube(const glm::ivec3& point, const glm::ivec3& center, int radius) const
{
    return std::abs(point.x - center.x) <= radius &&
           std::abs(point.y - center.y) <= radius &&
           std::abs(point.z - center.z) <= radius;
}
