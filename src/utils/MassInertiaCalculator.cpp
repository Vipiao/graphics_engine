// MassInertiaCalculator.cpp
#include "MassInertiaCalculator.h"
#include <algorithm>

// ===== HELPER FUNCTION IMPLEMENTATIONS =====

glm::dmat3 MassInertiaCalculator::applyParallelAxisTheorem(
    const glm::dmat3& localTensor,
    double mass,
    const glm::dvec3& displacement) {
    
    // Parallel axis theorem: I_new = I_local + m*(d²*I - r⊗r)
    double distanceSquared = glm::dot(displacement, displacement);
    
    // Create r⊗r (outer product)
    glm::dmat3 outerProduct(0.0);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            outerProduct[i][j] = displacement[i] * displacement[j];
        }
    }
    
    // Create d²*I (identity matrix scaled by distance squared)
    glm::dmat3 scaledIdentity(0.0);
    scaledIdentity[0][0] = scaledIdentity[1][1] = scaledIdentity[2][2] = distanceSquared;
    
    return localTensor + mass * (scaledIdentity - outerProduct);
}

double MassInertiaCalculator::calculatePolyhedronMass(const std::vector<glm::ivec3>& vertices, int maxSize, double density) {
    if (vertices.size() != 8) {
        return 0.0; // Only support 8-vertex polyhedra
    }
    
    // Get triangles using PolyhedronProcessor
    auto triangles = PolyhedronProcessor::getTriangles(vertices, maxSize);
    if (triangles.empty()) {
        return 0.0;
    }
    
    // Calculate geometric center for tetrahedralization
    glm::dvec3 center = PolyhedronProcessor::getGeometricCenter(vertices);
    center = center / static_cast<double>(maxSize); // Normalize
    
    double totalVolume = 0.0;
    
    // Sum volumes of all tetrahedra
    for (const auto& triangle : triangles) {
        double tetraVolume = PolyhedronProcessor::calculateTetrahedronVolume(center, triangle[0], triangle[1], triangle[2]);
        totalVolume += tetraVolume;
    }
    
    return totalVolume * density;
}

glm::dvec3 MassInertiaCalculator::calculatePolyhedronCenterOfMass(const std::vector<glm::ivec3>& vertices, int maxSize) {
    if (vertices.size() != 8) {
        return glm::dvec3(0.0); // Only support 8-vertex polyhedra
    }
    
    // Get triangles using PolyhedronProcessor
    auto triangles = PolyhedronProcessor::getTriangles(vertices, maxSize);
    if (triangles.empty()) {
        return glm::dvec3(0.0);
    }
    
    // Calculate geometric center for tetrahedralization
    glm::dvec3 center = PolyhedronProcessor::getGeometricCenter(vertices);
    center = center / static_cast<double>(maxSize); // Normalize
    
    double totalVolume = 0.0;
    glm::dvec3 weightedCentroid(0.0);
    
    // Volume-weighted centroid calculation
    for (const auto& triangle : triangles) {
        double tetraVolume = PolyhedronProcessor::calculateTetrahedronVolume(center, triangle[0], triangle[1], triangle[2]);
        glm::dvec3 tetraCentroid = PolyhedronProcessor::calculateTetrahedronCentroid(center, triangle[0], triangle[1], triangle[2]);
        
        totalVolume += tetraVolume;
        weightedCentroid += tetraCentroid * tetraVolume;
    }
    
    if (totalVolume > 1e-15) {
        return weightedCentroid / totalVolume;
    }
    
    return glm::dvec3(0.0);
}

glm::dmat3 MassInertiaCalculator::calculatePointMassInertia(const std::vector<glm::ivec3>& vertices, double totalMass, const glm::dvec3& centerOfMass, int maxSize) {
    if (vertices.empty()) {
        return glm::dmat3(0.0);
    }
    
    double pointMass = totalMass / static_cast<double>(vertices.size());
    glm::dmat3 inertiaTensor(0.0);
    
    // Calculate inertia using point masses at vertices
    for (const glm::ivec3& vertex : vertices) {
        // Normalize vertex and translate to center of mass frame
        glm::dvec3 normalizedVertex = glm::dvec3(vertex) / static_cast<double>(maxSize);
        glm::dvec3 r = normalizedVertex - centerOfMass;
        
        double x = r.x, y = r.y, z = r.z;
        
        // Standard point mass inertia tensor formulation
        inertiaTensor[0][0] += pointMass * (y*y + z*z);  // Ixx
        inertiaTensor[1][1] += pointMass * (x*x + z*z);  // Iyy
        inertiaTensor[2][2] += pointMass * (x*x + y*y);  // Izz
        inertiaTensor[0][1] -= pointMass * x * y;        // -Ixy
        inertiaTensor[0][2] -= pointMass * x * z;        // -Ixz
        inertiaTensor[1][2] -= pointMass * y * z;        // -Iyz
    }
    
    // Make symmetric
    inertiaTensor[1][0] = inertiaTensor[0][1];
    inertiaTensor[2][0] = inertiaTensor[0][2];
    inertiaTensor[2][1] = inertiaTensor[1][2];
    
    return inertiaTensor;
}

double MassInertiaCalculator::calculateCubeInertiaScalingFactor(double cubeWidth, double cubeMass) {
    // True cube inertia: I = (mass/6) * width²
    double trueCubeInertia = (cubeMass / 6.0) * cubeWidth * cubeWidth;
    
    // Calculate point mass inertia for reference cube
    std::vector<glm::ivec3> cubeVertices = {
        {0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0},
        {0, 0, 4}, {4, 0, 4}, {4, 4, 4}, {0, 4, 4}
    };
    
    glm::dvec3 cubeCenterOfMass = calculatePolyhedronCenterOfMass(cubeVertices, 4);
    glm::dmat3 pointMassInertia = calculatePointMassInertia(cubeVertices, cubeMass, cubeCenterOfMass, 4);
    
    // Use diagonal component (they should all be equal for a cube)
    double pointMassCubeInertia = pointMassInertia[0][0];
    
    if (pointMassCubeInertia > 1e-15) {
        return trueCubeInertia / pointMassCubeInertia;
    }
    
    return 1.0; // Fallback if calculation fails
}

MassInertiaCalculator::MassProperties MassInertiaCalculator::calculatePolyhedronMassProperties(const std::vector<glm::ivec3>& vertices, int maxSize, double density) {
    MassProperties properties;
    
    // Calculate accurate mass and center of mass
    properties.mass = calculatePolyhedronMass(vertices, maxSize, density);
    properties.centerOfMass = calculatePolyhedronCenterOfMass(vertices, maxSize);
    
    if (properties.mass <= 1e-15) {
        properties.inertiaTensor = glm::dmat3(0.0);
        return properties;
    }
    
    // Calculate point mass inertia approximation
    glm::dmat3 pointMassInertia = calculatePointMassInertia(vertices, properties.mass, properties.centerOfMass, maxSize);
    
    // Apply scaling factor based on cube reference
    //double cubeWidth = 1.0; // Normalized cube width
    //double scalingFactor = calculateCubeInertiaScalingFactor(cubeWidth, properties.mass);
    // Optimization of the above.
    constexpr double scalingFactor = 1./3.;
    
    properties.inertiaTensor = pointMassInertia * scalingFactor;
    
    return properties;
}
