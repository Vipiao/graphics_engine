#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <chrono>
#include "HashFunctions.h"
#include "TimeHandler.h"
#include "AStar.h"
#include "Generator.h"

// Cell interface for stochastic analysis
class IStochasticCell {
public:
    int cost = 1; // Cost for stochastic analysis

    virtual ~IStochasticCell() = default;
    
    // Call callback for each connected neighbor position
    virtual void forEachConnectedNeighbor(std::function<void(const glm::ivec3&)> callback) const = 0;

    // Default implementations for cost management
    int getCost() const { return cost; }
    void setCost(int newCost) { cost = newCost; }
};

template<typename CellType>
class StochasticAnalyzer {
public:
    enum class AnalysisState {
        SELECTING_RANDOM_CELLS,
        PATHFINDING_INIT,
        PATHFINDING_STEPPING,
        UPDATING_PATH_COSTS,
        ANALYSIS_COMPLETE
    };

    StochasticAnalyzer(const std::unordered_map<glm::ivec3, CellType, Hash::IVec3Hash>& cells);
    ~StochasticAnalyzer();
    
    // Perform analysis until endTime, return true if more work needed
    bool performAnalysisUntil(std::chrono::time_point<std::chrono::high_resolution_clock> endTime, TimeHandler& timeHandler);

    // Reset analysis state (call when starting new analysis)
    void resetAnalysis();
    
private:
    // Initialize all cell costs to 1 and populate cache
    void initializeCostsAndCache();
    
    // Increment cost of cells along path
    void incrementPathCosts(const std::vector<glm::ivec3>& path);
    
    // Select two random cells with cost 1, removing invalid ones from cache
    std::pair<bool, std::pair<glm::ivec3, glm::ivec3>> selectRandomCellsWithCostOne();
    
    // Get cell at position
    CellType* getCell(const glm::ivec3& pos) const;
    
    // Get deterministic random index
    size_t getRandomIndex(size_t maxValue);
    
private:
    const std::unordered_map<glm::ivec3, CellType, Hash::IVec3Hash>& m_cells;

    // State machine variables
    AnalysisState m_analysisState = AnalysisState::SELECTING_RANDOM_CELLS;
    glm::ivec3 m_selectedStart;
    glm::ivec3 m_selectedEnd;

    // A* generator state
    std::unique_ptr<Generator<typename AStar<glm::ivec3, Hash::IVec3Hash>::Result>> m_astarGenerator;
    std::vector<glm::ivec3> m_foundPath;
    bool m_pathExists = false;
    
    // Cache of cell positions that may have cost 1 (shrinks over time)
    std::vector<glm::ivec3> m_costOneCellsCache;
    
    // Deterministic random counter
    inline static size_t s_randomCounter = 0;
};

// Template implementations (must be in header)
template<typename CellType>
StochasticAnalyzer<CellType>::StochasticAnalyzer(const std::unordered_map<glm::ivec3, CellType, Hash::IVec3Hash>& cells) 
    : m_cells(cells)
{
    initializeCostsAndCache();
}

template<typename CellType>
StochasticAnalyzer<CellType>::~StochasticAnalyzer() = default;

template<typename CellType>
void StochasticAnalyzer<CellType>::resetAnalysis() {
    m_analysisState = AnalysisState::SELECTING_RANDOM_CELLS;
    m_astarGenerator.reset(); // Clean up any existing generator
    initializeCostsAndCache(); // Reset all costs to 1 and rebuild cache
}

template<typename CellType>
void StochasticAnalyzer<CellType>::initializeCostsAndCache() {
    m_costOneCellsCache.clear();
    
    for (auto& [pos, cell] : m_cells) {
        const_cast<CellType&>(cell).setCost(1);
        m_costOneCellsCache.push_back(pos);
    }
}

template<typename CellType>
CellType* StochasticAnalyzer<CellType>::getCell(const glm::ivec3& pos) const {
    auto it = m_cells.find(pos);
    return (it != m_cells.end()) ? const_cast<CellType*>(&it->second) : nullptr;
}

template<typename CellType>
size_t StochasticAnalyzer<CellType>::getRandomIndex(size_t maxValue) {
    if (maxValue == 0) return 0;
    const size_t largePrime = 1000000007;
    return ((s_randomCounter++) * largePrime) % maxValue;
}

template<typename CellType>
bool StochasticAnalyzer<CellType>::performAnalysisUntil(
    std::chrono::time_point<std::chrono::high_resolution_clock> endTime, 
    TimeHandler& timeHandler) {
    
    std::chrono::time_point<std::chrono::high_resolution_clock> current;
    while ((current = timeHandler.now()) < endTime) {
    //while (true) {
        switch (m_analysisState) {
            case AnalysisState::SELECTING_RANDOM_CELLS:
                // Check if we have enough cells to continue
                if (m_costOneCellsCache.size() < 2) {
                    m_analysisState = AnalysisState::ANALYSIS_COMPLETE;
                    break;
                }
                
                // Select two random cells with cost 1
                {
                    auto [success, cellPair] = selectRandomCellsWithCostOne();
                    if (!success) {
                        m_analysisState = AnalysisState::ANALYSIS_COMPLETE;
                        break;
                    }
                    m_selectedStart = cellPair.first;
                    m_selectedEnd = cellPair.second;
                }
                
                m_analysisState = AnalysisState::PATHFINDING_INIT;
                break;
                
            case AnalysisState::PATHFINDING_INIT:
                // Initialize A* generator
                m_pathExists = false;
                m_foundPath.clear();
                m_astarGenerator = std::make_unique<Generator<typename AStar<glm::ivec3, Hash::IVec3Hash>::Result>>(
                    AStar<glm::ivec3, Hash::IVec3Hash>::searchGenerator(
                        m_selectedStart,
                        [this](const glm::ivec3& node) {
                            // Goal test - return true when we reach the end
                            return node == m_selectedEnd;
                        },
                        [this](const glm::ivec3& node, auto callback) {
                            // Neighbor expansion - call callback for each valid neighbor
                            auto cell = getCell(node);
                            if (!cell) {
                                return;
                            }
                            
                            cell->forEachConnectedNeighbor([&](const glm::ivec3& neighbor) {
                                auto neighborCell = getCell(neighbor);
                                if (neighborCell) {
                                    double cost = static_cast<double>(neighborCell->getCost());
                                    callback(neighbor, cost);
                                }
                            });
                        },
                        [this](const glm::ivec3& node) {
                            // Heuristic function - Manhattan distance
                            return static_cast<double>(
                                std::abs(node.x - m_selectedEnd.x) + 
                                std::abs(node.y - m_selectedEnd.y) + 
                                std::abs(node.z - m_selectedEnd.z)
                            );
                        }
                    )
                );
                
                // Start the generator
                ++(*m_astarGenerator);
                m_analysisState = AnalysisState::PATHFINDING_STEPPING;
                break;
                
            case AnalysisState::PATHFINDING_STEPPING:
                // Step through A* generator until complete
                if (*m_astarGenerator) {
                    ++(*m_astarGenerator);
                    // Generator is still running, yield control
                    break;
                } else {
                    // Generator finished, get result
                    auto result = (*m_astarGenerator)();
                    m_pathExists = result.found;
                    if (m_pathExists) {
                        m_foundPath = result.path;
                    }

                    // Clean up generator
                    m_astarGenerator.reset();
                    m_analysisState = AnalysisState::UPDATING_PATH_COSTS;
                }
                break;
                
            case AnalysisState::UPDATING_PATH_COSTS:
                if (m_pathExists) {
                    incrementPathCosts(m_foundPath);
                } else {
                    incrementPathCosts({m_selectedStart, m_selectedEnd});
                }
                
                m_analysisState = AnalysisState::SELECTING_RANDOM_CELLS;
                break;
                
            case AnalysisState::ANALYSIS_COMPLETE:
                return false; // Analysis complete
        }
    }
    
    return true; // Time ran out, more work needed
}

template<typename CellType>
void StochasticAnalyzer<CellType>::incrementPathCosts(const std::vector<glm::ivec3>& path) {
    for (const auto& pos : path) {
        auto cell = getCell(pos);
        if (cell) {
            cell->setCost(cell->getCost() + 1);
        }
    }
}

template<typename CellType>
std::pair<bool, std::pair<glm::ivec3, glm::ivec3>> StochasticAnalyzer<CellType>::selectRandomCellsWithCostOne() {
    // Check if we have enough cells
    if (m_costOneCellsCache.size() < 2) {
        return {false, {}};
    }
    
    glm::ivec3 firstCell;
    glm::ivec3 secondCell;
    bool foundFirst = false;
    bool foundSecond = false;
    
    // Select first valid cell
    while (!foundFirst && !m_costOneCellsCache.empty()) {
        size_t firstIndex = getRandomIndex(m_costOneCellsCache.size());
        auto cell = getCell(m_costOneCellsCache[firstIndex]);
        
        if (cell && cell->getCost() == 1) {
            firstCell = m_costOneCellsCache[firstIndex];
            foundFirst = true;
        } else {
            // Remove invalid cell by swapping with last and popping
            m_costOneCellsCache[firstIndex] = m_costOneCellsCache.back();
            m_costOneCellsCache.pop_back();
        }
    }
    
    // Check if we still have enough cells for a second selection
    if (!foundFirst || m_costOneCellsCache.size() < 2) {
        return {false, {}};
    }
    
    // Select second valid cell (different from first)
    while (!foundSecond && m_costOneCellsCache.size() >= 2) {
        size_t secondIndex = getRandomIndex(m_costOneCellsCache.size());
        auto cell = getCell(m_costOneCellsCache[secondIndex]);
        
        if (cell && cell->getCost() == 1 && m_costOneCellsCache[secondIndex] != firstCell) {
            secondCell = m_costOneCellsCache[secondIndex];
            foundSecond = true;
        } else {
            // Remove invalid cell by swapping with last and popping
            m_costOneCellsCache[secondIndex] = m_costOneCellsCache.back();
            m_costOneCellsCache.pop_back();
        }
    }
    
    if (foundFirst && foundSecond) {
        return {true, {firstCell, secondCell}};
    } else {
        return {false, {}};
    }
}