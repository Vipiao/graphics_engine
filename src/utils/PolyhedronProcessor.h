#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <set>
#include <array>

// Custom comparator for ivec3 for set operations
struct IVec3Compare {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const;
};

// Custom comparator for dvec3 with tolerance for set operations
struct Vec3Compare {
    static constexpr double eps = 1e-9;
    
    bool operator()(const glm::dvec3& a, const glm::dvec3& b) const;
};

class PolyhedronProcessor {
public:
    /**
     * @brief Complete mesh data structure for rendering
     */
    struct MeshData {
        std::vector<glm::dvec3> positions;
        std::vector<glm::dvec3> normals;
        std::vector<glm::dvec3> tangents;
        std::vector<glm::dvec2> uvs;
        
        bool isEmpty() const { return positions.empty(); }
    };

    struct AxisResult {
        std::vector<glm::dvec3> faceAxis;
        std::vector<glm::dvec3> edgeAxis;
        std::vector<std::array<int, 2>> edges;  // Edge connectivity: pairs of vertex indices
    };

    /**
     * Extract face normals and edge directions from modified cube vertices
     * @param vertices Vector of 8 cube vertices (possibly modified)
     * @param maxSize Maximum coordinate value (halfWidth = maxSize/2)
     * @return AxisResult containing unique face normals and edge directions
     */
    static AxisResult getAxis(const std::vector<glm::ivec3>& vertices, int maxSize);

    /**
     * Extract unique vertices and normalize them
     * @param vertices Input vertices
     * @param maxSize Maximum coordinate value for normalization
     * @return Vector of unique normalized vertices
     */
    static std::vector<glm::dvec3> getUniqueVertices(const std::vector<glm::ivec3>& vertices, int maxSize);

    /**
     * Extract triangles with non-zero area from the cube faces
     * @param vertices Vector of 8 cube vertices
     * @param maxSize Maximum coordinate value for normalization
     * @return Vector of triangles (each triangle is 3 vertices)
     */
    static std::vector<std::array<glm::dvec3, 3>> getTriangles(const std::vector<glm::ivec3>& vertices, int maxSize);

    /**
     * Extract triangle indices with non-zero area from the cube faces
     * @param vertices Vector of 8 cube vertices
     * @param maxSize Maximum coordinate value for normalization
     * @return Vector of triangle indices (each triangle is 3 indices into vertices array)
     */
    static std::vector<std::array<int, 3>> getTriangleIndices(const std::vector<glm::ivec3>& vertices, int maxSize);

    /**
     * Extract triangle indices from normalized vertices
     * @param vertices Vector of 8 normalized vertices in [0,1] space
     * @return Vector of triangle indices (each triangle is 3 indices into vertices array)
     */
    static std::vector<std::array<int, 3>> getTriangleIndices(const std::vector<glm::dvec3>& vertices);

    /**
     * Validate if the polyhedron formed by the vertices is valid
     * @param vertices Vector of 8 cube vertices
     * @param maxSize Maximum coordinate value for normalization
     * @param normalThreshold Threshold for nearly opposite normals (default -0.9)
     * @param convexityMargin Margin for convexity check (default -0.1)
     * @return true if the polyhedron is valid, false otherwise
     */
    static bool validatePolyhedron(const std::vector<glm::ivec3>& vertices, int maxSize, 
                                   double normalThreshold = -0.98, double convexityMargin = -0.01);
 
    /**
     * @brief Generate complete mesh data with normals, tangents, and UVs from triangle data
     * @param triangles Vector of triangles (each triangle is 3 vertices)
     * @return MeshData containing all vertex attributes for rendering
     */
    static MeshData generateMeshData(const std::vector<std::array<glm::dvec3, 3>>& triangles);

    /**
     * @brief Calculate geometric center (average of vertices)
     * @param vertices Vector of integer vertices
     * @return Geometric center as normalized double vector
     */
    static glm::dvec3 getGeometricCenter(const std::vector<glm::ivec3>& vertices);

    /**
     * @brief Calculate volume of a tetrahedron
     * @param apex Apex vertex of tetrahedron
     * @param v1, v2, v3 Base triangle vertices
     * @return Volume of tetrahedron
     */
    static double calculateTetrahedronVolume(const glm::dvec3& apex, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3);

    /**
     * @brief Calculate centroid of a tetrahedron
     * @param apex Apex vertex of tetrahedron
     * @param v1, v2, v3 Base triangle vertices
     * @return Centroid of tetrahedron
     */
    static glm::dvec3 calculateTetrahedronCentroid(const glm::dvec3& apex, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3);

    /**
     * @brief Generate standard cube vertices in local space
     * @param width Width of the cube (edge length)
     * @return Vector of 8 vertices ordered: bottom (CCW from -X-Y), top (CCW from -X-Y)
     */
    static std::vector<glm::dvec3> generateCubeVertices(double width);

    /**
     * @brief Generate standard cube axes (X, Y, Z)
     * @return Vector of 3 orthogonal unit axes
     */
    static std::vector<glm::dvec3> generateCubeAxes();

    /**
     * @brief Get standard cube edge connectivity
     * @return Vector of 12 edge pairs (vertex index pairs)
     */
    static std::vector<std::array<int, 2>> generateCubeEdges();

    /**
     * @brief Check if a 2D point is inside a convex polygon with margin
     * @param point The 2D point to test
     * @param polygon Vector of polygon vertices ordered counter-clockwise
     * @param margin Inward margin to expand the "inside" region
     * @return true if point is inside the polygon (with margin), false otherwise
     */
    static bool isPointInConvexPolygon(
        const glm::dvec2& point,
        const std::vector<glm::dvec2>& polygon,
        double margin = 0.0);

    /**
     * @brief Check convexity of a set of triangles using vertex indices
     * @param vertices Vector of vertices
     * @param triangleIndices Vector of triangles (each triangle is 3 indices into vertices)
     * @param counterClockwise Whether triangles use counter-clockwise winding for outward normals
     * @return true if the triangles form a convex shape, false otherwise
     */
    static bool areTrianglesConvex(
        const std::vector<glm::dvec3>& vertices,
        const std::vector<std::array<int, 3>>& triangleIndices,
        const std::vector<bool>& triangleMask,
        bool counterClockwise = true);
    
    /**
     * @brief Check if the triangulated surface has at least one convex vertex
     * @param vertices Vector of vertices
     * @param triangleIndices Vector of triangles (each triangle is 3 indices into vertices)
     * @param verticesToTest Indices of vertices to test for convexity (for performance)
     * @return true if at least one vertex is convex, false otherwise
     */
    static bool hasAtLeastOneConvexVertex(
        const std::vector<glm::dvec3>& vertices,
        const std::vector<std::array<int, 3>>& triangleIndices,
        const std::vector<int>& verticesToTest);

    /**
     * @brief Default cube vertices in standard cube order
     * Coordinate system: +X right, +Y forward, +Z up
     * Vertices ordered: bottom (CCW from -X-Y), top (CCW from -X-Y)
     * @return Array of 8 cube vertices in integer coordinates [0..MAX_SIZE]
     */
    static const std::array<glm::ivec3, 8> DEFAULT_VERTICES;

    /**
     * @brief Enum for standard cube vertex indices
     * Matches the ordering in generateCubeVertices: (-/+X, -/+Y, -/+Z)
     */
    enum CubeVertex {
        BOTTOM_BACK_LEFT = 0,   // (-x, -y, -z)
        BOTTOM_BACK_RIGHT = 1,  // (+x, -y, -z)
        BOTTOM_FRONT_RIGHT = 2, // (+x, +y, -z)
        BOTTOM_FRONT_LEFT = 3,  // (-x, +y, -z)
        TOP_BACK_LEFT = 4,      // (-x, -y, +z)
        TOP_BACK_RIGHT = 5,     // (+x, -y, +z)
        TOP_FRONT_RIGHT = 6,    // (+x, +y, +z)
        TOP_FRONT_LEFT = 7      // (-x, +y, +z)
    };

    /**
     * @brief Check convexity of triangles in a specific direction
     * @param triangles Vector of triangles (each triangle is 3 vertices)
     * @param direction Direction vector to project normals onto for convexity check
     * @param counterClockwise Whether triangles use counter-clockwise winding for outward normals
     * @return true if the triangles are convex in the given direction, false otherwise
     */
    static bool areTrianglesConvexInDirection(
        const std::vector<std::array<glm::dvec3, 3>>& triangles,
        const glm::dvec3& direction,
        bool counterClockwise = true);

    /**
     * @brief Check polyhedron border intersection between two adjacent grid cells
     * @param coordA Grid coordinate of polyhedron A
     * @param verticesA Vertices of polyhedron A in local space [0,1]
     * @param coordB Grid coordinate of polyhedron B  
     * @param verticesB Vertices of polyhedron B in local space [0,1]
     * @return Vector of bools indicating which vertices of A are inside B's border region
     */
    static std::vector<bool> checkPolyhedronBorderIntersection(
        const glm::ivec3& coordA, const std::vector<glm::dvec3>& verticesA,
        const glm::ivec3& coordB, const std::vector<glm::dvec3>& verticesB);

    /**
     * @brief Check if two triangles are adjacent (share an edge) with consistent winding
     * @param triangleA First triangle (3 vertices)
     * @param triangleB Second triangle (3 vertices)
     * @param tolerance Tolerance for vertex comparison (default 1e-9)
     * @return true if triangles share exactly one edge with consistent winding, false otherwise
     */
    static bool areTrianglesAdjacent(const std::array<glm::dvec3, 3>& triangleA, const std::array<glm::dvec3, 3>& triangleB, double tolerance = Vec3Compare::eps);

    /**
     * @brief Group triangles into connected islands based on edge sharing
     * @param vertices Vector of 3D vertices
     * @param triangleIndices Vector of triangles (each triangle = 3 indices into vertices)
     * @return Vector of islands, where each island is a vector of triangle indices
     */
    static std::vector<std::vector<int>> groupTrianglesIntoIslands(
        const std::vector<glm::dvec3>& vertices,
        const std::vector<std::array<int, 3>>& triangleIndices);

    /**
     * Calculate the normal of a triangle
     * @param triangle Triangle vertices
     * @return Normalized normal vector
     */
    static glm::dvec3 getTriangleNormal(const std::array<glm::dvec3, 3>& triangle);

    /**
     * Count shared vertices between two triangles
     * @param t1, t2 Triangle vertices to compare
     * @param tolerance Tolerance for vertex comparison
     * @return Number of shared vertices (0, 1, 2, or 3)
     */
    static int countSharedVertices(const std::array<glm::dvec3, 3>& t1, const std::array<glm::dvec3, 3>& t2, double tolerance = Vec3Compare::eps);

private:
    // Face definitions for cube (indices into vertex array)  
    // Coordinate system: +X right, +Y forward, +Z up (right-handed)
    // Vertices ordered counter-clockwise when viewed from outside
    static constexpr std::array<std::array<int, 4>, 6> CUBE_FACES = {{
        {0, 3, 2, 1}, // Bottom face (Z = -halfWidth)
        {4, 5, 6, 7}, // Top face (Z = +halfWidth)
        {0, 1, 5, 4}, // Back face (Y = -halfWidth)
        {2, 3, 7, 6}, // Front face (Y = +halfWidth)
        {0, 4, 7, 3}, // Left face (X = -halfWidth)
        {1, 2, 6, 5}  // Right face (X = +halfWidth)
    }};

    // Edge definitions for cube (12 edges total)
    static constexpr std::array<std::array<int, 2>, 12> CUBE_EDGES = {{
        // Bottom face edges (Z = -halfWidth)
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        // Top face edges (Z = +halfWidth)
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        // Vertical edges connecting bottom to top
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    }};

    /**
     * Check if a quad is convex and return appropriate triangulation
     * @param v0, v1, v2, v3 The quad vertices in counter-clockwise order
     * @return Array of 2 triangles: either [(v0,v1,v2),(v0,v2,v3)] or [(v0,v1,v3),(v3,v1,v2)]
     */
    static std::array<std::array<int, 3>, 2> getConvexTriangulation(
        const glm::ivec3& v0, const glm::ivec3& v1, const glm::ivec3& v2, const glm::ivec3& v3,
        int idx0, int idx1, int idx2, int idx3);
    
    /**
     * Check if a quad is convex and return appropriate triangulation (dvec3 version)
     * @param v0, v1, v2, v3 The quad vertices in counter-clockwise order
     * @return Array of 2 triangles: either [(v0,v1,v2),(v0,v2,v3)] or [(v0,v1,v3),(v3,v1,v2)]
     */
    static std::array<std::array<int, 3>, 2> getConvexTriangulation(
        const glm::dvec3& v0, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3,
        int idx0, int idx1, int idx2, int idx3);

    /**
     * Calculate the center (centroid) of a triangle
     * @param triangle Triangle vertices
     * @return Center point of the triangle
     */
    static glm::dvec3 getTriangleCenter(const std::array<glm::dvec3, 3>& triangle);
};