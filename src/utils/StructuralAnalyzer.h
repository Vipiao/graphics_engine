// StructuralAnalyzer.h
#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include "AStar.h"
#include "HashFunctions.h"

/**
 * @brief Interface for nodes in the structural analysis grid
 */
class StructuralNode {
public:
    virtual ~StructuralNode() = default;
    
    /**
     * @brief Get coordinates that this node can connect to
     * Should return only coordinates where there is an actual structural connection
     * @return Vector of coordinates this node connects to
     */
    virtual std::vector<glm::ivec3> getConnectedNeighbors() const = 0;
    
    // Properties with default implementations
    int getPartitionId() const { return m_partitionId; }
    void setPartitionId(int id) { m_partitionId = id; }
    
    bool isWeak() const { return m_isWeak; }
    void setWeak(bool weak) { m_isWeak = weak; }

protected:
    int m_partitionId = 0;
    bool m_isWeak = false;
};

/**
 * @brief Parameters for structural analysis
 */
struct StructuralAnalysisParams {
    int searchRadius = 3;           // 0=1x1 inner, 1=3x3 inner, 2=5x5 inner, etc.
    int searchThreshold = 10;       // threshold parameter - max distance for connectivity search
};

/**
 * @brief High-performance structural integrity analyzer for 3D grid structures
 */
class StructuralAnalyzer {
public:
    explicit StructuralAnalyzer(const StructuralAnalysisParams& params = StructuralAnalysisParams{});
    
    /**
     * @brief Analyze structural integrity of a single node
     * @param analysisCenter The coordinate of the node to analyze
     * @param nodeGrid The complete grid for pathfinding
     * @return true if node is structurally sound, false if weak
     */
    template<typename NodeType>
    bool analyzeSingleNode(
        const glm::ivec3& analysisCenter,
        const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid);
    
    // Parameter accessors
    void setParams(const StructuralAnalysisParams& params) { m_params = params; }
    const StructuralAnalysisParams& getParams() const { return m_params; }

private:
    struct NeighborhoodAnalysis {
        std::unordered_set<glm::ivec3, Hash::IVec3Hash> innerRegion;     // Cells within inner cube
        std::unordered_set<glm::ivec3, Hash::IVec3Hash> perimeterCells;  // Cells in outer shell
        bool hasPerimeter = false;
    };
    
    // Internal analysis methods
    template<typename NodeType>
    NeighborhoodAnalysis findNeighborhood(
        const glm::ivec3& analysisCenter,
        const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid);
    
    template<typename NodeType>
    bool testPerimeterConnectivity(
        const std::unordered_set<glm::ivec3, Hash::IVec3Hash>& perimeterCells,
        const std::unordered_set<glm::ivec3, Hash::IVec3Hash>& innerRegion,
        const glm::ivec3& analysisCenter,
        const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid);
    
    // Helper methods
    template<typename NodeType>
    std::vector<glm::ivec3> getValidNeighbors(
        const glm::ivec3& coord,
        const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid) const;
    
    double manhattanDistance(const glm::ivec3& a, const glm::ivec3& b) const;
    bool isWithinCube(const glm::ivec3& point, const glm::ivec3& center, int radius) const;
    bool isWithinThreshold(const glm::ivec3& point, const glm::ivec3& center, int threshold) const;
    
    StructuralAnalysisParams m_params;
};

// Template implementations (must be in header)
template<typename NodeType>
bool StructuralAnalyzer::analyzeSingleNode(
    const glm::ivec3& analysisCenter,
    const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid)
{
    auto nodeIt = nodeGrid.find(analysisCenter);
    if (nodeIt == nodeGrid.end()) {
        throw std::invalid_argument("StructuralAnalyzer::analyzeSingleNode: analysis center node does not exist in grid");
    }
    
    // Step 1: Find neighborhood (inner region and perimeter cells)
    NeighborhoodAnalysis neighborhood = findNeighborhood(analysisCenter, nodeGrid);
    
    // If no perimeter cells found, consider it strong (edge case)
    if (!neighborhood.hasPerimeter || neighborhood.perimeterCells.empty()) {
        return true;
    }
    
    // Step 2: Test perimeter connectivity
    bool isConnected = testPerimeterConnectivity(neighborhood.perimeterCells, neighborhood.innerRegion, analysisCenter, nodeGrid);
    
    return isConnected;
}

template<typename NodeType>
StructuralAnalyzer::NeighborhoodAnalysis StructuralAnalyzer::findNeighborhood(
    const glm::ivec3& analysisCenter,
    const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid)
{
    NeighborhoodAnalysis result;
    
    // Use A* to find all reachable cells within outer cube
    int innerRadius = m_params.searchRadius;
    int outerRadius = m_params.searchRadius + 1;
    
    // Multi-target A* search
    std::unordered_set<glm::ivec3, Hash::IVec3Hash> targetsToFind;
    
    // Generate all potential targets within outer cube
    for (int x = -outerRadius; x <= outerRadius; ++x) {
        for (int y = -outerRadius; y <= outerRadius; ++y) {
            for (int z = -outerRadius; z <= outerRadius; ++z) {
                glm::ivec3 targetCoord = analysisCenter + glm::ivec3(x, y, z);
                if (nodeGrid.find(targetCoord) != nodeGrid.end()) {
                    targetsToFind.insert(targetCoord);
                }
            }
        }
    }
    
    if (targetsToFind.empty()) {
        return result;
    }
    
    // Multi-target A* search
    std::unordered_set<glm::ivec3, Hash::IVec3Hash> foundTargets;
    
    auto astarResult = AStar<glm::ivec3, Hash::IVec3Hash>::search(
        analysisCenter,
        [&](const glm::ivec3& node) {
            if (targetsToFind.find(node) != targetsToFind.end()) {
                foundTargets.insert(node);
            }
            return foundTargets.size() == targetsToFind.size();
        },
        [&](const glm::ivec3& node, auto callback) {
            auto neighbors = getValidNeighbors(node, nodeGrid);
            for (const auto& neighbor : neighbors) {
                callback(neighbor, 1.0); // Unit cost for each step
            }
        },
        [&](const glm::ivec3& node) {
            return manhattanDistance(node, analysisCenter);
        }
    );
    
    // Categorize found nodes
    for (const glm::ivec3& coord : foundTargets) {
        if (isWithinCube(coord, analysisCenter, innerRadius)) {
            result.innerRegion.insert(coord);
        } else {
            result.perimeterCells.insert(coord);
        }
    }
    
    result.hasPerimeter = !result.perimeterCells.empty();
    
    return result;
}

template<typename NodeType>
bool StructuralAnalyzer::testPerimeterConnectivity(
    const std::unordered_set<glm::ivec3, Hash::IVec3Hash>& perimeterCells,
    const std::unordered_set<glm::ivec3, Hash::IVec3Hash>& innerRegion,
    const glm::ivec3& analysisCenter,
    const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid)
{
    if (perimeterCells.size() <= 1) {
        return true; // Single or no perimeter cell is trivially connected
    }
    
    // Pick first perimeter cell as starting point
    glm::ivec3 startingPoint = *perimeterCells.begin();
    
    // Create target set (all perimeter cells except starting point)
    std::unordered_set<glm::ivec3, Hash::IVec3Hash> targetsToFind = perimeterCells;
    targetsToFind.erase(startingPoint);
    
    if (targetsToFind.empty()) {
        return true;
    }
    
    // Multi-target A* search from starting point to all other perimeter cells
    std::unordered_set<glm::ivec3, Hash::IVec3Hash> foundTargets;
    
    auto astarResult = AStar<glm::ivec3, Hash::IVec3Hash>::search(
        startingPoint,
        [&](const glm::ivec3& node) {
            if (targetsToFind.find(node) != targetsToFind.end()) {
                foundTargets.insert(node);
            }
            return foundTargets.size() == targetsToFind.size();
        },
        [&](const glm::ivec3& node, auto callback) {
            auto neighbors = getValidNeighbors(node, nodeGrid);
            for (const auto& neighbor : neighbors) {
                if (isWithinCube(neighbor, analysisCenter, m_params.searchThreshold) &&
                    innerRegion.find(neighbor) == innerRegion.end()) {
                    callback(neighbor, 1.0);
                }
            }
        },
        [&](const glm::ivec3& node) {
            return manhattanDistance(node, analysisCenter);
        }
    );
    
    // Return true if we found all targets (perimeter is fully connected)
    return foundTargets.size() == targetsToFind.size();
}

template<typename NodeType>
std::vector<glm::ivec3> StructuralAnalyzer::getValidNeighbors(
    const glm::ivec3& coord,
    const std::unordered_map<glm::ivec3, NodeType, Hash::IVec3Hash>& nodeGrid) const
{
    auto nodeIt = nodeGrid.find(coord);
    if (nodeIt == nodeGrid.end()) {
        return {}; // Return empty vector if node doesn't exist
    }
    
    const StructuralNode* currentNode = &nodeIt->second;
    return currentNode->getConnectedNeighbors();
}