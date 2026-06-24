#include "PolyhedronProcessor.h"
#include "GeometryUtils.h"
#include <glm/gtx/norm.hpp>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <numeric>
#include "../math/IntegerVectorMath.h"
#include <unordered_set>
#include <map>
#include <queue>
#include <utility>
#include <iostream>

// Define static default vertices
const std::array<glm::ivec3, 8> PolyhedronProcessor::DEFAULT_VERTICES = {{
    {0, 0, 0},          // 0: bottom-back-left
    {4, 0, 0},          // 1: bottom-back-right
    {4, 4, 0},          // 2: bottom-front-right
    {0, 4, 0},          // 3: bottom-front-left
    {0, 0, 4},          // 4: top-back-left
    {4, 0, 4},          // 5: top-back-right
    {4, 4, 4},          // 6: top-front-right
    {0, 4, 4}           // 7: top-front-left
}};

bool IVec3Compare::operator()(const glm::ivec3& a, const glm::ivec3& b) const {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

bool Vec3Compare::operator()(const glm::dvec3& a, const glm::dvec3& b) const {
    if (std::abs(a.x - b.x) > eps) return a.x < b.x;
    if (std::abs(a.y - b.y) > eps) return a.y < b.y;
    return a.z < b.z - eps;
}

std::array<std::array<int, 3>, 2> PolyhedronProcessor::getConvexTriangulation(
    const glm::ivec3& v0, const glm::ivec3& v1, const glm::ivec3& v2, const glm::ivec3& v3,
    int idx0, int idx1, int idx2, int idx3) {
    
    // Calculate normal of triangle (v0, v1, v2)
    glm::ivec3 edge1 = v1 - v0;
    glm::ivec3 edge2 = v2 - v0;
    glm::ivec3 normal = IntegerVectorMath::cross(edge1, edge2);
    
    // Calculate vector from v0 to v3
    glm::ivec3 vec_to_v3 = v3 - v0;
    
    // Check if quad is convex by testing if v3 is on the same side as the normal
    int dot_product = IntegerVectorMath::dot(normal, vec_to_v3);
    
    if (dot_product > 0) {
        // Quad is concave, use alternative triangulation
        return {{
            {idx0, idx1, idx3},
            {idx3, idx1, idx2}
        }};
    } else {
        // Quad is convex, use standard triangulation
        return {{
            {idx0, idx1, idx2},
            {idx0, idx2, idx3}
        }};
    }
}

std::array<std::array<int, 3>, 2> PolyhedronProcessor::getConvexTriangulation(
    const glm::dvec3& v0, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3,
    int idx0, int idx1, int idx2, int idx3) {
    
    // Calculate normal of triangle (v0, v1, v2)
    glm::dvec3 edge1 = v1 - v0;
    glm::dvec3 edge2 = v2 - v0;
    glm::dvec3 normal = glm::cross(edge1, edge2);
    
    // Calculate vector from v0 to v3
    glm::dvec3 vec_to_v3 = v3 - v0;
    
    // Check if quad is convex by testing if v3 is on the same side as the normal
    double dot_product = glm::dot(normal, vec_to_v3);
    
    if (dot_product > 0.0) {
        // Quad is concave, use alternative triangulation
        return {{
            {idx0, idx1, idx3},
            {idx3, idx1, idx2}
        }};
    } else {
        // Quad is convex, use standard triangulation
        return {{
            {idx0, idx1, idx2},
            {idx0, idx2, idx3}
        }};
    }
}

PolyhedronProcessor::AxisResult PolyhedronProcessor::getAxis(const std::vector<glm::ivec3>& vertices, int /*maxSize*/) {
    if (vertices.size() != 8) {
        return {}; // Invalid input - need exactly 8 vertices for a cube
    }
    
    AxisResult result;

    // Collect all face normals as integer vectors (reserve for max 12 triangles from 6 faces)
    std::vector<glm::ivec3> faceAxisInts;
    faceAxisInts.reserve(12);
    
    for (const auto& face : CUBE_FACES) {
        // Get the 4 vertices of this face (ordered counter-clockwise from outside)
        glm::ivec3 v0 = vertices[face[0]];
        glm::ivec3 v1 = vertices[face[1]];
        glm::ivec3 v2 = vertices[face[2]];
        glm::ivec3 v3 = vertices[face[3]];
        
        // Get convex triangulation
        auto triangulation = getConvexTriangulation(v0, v1, v2, v3, face[0], face[1], face[2], face[3]);
        
        // Process both triangles
        for (const auto& triangle : triangulation) {
            glm::ivec3 t_v0 = vertices[triangle[0]];
            glm::ivec3 t_v1 = vertices[triangle[1]];
            glm::ivec3 t_v2 = vertices[triangle[2]];
            
            glm::ivec3 t_edge1 = t_v1 - t_v0;
            glm::ivec3 t_edge2 = t_v2 - t_v0;
            glm::ivec3 t_normal = IntegerVectorMath::cross(t_edge1, t_edge2);
            
            if (IntegerVectorMath::length2(t_normal) > 0) {
                faceAxisInts.push_back(t_normal);
            }
        }
    }
    
    // Remove duplicates from face axes using cross product comparison
    std::vector<glm::ivec3> uniqueFaceAxisInts;
    uniqueFaceAxisInts.reserve(faceAxisInts.size());
    for (const auto& axis : faceAxisInts) {
        bool isDuplicate = false;
        for (const auto& existingAxis : uniqueFaceAxisInts) {
            glm::ivec3 crossProduct = IntegerVectorMath::cross(axis, existingAxis);
            if (crossProduct == glm::ivec3(0, 0, 0)) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            uniqueFaceAxisInts.push_back(axis);
        }
    }
    
    // Normalize face axes and add to result
    result.faceAxis.reserve(uniqueFaceAxisInts.size());
    for (const auto& axis : uniqueFaceAxisInts) {
        result.faceAxis.push_back(glm::normalize(glm::dvec3(axis)));
    }
    
    // Collect all edge directions as integer vectors (reserve for 12 edges max)
    std::vector<glm::ivec3> edgeAxisInts;
    edgeAxisInts.reserve(12);

    for (const auto& edge : CUBE_EDGES) {
        glm::ivec3 edgeVec = vertices[edge[1]] - vertices[edge[0]];
        
        if (IntegerVectorMath::length2(edgeVec) > 0) {
            edgeAxisInts.push_back(edgeVec);
        }
    }
    
    // Remove duplicates from edge axes using cross product comparison
    std::vector<glm::ivec3> uniqueEdgeAxisInts;
    uniqueEdgeAxisInts.reserve(edgeAxisInts.size());
    for (const auto& axis : edgeAxisInts) {
        bool isDuplicate = false;
        for (const auto& existingAxis : uniqueEdgeAxisInts) {
            glm::ivec3 crossProduct = IntegerVectorMath::cross(axis, existingAxis);
            if (crossProduct == glm::ivec3(0, 0, 0)) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            uniqueEdgeAxisInts.push_back(axis);
        }
    }
    
    // Normalize edge axes and add to result
    result.edgeAxis.reserve(uniqueEdgeAxisInts.size());
    for (const auto& axis : uniqueEdgeAxisInts) {
        result.edgeAxis.push_back(glm::normalize(glm::dvec3(axis)));
    }

    // Build actual edge connectivity list (preserve all edges, not just unique directions)
    result.edges.reserve(12);
    for (const auto& edge : CUBE_EDGES) {
        glm::ivec3 edgeVec = vertices[edge[1]] - vertices[edge[0]];
        
        // Only add edges with non-zero length
        if (IntegerVectorMath::length2(edgeVec) > 0) {
            result.edges.push_back({edge[0], edge[1]});
        }
    }
    
    return result;
}

std::vector<glm::dvec3> PolyhedronProcessor::getUniqueVertices(const std::vector<glm::ivec3>& vertices, int maxSize) {
    std::set<glm::ivec3, IVec3Compare> uniqueSet;
    
    // First find unique ivec3 vertices
    for (const auto& v : vertices) {
        uniqueSet.insert(v);
    }
    
    // Then convert to normalized dvec3
    std::vector<glm::dvec3> result;
    result.reserve(uniqueSet.size());
    
    for (const auto& v : uniqueSet) {
        glm::dvec3 normalized = glm::dvec3(v) / double(maxSize);
        result.push_back(normalized);
    }
    
    return result;
}

std::vector<std::array<glm::dvec3, 3>> PolyhedronProcessor::getTriangles(const std::vector<glm::ivec3>& vertices, int maxSize) {
    if (vertices.size() != 8) {
        return {}; // Invalid input
    }
    
    std::vector<std::array<glm::dvec3, 3>> triangles;
    triangles.reserve(12); // Maximum 12 triangles (2 per face * 6 faces)
    
    // Process each face and create triangles
    for (const auto& face : CUBE_FACES) {
        // Get the 4 vertices of this face
        glm::ivec3 v0 = vertices[face[0]];
        glm::ivec3 v1 = vertices[face[1]];
        glm::ivec3 v2 = vertices[face[2]];
        glm::ivec3 v3 = vertices[face[3]];
        
        // Get convex triangulation
        auto triangulation = getConvexTriangulation(v0, v1, v2, v3, face[0], face[1], face[2], face[3]);
        
        // Process both triangles
        for (const auto& triangle : triangulation) {
            glm::ivec3 t_v0 = vertices[triangle[0]];
            glm::ivec3 t_v1 = vertices[triangle[1]];
            glm::ivec3 t_v2 = vertices[triangle[2]];
            
            glm::ivec3 t_cross = IntegerVectorMath::cross(t_v1 - t_v0, t_v2 - t_v0);
            double area = 0.5 * glm::length(glm::dvec3(t_cross));
            
            if (area > Vec3Compare::eps) {
                glm::dvec3 dv0 = glm::dvec3(t_v0) / double(maxSize);
                glm::dvec3 dv1 = glm::dvec3(t_v1) / double(maxSize);
                glm::dvec3 dv2 = glm::dvec3(t_v2) / double(maxSize);
                triangles.push_back({dv0, dv1, dv2});
            }
        }
    }
    
    return triangles;
}

std::vector<std::array<int, 3>> PolyhedronProcessor::getTriangleIndices(const std::vector<glm::ivec3>& vertices, int /* maxSize */) {
    if (vertices.size() != 8) {
        return {}; // Invalid input
    }
    
    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(12); // Maximum 12 triangles (2 per face * 6 faces)
    
    // Process each face
    for (const auto& face : CUBE_FACES) {
        // Get quad vertices
        const glm::ivec3& v0 = vertices[face[0]];
        const glm::ivec3& v1 = vertices[face[1]];
        const glm::ivec3& v2 = vertices[face[2]];
        const glm::ivec3& v3 = vertices[face[3]];
        
        // Get triangulation for this quad
        auto triangulation = getConvexTriangulation(v0, v1, v2, v3, face[0], face[1], face[2], face[3]);
        
        // Process both triangles
        for (const auto& triangle : triangulation) {
            glm::ivec3 t_v0 = vertices[triangle[0]];
            glm::ivec3 t_v1 = vertices[triangle[1]];
            glm::ivec3 t_v2 = vertices[triangle[2]];
            
            // Check if triangle has non-zero area
            glm::ivec3 t_cross = IntegerVectorMath::cross(t_v1 - t_v0, t_v2 - t_v0);
            double area = 0.5 * glm::length(glm::dvec3(t_cross));
            
            if (area > Vec3Compare::eps) {
                triangles.push_back({triangle[0], triangle[1], triangle[2]});
            }
        }
    }
    
    return triangles;
}

std::vector<std::array<int, 3>> PolyhedronProcessor::getTriangleIndices(const std::vector<glm::dvec3>& vertices) {
    if (vertices.size() != 8) {
        return {}; // Invalid input
    }
    
    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(12); // Maximum 12 triangles (2 per face * 6 faces)
    
    // Process each face
    for (const auto& face : CUBE_FACES) {
        // Get quad vertices
        const glm::dvec3& v0 = vertices[face[0]];
        const glm::dvec3& v1 = vertices[face[1]];
        const glm::dvec3& v2 = vertices[face[2]];
        const glm::dvec3& v3 = vertices[face[3]];
        
        // Get triangulation for this quad
        auto triangulation = getConvexTriangulation(v0, v1, v2, v3, face[0], face[1], face[2], face[3]);
        
        // Process both triangles
        for (const auto& triangle : triangulation) {
            const glm::dvec3& t_v0 = vertices[triangle[0]];
            const glm::dvec3& t_v1 = vertices[triangle[1]];
            const glm::dvec3& t_v2 = vertices[triangle[2]];
            
            // Check if triangle has non-zero area
            glm::dvec3 t_cross = glm::cross(t_v1 - t_v0, t_v2 - t_v0);
            double area = 0.5 * glm::length(t_cross);
            
            if (area > Vec3Compare::eps) {
                triangles.push_back({triangle[0], triangle[1], triangle[2]});
            }
        }
    }
    
    return triangles;
}

glm::dvec3 PolyhedronProcessor::getTriangleCenter(const std::array<glm::dvec3, 3>& triangle) {
    return (triangle[0] + triangle[1] + triangle[2]) / 3.0;
}

glm::dvec3 PolyhedronProcessor::getTriangleNormal(const std::array<glm::dvec3, 3>& triangle) {
    glm::dvec3 edge1 = triangle[1] - triangle[0];
    glm::dvec3 edge2 = triangle[2] - triangle[0];
    glm::dvec3 normal = glm::cross(edge1, edge2);
    
    if (glm::length(normal) > Vec3Compare::eps) {
        return glm::normalize(normal);
    }
    return glm::dvec3(0.0, 0.0, 0.0); // Degenerate triangle
}

int PolyhedronProcessor::countSharedVertices(const std::array<glm::dvec3, 3>& t1, const std::array<glm::dvec3, 3>& t2, double tolerance) {
    int count = 0;
    
    for (const auto& v1 : t1) {
        for (const auto& v2 : t2) {
            if (glm::length(v1 - v2) < tolerance) {
                count++;
                break; // Found a match for this vertex, move to next
            }
        }
    }
    
    return count;
}

bool PolyhedronProcessor::validatePolyhedron(const std::vector<glm::ivec3>& vertices, int maxSize, 
                                             double normalThreshold, double convexityMargin) {
    // Get all triangles
    auto triangles = getTriangles(vertices, maxSize);
    
    // Check minimum triangle count (need at least 4 for a tetrahedron)
    if (triangles.size() < 4) {
        return false;
    }
    
    // Check all pairs of triangles
    for (size_t i = 0; i < triangles.size(); ++i) {
        for (size_t j = i + 1; j < triangles.size(); ++j) {
            const auto& triangleA = triangles[i];
            const auto& triangleB = triangles[j];
            
            // Count shared vertices
            int sharedVertices = countSharedVertices(triangleA, triangleB);
            
            // Only check triangles that share 1 or 2 vertices
            if (sharedVertices == 0) {
                continue;
            }

            // Calculate normals
            glm::dvec3 normalA = getTriangleNormal(triangleA);
            glm::dvec3 normalB = getTriangleNormal(triangleB);
            
            // Check if normals are degenerate
            if (glm::length2(normalA) < Vec3Compare::eps || glm::length2(normalB) < Vec3Compare::eps) {
                return false; // Degenerate triangle
            }
            
            // Check for nearly opposite normals
            double normalDot = glm::dot(normalA, normalB);
            if (normalDot < normalThreshold) {
                return false; // Nearly opposite normals
            }
        
            // Convexity check: vector from center of A to center of B should point outward from A
            glm::dvec3 centerA = getTriangleCenter(triangleA);
            glm::dvec3 centerB = getTriangleCenter(triangleB);
            
            glm::dvec3 centerToCenter = glm::normalize(centerB - centerA);
            double convexityDot = glm::dot(-normalA, centerToCenter);
            
            // If dot product is significantly negative, triangle A is facing inward relative to B (concave)
            if (convexityDot < convexityMargin) {
                return false; // Concave with margin
            }
        }
    }
    
    return true; // All checks passed
}

PolyhedronProcessor::MeshData PolyhedronProcessor::generateMeshData(const std::vector<std::array<glm::dvec3, 3>>& triangles) {
    MeshData meshData;
    
    if (triangles.empty()) {
        return meshData; // Return empty mesh data
    }
    
    // Reserve space for triangle data (3 vertices per triangle)
    size_t numVertices = triangles.size() * 3;
    meshData.positions.reserve(numVertices);
    meshData.normals.reserve(numVertices);
    meshData.tangents.reserve(numVertices);
    meshData.uvs.reserve(numVertices);
    
    // First pass: Generate positions, normals, and determine projection for each triangle
    struct TriangleInfo {
        glm::dvec3 normal;
        int projectionAxis; // 0=YZ, 1=XZ, 2=XY
        std::array<glm::dvec2, 3> projectedCoords;
    };
    
    std::vector<TriangleInfo> triangleInfos;
    triangleInfos.reserve(triangles.size());
    
    // Collect all 2D projected coordinates for global bounding box calculation
    std::vector<glm::dvec2> allProjectedCoords;
    allProjectedCoords.reserve(triangles.size() * 3); // Reserve for known size
    
    for (const auto& triangle : triangles) {
        TriangleInfo info;
        
        // Calculate triangle normal
        glm::dvec3 edge1 = triangle[1] - triangle[0];
        glm::dvec3 edge2 = triangle[2] - triangle[0];
        info.normal = glm::normalize(glm::cross(edge1, edge2));
        
        // Determine best projection axis based on normal
        glm::dvec3 absNormal = glm::abs(info.normal);
        if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z) {
            info.projectionAxis = 0; // Project to YZ plane
        } else if (absNormal.y >= absNormal.z) {
            info.projectionAxis = 1; // Project to XZ plane
        } else {
            info.projectionAxis = 2; // Project to XY plane
        }
        
        // Project triangle vertices to 2D
        for (int i = 0; i < 3; ++i) {
            glm::dvec2 projected;
            switch (info.projectionAxis) {
                case 0: // YZ plane
                    projected = glm::dvec2(triangle[i].y, triangle[i].z);
                    break;
                case 1: // XZ plane
                    projected = glm::dvec2(triangle[i].x, triangle[i].z);
                    break;
                case 2: // XY plane
                    projected = glm::dvec2(triangle[i].x, triangle[i].y);
                    break;
            }
            info.projectedCoords[i] = projected;
            allProjectedCoords.push_back(projected);
        }
        
        triangleInfos.push_back(info);
    }
    
    // Calculate global bounding box for UV normalization
    glm::dvec2 minCoords = allProjectedCoords[0];
    glm::dvec2 maxCoords = allProjectedCoords[0];
    
    for (const auto& coord : allProjectedCoords) {
        minCoords = glm::min(minCoords, coord);
        maxCoords = glm::max(maxCoords, coord);
    }
    
    glm::dvec2 uvRange = maxCoords - minCoords;
    // Prevent division by zero
    if (uvRange.x < 1e-9) uvRange.x = 1.0;
    if (uvRange.y < 1e-9) uvRange.y = 1.0;
    
    // Second pass: Generate all vertex attributes
    for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx) {
        const auto& triangle = triangles[triIdx];
        const auto& info = triangleInfos[triIdx];
        
        // Generate UVs for this triangle
        std::array<glm::dvec2, 3> uvs;
        for (int i = 0; i < 3; ++i) {
            uvs[i] = (info.projectedCoords[i] - minCoords) / uvRange;
        }
        
        // Calculate tangent vector from UV coordinates
        glm::dvec3 edge1 = triangle[1] - triangle[0];
        glm::dvec3 edge2 = triangle[2] - triangle[0];
        glm::dvec2 deltaUV1 = uvs[1] - uvs[0];
        glm::dvec2 deltaUV2 = uvs[2] - uvs[0];
        
        glm::dvec3 tangent;
        double denominator = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        
        if (glm::abs(denominator) > 1e-9) {
            double invDenominator = 1.0 / denominator;
            tangent = (deltaUV2.y * edge1 - deltaUV1.y * edge2) * invDenominator;
        } else {
            // Degenerate UV case - generate tangent perpendicular to normal
            if (glm::abs(info.normal.x) < 0.9) {
                tangent = glm::cross(info.normal, glm::dvec3(1.0, 0.0, 0.0));
            } else {
                tangent = glm::cross(info.normal, glm::dvec3(0.0, 1.0, 0.0));
            }
        }
        
        // Orthogonalize tangent against normal and normalize
        tangent = glm::normalize(tangent - glm::dot(tangent, info.normal) * info.normal);
        
        // Add vertices for this triangle
        for (int i = 0; i < 3; ++i) {
            meshData.positions.push_back(triangle[i]);
            meshData.normals.push_back(info.normal);
            meshData.tangents.push_back(tangent);
            meshData.uvs.push_back(uvs[i]);
        }
    }
    
    return meshData;
}

glm::dvec3 PolyhedronProcessor::getGeometricCenter(const std::vector<glm::ivec3>& vertices) {
    if (vertices.empty()) {
        return glm::dvec3(0.0);
    }
    
    glm::dvec3 sum(0.0);
    for (const glm::ivec3& vertex : vertices) {
        sum += glm::dvec3(vertex);
    }
    
    return sum / static_cast<double>(vertices.size());
}

double PolyhedronProcessor::calculateTetrahedronVolume(const glm::dvec3& apex, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3) {
    // Volume = |det(v1-apex, v2-apex, v3-apex)| / 6
    glm::dvec3 edge1 = v1 - apex;
    glm::dvec3 edge2 = v2 - apex;
    glm::dvec3 edge3 = v3 - apex;
    
    double det = glm::dot(edge1, glm::cross(edge2, edge3));
    return std::abs(det) / 6.0;
}

glm::dvec3 PolyhedronProcessor::calculateTetrahedronCentroid(const glm::dvec3& apex, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3) {
    // Centroid = (apex + v1 + v2 + v3) / 4
    return (apex + v1 + v2 + v3) * 0.25;
}

std::vector<glm::dvec3> PolyhedronProcessor::generateCubeVertices(double width) {
    double halfWidth = width * 0.5;
    
    return {
        {-halfWidth, -halfWidth, -halfWidth}, { halfWidth, -halfWidth, -halfWidth},
        { halfWidth,  halfWidth, -halfWidth}, {-halfWidth,  halfWidth, -halfWidth},
        {-halfWidth, -halfWidth,  halfWidth}, { halfWidth, -halfWidth,  halfWidth},
        { halfWidth,  halfWidth,  halfWidth}, {-halfWidth,  halfWidth,  halfWidth}
    };
}

std::vector<glm::dvec3> PolyhedronProcessor::generateCubeAxes() {
    return {
        glm::dvec3(1.0, 0.0, 0.0),  // X-axis
        glm::dvec3(0.0, 1.0, 0.0),  // Y-axis
        glm::dvec3(0.0, 0.0, 1.0)   // Z-axis
    };
}

std::vector<std::array<int, 2>> PolyhedronProcessor::generateCubeEdges() {
    // Return standard cube edge connectivity (12 edges)
    // Uses same vertex ordering as generateCubeVertices and CUBE_EDGES
    return {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},  // Bottom face edges
        {4, 5}, {5, 6}, {6, 7}, {7, 4},  // Top face edges
        {0, 4}, {1, 5}, {2, 6}, {3, 7}   // Vertical edges
    };
}

bool PolyhedronProcessor::isPointInConvexPolygon(
    const glm::dvec2& point,
    const std::vector<glm::dvec2>& polygon,
    double margin) {
    
    if (polygon.size() < 3) {
        return false; // Not a valid polygon
    }
    
    bool hasValidEdge = false;

    // For each edge of the polygon, check if point is on the "inside" side
    for (size_t i = 0; i < polygon.size(); ++i) {
        size_t j = (i + 1) % polygon.size();
        
        glm::dvec2 edgeStart = polygon[i];
        glm::dvec2 edgeEnd = polygon[j];
        glm::dvec2 edge = edgeEnd - edgeStart;

        // Skip zero-length edges to avoid normalization of zero vector
        double edgeLengthSq = edge.x * edge.x + edge.y * edge.y;
        if (edgeLengthSq < 1e-18) {
            continue;
        }

        hasValidEdge = true;
        
        // Calculate inward normal (rotate edge 90 degrees clockwise for counter-clockwise polygon)
        glm::dvec2 inwardNormal = glm::dvec2(edge.y, -edge.x) / glm::sqrt(edgeLengthSq);
        
        // Move edge inward by margin distance
        glm::dvec2 adjustedEdgeStart = edgeStart + inwardNormal * margin;
        glm::dvec2 adjustedEdgeEnd = edgeEnd + inwardNormal * margin;
        glm::dvec2 adjustedEdge = adjustedEdgeEnd - adjustedEdgeStart;
        
        // Check which side of the adjusted edge the point is on
        glm::dvec2 toPoint = point - adjustedEdgeStart;
        double cross = adjustedEdge.x * toPoint.y - adjustedEdge.y * toPoint.x;
        
        // For counter-clockwise polygon, point should be on the left side (positive cross product)
        if (cross < 0.0) {
            return false; // Point is outside this edge
        }
    }

    // If no valid edges exist, polygon is degenerate (all vertices at same point)
    // Point is only "inside" if it coincides with that point
    if (!hasValidEdge) {
        glm::dvec2 diff = point - polygon[0];
        double distSq = diff.x * diff.x + diff.y * diff.y;
        return distSq < 1e-18;
    }
    
    return true; // Point is inside all edges
}

bool PolyhedronProcessor::areTrianglesConvex(
    const std::vector<glm::dvec3>& vertices,
    const std::vector<std::array<int, 3>>& triangleIndices,
    const std::vector<bool>& triangleMask,
    bool counterClockwise) {

    if (triangleIndices.size() != triangleMask.size()) {
        return false; // Mismatched sizes
    }
    
    if (triangleIndices.size() < 2) {
        return true; // Single triangle is always "convex"
    }

    //extern int debug1;
    //debug1++;
    //
    //// Debug output for GeoGebra
    //std::cout << "areTrianglesConvex Debug - Vertices:" << std::endl;
    //for (size_t i = 0; i < vertices.size(); ++i) {
    //    std::cout << "V" << i << "=(" << vertices[i].x << "," << vertices[i].y << "," << vertices[i].z << ")" << std::endl;
    //}
    //
    //std::cout << "Triangles (masked only):" << std::endl;
    //for (size_t i = 0; i < triangleIndices.size(); ++i) {
    //    if (triangleMask[i]) {
    //        const auto& tri = triangleIndices[i];
    //        std::cout << "Polygon(V" << tri[0] << ", V" << tri[1] << ", V" << tri[2] << ")" << std::endl;
    //    }
    //}

    // Identify degenerate triangles (triangles without 3 distinct vertices)
    std::vector<bool> isDegenerateTriangle(triangleIndices.size(), false);
    for (size_t i = 0; i < triangleIndices.size(); ++i) {
        const auto& tri = triangleIndices[i];
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2]) {
            isDegenerateTriangle[i] = true;
        }
    }

    // Build a map of edges to triangles for efficient lookup
    struct Edge {
        int v1, v2;
        
        bool operator<(const Edge& other) const {
            if (v1 != other.v1) return v1 < other.v1;
            return v2 < other.v2;
        }
    };
    
    std::map<Edge, std::vector<size_t>> edgeToTriangles;
    
    // Build edge map
    for (size_t triIdx = 0; triIdx < triangleIndices.size(); ++triIdx) {
        if (isDegenerateTriangle[triIdx]) continue;
        
        const auto& triangle = triangleIndices[triIdx];
        
        // Add all three edges of this triangle
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;
            int v1 = triangle[i];
            int v2 = triangle[j];
            
            // Normalize edge direction (smaller index first)
            Edge edge;
            if (v1 < v2) {
                edge = {v1, v2};
            } else {
                edge = {v2, v1};
            }
            
            edgeToTriangles[edge].push_back(triIdx);
        }
    }
    
    // Check convexity for each shared edge
    for (const auto& pair : edgeToTriangles) {
        const std::vector<size_t>& triangleIdxList = pair.second;
        
        // Only check edges shared by exactly 2 triangles
        if (triangleIdxList.size() != 2) {
            continue;
        }

        // Only check convexity if at least one triangle is included in the mask
        if (!triangleMask[triangleIdxList[0]] && !triangleMask[triangleIdxList[1]]) {
            continue;
        }
        
        const auto& tri1 = triangleIndices[triangleIdxList[0]];
        const auto& tri2 = triangleIndices[triangleIdxList[1]];
        
        // Get triangle vertices
        glm::dvec3 tri1_v0 = vertices[tri1[0]];
        glm::dvec3 tri1_v1 = vertices[tri1[1]];
        glm::dvec3 tri1_v2 = vertices[tri1[2]];
        
        // Calculate normal for first triangle
        glm::dvec3 edge1_1 = tri1_v1 - tri1_v0;
        glm::dvec3 edge1_2 = tri1_v2 - tri1_v0;
        glm::dvec3 normal1 = counterClockwise ? glm::cross(edge1_1, edge1_2) : glm::cross(edge1_2, edge1_1);
        normal1 = glm::normalize(normal1);
        
        // Calculate centroids
        glm::dvec3 centroid1 = (tri1_v0 + tri1_v1 + tri1_v2) / 3.0;
        glm::dvec3 centroid2 = (vertices[tri2[0]] + vertices[tri2[1]] + vertices[tri2[2]]) / 3.0;
        
        // Check convexity: dot product between normal and centroid difference
        glm::dvec3 centroidDiff = centroid2 - centroid1;
        double dot = glm::dot(normal1, centroidDiff);
        if (dot > 1.e-3) { // Allow small tolerance for numerical errors
            return false; // Concave
        }
    }
    
    return true; // All checked edges are convex
}

bool PolyhedronProcessor::hasAtLeastOneConvexVertex(
    const std::vector<glm::dvec3>& vertices,
    const std::vector<std::array<int, 3>>& triangleIndices,
    const std::vector<int>& verticesToTest) {
    
    //extern int debug1;
    //debug1++;
    //
    //// Debug output for GeoGebra
    //std::cout << "Vertices for convexity analysis:" << std::endl;
    //for (size_t i = 0; i < vertices.size(); ++i) {
    //    std::cout << "V" << i << "=(" << vertices[i].x << "," << vertices[i].y << "," << vertices[i].z << ")" << std::endl;
    //}
    //
    //std::cout << "Triangles:" << std::endl;
    //for (size_t i = 0; i < triangleIndices.size(); ++i) {
    //    const auto& tri = triangleIndices[i];
    //    std::cout << "Polygon(V" << tri[0] << ", V" << tri[1] << ", V" << tri[2] << ")" << std::endl;
    //}

    // Early returns for invalid inputs
    if (triangleIndices.size() < 3 || vertices.size() < 4 || verticesToTest.empty()) {
        return false;
    }

    const double distanceMargin = 1e-6;

    // Pre-calculate all triangle normals once
    std::vector<glm::dvec3> triangleNormals;
    triangleNormals.reserve(triangleIndices.size());
    
    for (size_t triIdx = 0; triIdx < triangleIndices.size(); ++triIdx) {
        const auto& triangle = triangleIndices[triIdx];
        
        glm::dvec3 v0 = vertices[triangle[0]];
        glm::dvec3 v1 = vertices[triangle[1]];
        glm::dvec3 v2 = vertices[triangle[2]];
        
        glm::dvec3 edge1 = v1 - v0;
        glm::dvec3 edge2 = v2 - v0;
        glm::dvec3 normal = glm::cross(edge1, edge2);
        
        if (glm::length2(normal) > Vec3Compare::eps * Vec3Compare::eps) {
            normal = glm::normalize(normal);
        } else {
            normal = glm::dvec3(0.0); // Invalid normal for degenerate triangle
        }
        
        triangleNormals.push_back(normal);
    }
    
    // Test each vertex for convexity
    for (int vertexIdx : verticesToTest) {
        if (vertexIdx < 0 || vertexIdx >= static_cast<int>(vertices.size())) {
            continue; // Skip invalid vertex indices
        }

        // Find all triangles within distance margin of this vertex
        std::vector<glm::dvec3> neighboringNormals;
        std::unordered_set<int> neighboringVertexIndices;
        
        for (size_t triIdx = 0; triIdx < triangleIndices.size(); ++triIdx) {
            const auto& triangle = triangleIndices[triIdx];
            
            // Calculate distance from vertex to triangle
            double distance = GeometryUtils::pointToTriangleDistance(
                vertices[vertexIdx],
                vertices[triangle[0]],
                vertices[triangle[1]],
                vertices[triangle[2]]);
            
            if (distance <= distanceMargin) {
                // Use pre-calculated normal
                if (glm::length2(triangleNormals[triIdx]) > Vec3Compare::eps * Vec3Compare::eps) {
                    neighboringNormals.push_back(triangleNormals[triIdx]);
                    
                    // Collect all vertex indices from this triangle
                    neighboringVertexIndices.insert(triangle[0]);
                    neighboringVertexIndices.insert(triangle[1]);
                    neighboringVertexIndices.insert(triangle[2]);
                }
            }
        }
        if (neighboringNormals.empty()) {
            continue; // No neighboring triangles found
        }

        // Convert set to vector for projection
        std::vector<int> verticesToProject(neighboringVertexIndices.begin(), neighboringVertexIndices.end());
        
        // Test this vertex for convexity against each of its normals
        bool isVertexConvex = false;
        
        for (const glm::dvec3& normal : neighboringNormals) {
            // Project all vertices onto the normal
            std::vector<std::pair<double, int>> projections; 
            for (int vIdx : verticesToProject) {
                double projection = glm::dot(vertices[vIdx], normal);
                projections.push_back({projection, vIdx});
            }
            
            // Find maximum projection value
            double maxProjection = projections[0].first;
            for (const auto& proj : projections) {
                maxProjection = std::max(maxProjection, proj.first);
            }
            
            // Collect vertices at positive extreme (within tolerance)
            const double tolerance = 1e-9;
            std::vector<int> extremeVertices;
            for (const auto& proj : projections) {
                if (proj.first >= maxProjection - tolerance) {
                    extremeVertices.push_back(proj.second);
                }
            }
            
            // Check if corner vertex is at positive extreme
            bool cornerAtExtreme = std::find(extremeVertices.begin(), extremeVertices.end(), vertexIdx) != extremeVertices.end();
            if (!cornerAtExtreme) {
                continue; // Not convex for this normal, try next normal
            }
            
            // If exactly 3 vertices at extreme (including corner), corner is convex
            if (extremeVertices.size() == 3) {
                isVertexConvex = true;
                break; // Found convexity, no need to check other normals
            }
            
            // If more than 3 vertices at extreme, do 2D analysis
            if (extremeVertices.size() > 3) {
                // Create plane transform for this normal
                glm::dmat3 planeTransform = GeometryUtils::createPlaneTransform(normal);
                
                // Project extreme vertices to 2D
                std::vector<glm::dvec3> extremeVertices3D;
                for (int vIdx : extremeVertices) {
                    extremeVertices3D.push_back(vertices[vIdx]);
                }
                
                std::vector<glm::dvec2> extremeVertices2D = GeometryUtils::projectToPlane(extremeVertices3D, planeTransform);
                
                // Find corner vertex in 2D
                int cornerIdx2D = -1;
                for (size_t i = 0; i < extremeVertices.size(); ++i) {
                    if (extremeVertices[i] == vertexIdx) {
                        cornerIdx2D = static_cast<int>(i);
                        break;
                    }
                }
                
                if (cornerIdx2D == -1) continue; // Shouldn't happen, but safety check
                
                glm::dvec2 cornerPos2D = extremeVertices2D[cornerIdx2D];
                
                // Get relative positions of other extreme vertices
                std::vector<glm::dvec2> otherVertices2D;
                for (size_t i = 0; i < extremeVertices2D.size(); ++i) {
                    if (static_cast<int>(i) != cornerIdx2D) {
                        otherVertices2D.push_back(extremeVertices2D[i] - cornerPos2D);
                    }
                }
                
                if (otherVertices2D.size() < 3) {
                    // Less than 3 other vertices, automatically convex
                    isVertexConvex = true;
                    break;
                }
                
                // Wind the other vertices
                std::vector<glm::dvec2> windedVertices2D = GeometryUtils::windPointsAroundOrigin(otherVertices2D);
                
                // Test if corner (origin) is outside the fan
                bool isInsideFan = true;
                for (size_t i = 0; i < windedVertices2D.size(); ++i) {
                    size_t nextIdx = (i + 1) % windedVertices2D.size();
                    
                    glm::dvec2 edge = windedVertices2D[nextIdx] - windedVertices2D[i];
                    glm::dvec2 toCorner = glm::dvec2(0.0) - windedVertices2D[i]; // Corner is at origin
                    
                    // Cross product to determine which side of edge the corner is on
                    double cross = edge.x * toCorner.y - edge.y * toCorner.x;
                    
                    // For counter-clockwise winding, corner is outside if on right side  
                    static const double fanMargin = 1.e-6;
                    if (cross < -fanMargin) {
                        isInsideFan = false;
                        break;
                    }
                }
                
                if (!isInsideFan) {
                    isVertexConvex = true;
                    break; // Found convexity, no need to check other normals
                }
            }
        }
        
        if (isVertexConvex) {
            return true; // Found at least one convex vertex
        }
    }
    
    return false; // No convex vertices found
}

bool PolyhedronProcessor::areTrianglesConvexInDirection(
    const std::vector<std::array<glm::dvec3, 3>>& triangles,
    const glm::dvec3& direction,
    bool counterClockwise) {
    
    if (triangles.size() < 2) {
        return true; // Single triangle is always "convex"
    }
    
    glm::dvec3 normalizedDirection = glm::normalize(direction);
    
    // Build edge map (same as in areTrianglesConvex)
    struct Edge {
        glm::dvec3 v1, v2;
        bool operator<(const Edge& other) const {
            if (v1 != other.v1) return v1.x < other.v1.x || (v1.x == other.v1.x && (v1.y < other.v1.y || (v1.y == other.v1.y && v1.z < other.v1.z)));
            return v2.x < other.v2.x || (v2.x == other.v2.x && (v2.y < other.v2.y || (v2.y == other.v2.y && v2.z < other.v2.z)));
        }
    };
    
    std::map<Edge, std::vector<size_t>> edgeToTriangles;
    
    // Build edge map (same logic as before)
    for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx) {
        const auto& triangle = triangles[triIdx];
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;
            glm::dvec3 v1 = triangle[i];
            glm::dvec3 v2 = triangle[j];
            
            Edge edge;
            if (v1.x < v2.x || (v1.x == v2.x && (v1.y < v2.y || (v1.y == v2.y && v1.z < v2.z)))) {
                edge = {v1, v2};
            } else {
                edge = {v2, v1};
            }
            
            edgeToTriangles[edge].push_back(triIdx);
        }
    }
    
    // Check directional convexity for each shared edge
    for (const auto& pair : edgeToTriangles) {
        const std::vector<size_t>& triangleIndices = pair.second;
        
        if (triangleIndices.size() != 2) {
            continue;
        }
        
        const auto& tri1 = triangles[triangleIndices[0]];
        const auto& tri2 = triangles[triangleIndices[1]];
        
        // Calculate normals
        glm::dvec3 edge1_1 = tri1[1] - tri1[0];
        glm::dvec3 edge1_2 = tri1[2] - tri1[0];
        glm::dvec3 normal1 = counterClockwise ? glm::cross(edge1_1, edge1_2) : glm::cross(edge1_2, edge1_1);
        normal1 = glm::normalize(normal1);
        
        glm::dvec3 edge2_1 = tri2[1] - tri2[0];
        glm::dvec3 edge2_2 = tri2[2] - tri2[0];
        glm::dvec3 normal2 = counterClockwise ? glm::cross(edge2_1, edge2_2) : glm::cross(edge2_2, edge2_1);
        normal2 = glm::normalize(normal2);
        
        // Project normals onto the direction
        double proj1 = glm::dot(normal1, normalizedDirection);
        double proj2 = glm::dot(normal2, normalizedDirection);
        
        // Calculate triangle centroids
        glm::dvec3 centroid1 = (tri1[0] + tri1[1] + tri1[2]) / 3.0;
        glm::dvec3 centroid2 = (tri2[0] + tri2[1] + tri2[2]) / 3.0;
        
        // Vector from centroid1 to centroid2 projected onto direction
        glm::dvec3 centroidDiff = centroid2 - centroid1;
        double centroidProjDiff = glm::dot(centroidDiff, normalizedDirection);
        
        // Check if the surface curves in the right direction
        // If moving in positive direction, the normal component should increase (or stay same)
        if (centroidProjDiff > 0.01) { // Moving forward in direction
            if (proj2 < proj1 - 0.1) { // Normal is decreasing too much (concave)
                return false;
            }
        } else if (centroidProjDiff < -0.01) { // Moving backward in direction
            if (proj1 < proj2 - 0.1) { // Normal is decreasing too much (concave)
                return false;
            }
        }
    }
    
    return true; // All edges are convex in the given direction
}

std::vector<bool> PolyhedronProcessor::checkPolyhedronBorderIntersection(
    const glm::ivec3& coordA, const std::vector<glm::dvec3>& verticesA,
    const glm::ivec3& coordB, const std::vector<glm::dvec3>& verticesB) {
    
    std::vector<bool> result(verticesA.size(), false);
    
    // Calculate the difference between coordinates
    glm::ivec3 diff = coordB - coordA;
    
    // Check if coordinates are adjacent (Manhattan distance exactly 1)
    if (std::abs(diff.x) + std::abs(diff.y) + std::abs(diff.z) != 1) {
        return result; // Not adjacent cells, return all false
    }
    
    // Calculate border axis and value
    int borderAxis = std::abs(diff.y) + std::abs(diff.z) * 2;
    double borderValue = std::max(diff.x + diff.y + diff.z, 0);
    double borderValueB = 1.0 - borderValue;
    
    const double tolerance = 1e-6;
    
    // Arrays for processing both A and B vertices
    const std::vector<glm::dvec3>* vertices[2] = {&verticesA, &verticesB};
    double borderValues[2] = {borderValue, borderValueB};
    glm::ivec3 coordOffsets[2] = {glm::ivec3(0), diff};
    
    std::vector<glm::dvec2> borderVerticesA;
    std::vector<int> borderVertexIndicesA;
    std::vector<glm::dvec2> borderVerticesB;
    
    // Collect border vertices for both polyhedra
    for (int polyIdx = 0; polyIdx < 2; ++polyIdx) {
        const auto& polyVertices = *vertices[polyIdx];
        double targetBorderValue = borderValues[polyIdx];
        glm::ivec3 coordOffset = coordOffsets[polyIdx];
        
        for (size_t i = 0; i < polyVertices.size(); ++i) {
            double axisValue = polyVertices[i][borderAxis];
            if (std::abs(axisValue - targetBorderValue) < tolerance) {
                // This vertex lies on the border, project to 2D
                glm::dvec2 vertex2D;
                if (borderAxis == 0) { // YZ plane
                    vertex2D = glm::dvec2(polyVertices[i].y + coordOffset.y, polyVertices[i].z + coordOffset.z);
                } else if (borderAxis == 1) { // XZ plane
                    vertex2D = glm::dvec2(polyVertices[i].x + coordOffset.x, polyVertices[i].z + coordOffset.z);
                } else { // XY plane
                    vertex2D = glm::dvec2(polyVertices[i].x + coordOffset.x, polyVertices[i].y + coordOffset.y);
                }
                
                if (polyIdx == 0) { // Processing A
                    borderVerticesA.push_back(vertex2D);
                    borderVertexIndicesA.push_back(i);
                } else { // Processing B
                    borderVerticesB.push_back(vertex2D);
                }
            }
        }
    }
    
    // If either polyhedron has no vertices on the border, no intersection possible
    if (borderVerticesA.empty() || borderVerticesB.empty()) {
        return result;
    }
    
    // Wind the B vertices to ensure proper polygon ordering
    std::vector<glm::dvec2> windedVerticesB;
    if (borderVerticesB.size() >= 3) {
        windedVerticesB = GeometryUtils::windPoints(borderVerticesB);
    } else {
        return result; // Can't form a polygon with < 3 vertices
    }
    
    // Check each border vertex of A against the polygon formed by B's border vertices
    const double margin = 0.01; // Small margin for intersection tolerance
    
    for (size_t i = 0; i < borderVerticesA.size(); ++i) {
        int vertexIndex = borderVertexIndicesA[i];
        const glm::dvec2& vertex = borderVerticesA[i];
        
        bool isInside = isPointInConvexPolygon(vertex, windedVerticesB, margin);
        result[vertexIndex] = isInside;
    }
    
    return result;
}

bool PolyhedronProcessor::areTrianglesAdjacent(const std::array<glm::dvec3, 3>& triangleA, const std::array<glm::dvec3, 3>& triangleB, double tolerance) {
    // Find shared vertices between the triangles
    std::vector<std::pair<int, int>> sharedVertices; // pairs of (indexA, indexB)
    
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (glm::length(triangleA[i] - triangleB[j]) < tolerance) {
                sharedVertices.push_back({i, j});
                break; // Each vertex in A can match at most one vertex in B
            }
        }
    }
    
    // Triangles are adjacent if they share exactly 2 vertices (an edge)
    if (sharedVertices.size() != 2) {
        return false;
    }
    
    // Get the indices of shared vertices in both triangles
    int idxA1 = sharedVertices[0].first;
    int idxB1 = sharedVertices[0].second;
    int idxA2 = sharedVertices[1].first;
    int idxB2 = sharedVertices[1].second;
    
    // Check winding consistency
    // For proper winding, the shared edge should appear in opposite directions in the two triangles
    
    // In triangle A, determine the direction of the shared edge
    bool edgeA_forward = (idxA2 == (idxA1 + 1) % 3); // Is the edge v1->v2 in forward direction?
    bool edgeA_backward = (idxA1 == (idxA2 + 1) % 3); // Is the edge v2->v1 in forward direction?
    
    // In triangle B, determine the direction of the shared edge
    bool edgeB_forward = (idxB2 == (idxB1 + 1) % 3); // Is the edge v1->v2 in forward direction?
    bool edgeB_backward = (idxB1 == (idxB2 + 1) % 3); // Is the edge v2->v1 in forward direction?
    
    // The shared edge must be part of both triangles' edge lists
    if (!((edgeA_forward || edgeA_backward) && (edgeB_forward || edgeB_backward))) {
        return false; // Shared vertices don't form a continuous edge in both triangles
    }
    
    // For consistent winding, the edge directions should be opposite
    if (edgeA_forward != edgeB_forward) {
        return true;
    }
    
    // If we reach here, the triangles share an edge but have inconsistent winding
    // This would create a surface orientation flip, so they're not properly adjacent
    return false;
}

std::vector<std::vector<int>> PolyhedronProcessor::groupTrianglesIntoIslands(
    const std::vector<glm::dvec3>& /*vertices*/,
    const std::vector<std::array<int, 3>>& triangleIndices) {
    
    if (triangleIndices.empty()) {
        return {};
    }
    
    // Build edge to triangle map
    std::map<std::pair<int, int>, std::vector<int>> edgeToTriangles;
    
    for (int triIdx = 0; triIdx < static_cast<int>(triangleIndices.size()); ++triIdx) {
        const auto& triangle = triangleIndices[triIdx];
        
        // Add all 3 edges of this triangle
        for (int i = 0; i < 3; ++i) {
            int v1 = triangle[i];
            int v2 = triangle[(i + 1) % 3];
            
            // Create edge key with smaller vertex index first for consistency
            std::pair<int, int> edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
            
            edgeToTriangles[edge].push_back(triIdx);
        }
    }
    
    // Initialize triangle classes (-1 = unprocessed)
    std::vector<int> triangleClass(triangleIndices.size(), -1);
    std::vector<std::vector<int>> islands;
    
    // BFS to find connected components
    for (int triIdx = 0; triIdx < static_cast<int>(triangleIndices.size()); ++triIdx) {
        if (triangleClass[triIdx] != -1) {
            continue; // Already processed
        }
        
        // Start new island
        int currentIslandClass = static_cast<int>(islands.size());
        islands.emplace_back();
        std::queue<int> queue;
        
        // Add starting triangle
        queue.push(triIdx);
        triangleClass[triIdx] = currentIslandClass;
        
        // BFS expansion
        while (!queue.empty()) {
            int currentTriIdx = queue.front();
            queue.pop();
            islands[currentIslandClass].push_back(currentTriIdx);
            
            const auto& currentTriangle = triangleIndices[currentTriIdx];
            
            // Check all 3 edges for neighboring triangles
            for (int i = 0; i < 3; ++i) {
                int v1 = currentTriangle[i];
                int v2 = currentTriangle[(i + 1) % 3];
                std::pair<int, int> edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                
                auto edgeIt = edgeToTriangles.find(edge);
                if (edgeIt != edgeToTriangles.end()) {
                    for (int neighborTriIdx : edgeIt->second) {
                        if (neighborTriIdx != currentTriIdx && triangleClass[neighborTriIdx] == -1) {
                            triangleClass[neighborTriIdx] = currentIslandClass;
                            queue.push(neighborTriIdx);
                        }
                    }
                }
            }
        }
    }
    
    return islands;
}