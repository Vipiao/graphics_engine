#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <limits>
#include <algorithm>
#include <functional>
#include "Generator.h"

template<typename Node, typename Hash = std::hash<Node>>
class AStar {
public:
    struct Result {
        bool found = false;
        std::vector<Node> path;
        double totalCost = 0.0;
    };
    
    template<typename IsTargetF, typename GetNeighborsF, typename HeuristicF>
    static Result search(
        const Node& start,
        IsTargetF isTarget,
        GetNeighborsF getNeighbors,
        HeuristicF heuristic
    );

    template<typename IsTargetF, typename GetNeighborsF, typename HeuristicF>
    static Generator<Result> searchGenerator(
        const Node& start,
        IsTargetF isTarget,
        GetNeighborsF getNeighbors,
        HeuristicF heuristic
    );

private:
    struct NodeInfo {
        Node node;
        double gCost = std::numeric_limits<double>::infinity();
        double hCost = 0.0;
        Node parent;
        bool hasParent = false;
        bool inClosed = false;
        
        double fCost() const { return gCost + hCost; }
    };
    
    // Comparator for priority queue with NodeInfo pointers
    struct NodeInfoComparator {
        bool operator()(const NodeInfo* a, const NodeInfo* b) const {
            return a->fCost() > b->fCost(); // Min-heap
        }
    };
    
    static std::vector<Node> reconstructPath(
        const std::unordered_map<Node, NodeInfo, Hash>& nodeInfo,
        const Node& target
    );
};

template<typename Node, typename Hash>
template<typename IsTargetF, typename GetNeighborsF, typename HeuristicF>
typename AStar<Node, Hash>::Result AStar<Node, Hash>::search(
    const Node& start,
    IsTargetF isTarget,
    GetNeighborsF getNeighbors,
    HeuristicF heuristic
) {
    std::unordered_map<Node, NodeInfo, Hash> nodeInfo;
    std::priority_queue<NodeInfo*, std::vector<NodeInfo*>, NodeInfoComparator> openQueue;
    
    // Step 1: Initialize start node
    NodeInfo& startInfo = nodeInfo[start];
    startInfo.node = start;
    startInfo.gCost = 0.0;
    startInfo.hCost = heuristic(start);
    
    openQueue.push(&startInfo);
    
    while (!openQueue.empty()) {
        // Step 2: Get best node from queue
        NodeInfo* current = openQueue.top();
        openQueue.pop();
        
        auto& currentInfo = nodeInfo[current->node];
        
        // Skip stale entries (outdated f-cost due to better path found later)
        if (currentInfo.inClosed || currentInfo.fCost() < current->fCost()) {
            continue;
        }
        
        // Move to closed set
        currentInfo.inClosed = true;
        
        // Check if target reached
        if (isTarget(current->node)) {
            Result result;
            result.found = true;
            result.path = reconstructPath(nodeInfo, current->node);
            result.totalCost = currentInfo.gCost;
            return result;
        }
        
        // Step 3: Process neighbors
        getNeighbors(current->node, [&](const Node& neighbor, double edgeCost) {
            NodeInfo& neighborInfo = nodeInfo[neighbor];
            
            // Skip if already in closed set
            if (neighborInfo.inClosed) {
                return;
            }
            
            double tentativeGCost = currentInfo.gCost + edgeCost;
            
            // If this is a better path to neighbor
            if (tentativeGCost < neighborInfo.gCost) {
                neighborInfo.node = neighbor;
                neighborInfo.gCost = tentativeGCost;
                neighborInfo.hCost = heuristic(neighbor);
                neighborInfo.parent = current->node;
                neighborInfo.hasParent = true;
                
                // Add to queue (old entries will be detected as stale)
                openQueue.push(&neighborInfo);
            }
        });
    }
    
    // No path found
    return Result{};
}

template<typename Node, typename Hash>
template<typename IsTargetF, typename GetNeighborsF, typename HeuristicF>
Generator<typename AStar<Node, Hash>::Result> AStar<Node, Hash>::searchGenerator(
    const Node& start,
    IsTargetF isTarget,
    GetNeighborsF getNeighbors,
    HeuristicF heuristic
) {
    std::unordered_map<Node, NodeInfo, Hash> nodeInfo;
    std::priority_queue<NodeInfo*, std::vector<NodeInfo*>, NodeInfoComparator> openQueue;
    
    // Step 1: Initialize start node
    NodeInfo& startInfo = nodeInfo[start];
    startInfo.node = start;
    startInfo.gCost = 0.0;
    startInfo.hCost = heuristic(start);
    
    openQueue.push(&startInfo);
    
    int nodesProcessed = 0;
    constexpr int YIELD_INTERVAL = 20; // Yield every 20 nodes processed
    
    while (!openQueue.empty()) {
        // Step 2: Get best node from queue
        NodeInfo* current = openQueue.top();
        openQueue.pop();
        
        auto& currentInfo = nodeInfo[current->node];
        
        // Skip stale entries (outdated f-cost due to better path found later)
        if (currentInfo.inClosed || currentInfo.fCost() < current->fCost()) {
            continue;
        }
        
        // Move to closed set
        currentInfo.inClosed = true;
        nodesProcessed++;
        
        // Check if target reached
        if (isTarget(current->node)) {
            Result result;
            result.found = true;
            result.path = reconstructPath(nodeInfo, current->node);
            result.totalCost = currentInfo.gCost;
            co_yield result;
            co_return;
        }
        
        // Step 3: Process neighbors
        getNeighbors(current->node, [&](const Node& neighbor, double edgeCost) {
            NodeInfo& neighborInfo = nodeInfo[neighbor];
            
            // Skip if already in closed set
            if (neighborInfo.inClosed) {
                return;
            }
            
            double tentativeGCost = currentInfo.gCost + edgeCost;
            
            // If this is a better path to neighbor
            if (tentativeGCost < neighborInfo.gCost) {
                neighborInfo.node = neighbor;
                neighborInfo.gCost = tentativeGCost;
                neighborInfo.hCost = heuristic(neighbor);
                neighborInfo.parent = current->node;
                neighborInfo.hasParent = true;
                
                // Add to queue (old entries will be detected as stale)
                openQueue.push(&neighborInfo);
            }
        });
        
        // Yield periodically to allow time checking
        if (nodesProcessed % YIELD_INTERVAL == 0) {
            co_yield Result{}; // Empty result indicates more work needed
        }
    }
    
    // No path found
    co_yield Result{};
    co_return;
}

template<typename Node, typename Hash>
std::vector<Node> AStar<Node, Hash>::reconstructPath(
    const std::unordered_map<Node, NodeInfo, Hash>& nodeInfo,
    const Node& target
) {
    std::vector<Node> path;
    Node current = target;
    
    while (true) {
        path.push_back(current);
        
        auto it = nodeInfo.find(current);
        if (it == nodeInfo.end() || !it->second.hasParent) {
            break;
        }
        
        current = it->second.parent;
    }
    
    std::reverse(path.begin(), path.end());
    return path;
}