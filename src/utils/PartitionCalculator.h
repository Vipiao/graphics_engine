// PartitionCalculator.h
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include "AStar.h"
#include "HashFunctions.h"

template<typename CellType, typename CellHash = Hash::IVec3Hash>
class PartitionCalculator {
public:
    struct PartitionResult {
        bool hasSplit = false;
        std::vector<std::vector<glm::ivec3>> partitions;
    };
    
    /**
     * @brief Analyze if edge blocks are connected and find partitions if split
     * @param cellMap Pointer to map of cells (coordinate -> cell)
     * @param edgeBlocks Vector of edge block coordinates to test connectivity
     * @param getNeighbors Function that returns connected neighbors for a cell: std::vector<glm::ivec3>(const CellType&)
     * @return PartitionResult containing split status and partition coordinates
     */
    template<typename CellMap, typename NeighborFunction>
    static PartitionResult analyzePartitions(
        const CellMap* cellMap,
        const std::vector<glm::ivec3>& edgeBlocks,
        NeighborFunction getNeighbors);

private:
    template<typename CellMap, typename NeighborFunction>
    static std::vector<glm::ivec3> floodFillPartition(
        const CellMap* cellMap,
        const glm::ivec3& startCoord,
        std::unordered_set<glm::ivec3, CellHash>& visited,
        NeighborFunction getNeighbors);
    
    static double manhattanDistance(const glm::ivec3& a, const glm::ivec3& b);
};

// Template implementations
template<typename CellType, typename CellHash>
template<typename CellMap, typename NeighborFunction>
typename PartitionCalculator<CellType, CellHash>::PartitionResult 
PartitionCalculator<CellType, CellHash>::analyzePartitions(
    const CellMap* cellMap,
    const std::vector<glm::ivec3>& edgeBlocks,
    NeighborFunction getNeighbors) {
    
    PartitionResult result;
    
    if (!cellMap || edgeBlocks.empty()) {
        return result;
    }
    
    // Step 1: Filter edge blocks to only include those that exist in the cell map
    std::vector<glm::ivec3> validEdgeBlocks;
    for (const glm::ivec3& coord : edgeBlocks) {
        if (cellMap->find(coord) != cellMap->end()) {
            validEdgeBlocks.push_back(coord);
        }
    }
    
    if (validEdgeBlocks.size() <= 1) {
        // No split possible with 0 or 1 edge blocks
        return result;
    }
    
    // Step 2: Test connectivity using A* with dynamic goals
    glm::ivec3 startBlock = validEdgeBlocks[0];
    std::unordered_set<glm::ivec3, CellHash> targetsToFind;
    
    // Add all other edge blocks as targets (excluding the start block)
    for (size_t i = 1; i < validEdgeBlocks.size(); ++i) {
        targetsToFind.insert(validEdgeBlocks[i]);
    }
    
    std::unordered_set<glm::ivec3, CellHash> foundTargets;
    glm::ivec3 currentTarget = validEdgeBlocks[1]; // Start with second edge block as target
    
    auto astarResult = AStar<glm::ivec3, CellHash>::search(
        startBlock,
        [&](const glm::ivec3& node) {
            // Goal function with dynamic targets
            if (targetsToFind.find(node) != targetsToFind.end()) {
                foundTargets.insert(node);
                targetsToFind.erase(node);
                
                // Switch to next unreached target if available
                if (!targetsToFind.empty()) {
                    currentTarget = *targetsToFind.begin();
                    return false; // Continue searching for remaining targets
                }
            }
            return targetsToFind.empty(); // All targets found
        },
        [&](const glm::ivec3& node, auto callback) {
            // Neighbor expansion function
            auto nodeIt = cellMap->find(node);
            if (nodeIt == cellMap->end()) {
                return; // Node doesn't exist in map
            }
            
            std::vector<glm::ivec3> neighbors = getNeighbors(nodeIt->second);
            for (const glm::ivec3& neighbor : neighbors) {
                // Only include neighbors that exist in the cell map
                if (cellMap->find(neighbor) != cellMap->end()) {
                    callback(neighbor, 1.0); // Unit cost for each step
                }
            }
        },
        [&](const glm::ivec3& node) {
            // Heuristic function - Manhattan distance to current target
            return manhattanDistance(node, currentTarget);
        }
    );
    
    // Step 3: Check if all edge blocks were reached
    if (foundTargets.size() == validEdgeBlocks.size() - 1) {
        // All edge blocks are connected - no split
        result.hasSplit = false;
        return result;
    }
    
    // Step 4: Split detected - find partitions using flood fill
    result.hasSplit = true;
    std::unordered_set<glm::ivec3, CellHash> processedCells;
    
    // Start with all valid edge blocks and flood fill to find their partitions
    for (const glm::ivec3& edgeBlock : validEdgeBlocks) {
        if (processedCells.find(edgeBlock) == processedCells.end()) {
            // This edge block hasn't been processed yet - start a new partition
            std::vector<glm::ivec3> partition = floodFillPartition(
                cellMap, edgeBlock, processedCells, getNeighbors);
            
            if (!partition.empty()) {
                result.partitions.push_back(partition);
            }
        }
    }
    
    return result;
}

template<typename CellType, typename CellHash>
template<typename CellMap, typename NeighborFunction>
std::vector<glm::ivec3> PartitionCalculator<CellType, CellHash>::floodFillPartition(
    const CellMap* cellMap,
    const glm::ivec3& startCoord,
    std::unordered_set<glm::ivec3, CellHash>& visited,
    NeighborFunction getNeighbors) {
    
    std::vector<glm::ivec3> partition;
    std::vector<glm::ivec3> toProcess = {startCoord};
    
    while (!toProcess.empty()) {
        glm::ivec3 current = toProcess.back();
        toProcess.pop_back();
        
        // Skip if already visited
        if (visited.find(current) != visited.end()) {
            continue;
        }
        
        // Skip if cell doesn't exist
        auto cellIt = cellMap->find(current);
        if (cellIt == cellMap->end()) {
            continue;
        }
        
        // Mark as visited and add to partition
        visited.insert(current);
        partition.push_back(current);
        
        // Add neighbors to processing queue
        std::vector<glm::ivec3> neighbors = getNeighbors(cellIt->second);
        for (const glm::ivec3& neighbor : neighbors) {
            if (visited.find(neighbor) == visited.end() && 
                cellMap->find(neighbor) != cellMap->end()) {
                toProcess.push_back(neighbor);
            }
        }
    }
    
    return partition;
}

template<typename CellType, typename CellHash>
double PartitionCalculator<CellType, CellHash>::manhattanDistance(const glm::ivec3& a, const glm::ivec3& b) {
    return static_cast<double>(std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z));
}