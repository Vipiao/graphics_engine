// PairCache.h
#pragma once

#include <unordered_map>
#include <utility>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <queue>
#include <iostream>
#include "HashFunctions.h"

/**
 * @brief Generic global cache for storing data between pairs of objects with LRU eviction
 * 
 * Uses a global cache per DataType to ensure data is shared between all object pairs.
 * Template parameter DataType is the type of data to cache between object pairs.
 * Implements TTL-based eviction using expiration times.
 */
template<typename DataType>
class PairCache {
public:
    PairCache() = default;
    virtual ~PairCache() = default;
    
    /**
     * @brief Get const reference to cached data for a pair of objects from global cache
     * @param objA First object pointer (used as cache key)
     * @param objB Second object pointer (used as cache key)
     * @param currentTime Current time for expiration checking
     * @return Pointer to cached data, or nullptr if not found
     */
    static const DataType* getCachedData(int idA, int idB, uint64_t currentTime) {
        auto cacheKey = makeCacheKey(idA, idB);
        auto it = s_globalCache.find(cacheKey);
        
        if (it == s_globalCache.end()) {
            return nullptr; // No cached entry
        }
        
        const CachedInfo& info = it->second;
        
        // Check if entry has expired
        if (currentTime >= info.expiryTime) {
            s_globalCache.erase(it);
            return nullptr;
        }
        
        // Trigger periodic cleanup
        s_cleanupCounter++;
        if (s_cleanupCounter >= CLEANUP_FREQUENCY) {
            cleanupExpiredEntries(currentTime);
            s_cleanupCounter = 0;
        }
        
        return &info.data;
    }
    
    /**
     * @brief Set cached data for a pair of objects in global cache
     * @param idA First object ID (used as cache key)
     * @param idB Second object ID (used as cache key)
     * @param data Data to cache
     * @param currentTime Current time
     * @param expirationDuration How long the data should remain valid
     */
    static void setCachedData(int idA, int idB, DataType data, uint64_t currentTime, uint64_t expirationDuration) {
        auto cacheKey = makeCacheKey(idA, idB);
        uint64_t expiryTime = currentTime + expirationDuration;
        
        CachedInfo info;
        info.data = std::move(data);
        info.expiryTime = expiryTime;
        
        s_globalCache[cacheKey] = info;

        // Add to expiration queue
        s_expirationQueue.push(std::make_pair(expiryTime, cacheKey));
        
        // Trigger periodic cleanup
        s_cleanupCounter++;
        if (s_cleanupCounter >= CLEANUP_FREQUENCY) {
            cleanupExpiredEntries(currentTime);
            s_cleanupCounter = 0;
        }
    }

    /**
     * @brief Refresh expiry time for existing cached data without modifying the data
     * @param idA First object ID (used as cache key)
     * @param idB Second object ID (used as cache key)
     * @param currentTime Current time
     * @param expirationDuration New expiration duration from current time
     * @return True if cache entry existed and was refreshed, false otherwise
     */
    static bool refreshCachedData(int idA, int idB, uint64_t currentTime, uint64_t expirationDuration) {
        auto cacheKey = makeCacheKey(idA, idB);
        auto it = s_globalCache.find(cacheKey);
        
        if (it == s_globalCache.end()) {
            return false; // No cached entry exists
        }
        
        // Update expiry time
        uint64_t newExpiryTime = currentTime + expirationDuration;
        it->second.expiryTime = newExpiryTime;
        
        // Add new expiration to queue
        s_expirationQueue.push(std::make_pair(newExpiryTime, cacheKey));
        
        return true;
    }

    /**
     * @brief Clear cached data for a specific pair of objects
     * @param idA First object ID (used as cache key)
     * @param idB Second object ID (used as cache key)
     */
    static void clearCachedData(int idA, int idB) {
        auto cacheKey = makeCacheKey(idA, idB);
        s_globalCache.erase(cacheKey);
        // Note: We don't remove from expiration queue as it would be expensive
        // The cleanup process will skip entries that no longer exist in the cache
    }

private:
    struct CachedInfo {
        DataType data;           // The cached data
        uint64_t expiryTime;     // Time when this entry expires
    };

    static std::pair<int, int> makeCacheKey(int idA, int idB) {
        // Sort to ensure consistent ordering
        return (idA < idB) ? std::make_pair(idA, idB) : std::make_pair(idB, idA);
    }
    
    static void cleanupExpiredEntries(uint64_t currentTime) {
        int cleanedCount = 0;
        
        // Process expired entries from the front of the queue
        while (!s_expirationQueue.empty()) {
            const auto& entry = s_expirationQueue.top();
            uint64_t expiryTime = entry.first;
            
            // If this entry hasn't expired yet, we're done (queue is sorted by expiry time)
            if (expiryTime > currentTime) {
                break;
            }
            
            const auto& cacheKey = entry.second;
            auto cacheIt = s_globalCache.find(cacheKey);
            
            // If entry still exists in cache and has actually expired, remove it
            if (cacheIt != s_globalCache.end() && currentTime >= cacheIt->second.expiryTime) {
                s_globalCache.erase(cacheIt);
                cleanedCount++;
            }
            
            // Remove this queue entry (it's either expired or stale from a refresh)
            s_expirationQueue.pop();
        }
        
        //if (cleanedCount > 0) {
        //    std::cout << "PairCache: Cleaned " << cleanedCount << " expired entries. Cache size: " 
        //              << s_globalCache.size() << std::endl;
        //}
    }
    
    // Static members per template instantiation
    static uint64_t s_cleanupCounter;
    static std::priority_queue<
        std::pair<uint64_t, std::pair<int, int>>,
        std::vector<std::pair<uint64_t, std::pair<int, int>>>,
        std::greater<std::pair<uint64_t, std::pair<int, int>>>
    > s_expirationQueue;
    static std::unordered_map<std::pair<int, int>, CachedInfo, Hash::IntPairHash> s_globalCache;
    static constexpr uint64_t CLEANUP_FREQUENCY = 10; // Check for cleanup every N operations
};

// Static member definitions
template<typename DataType>
uint64_t PairCache<DataType>::s_cleanupCounter = 0;

template<typename DataType>
std::priority_queue<
    std::pair<uint64_t, std::pair<int, int>>,
    std::vector<std::pair<uint64_t, std::pair<int, int>>>,
    std::greater<std::pair<uint64_t, std::pair<int, int>>>
> PairCache<DataType>::s_expirationQueue;

template<typename DataType>
std::unordered_map<std::pair<int, int>, typename PairCache<DataType>::CachedInfo, Hash::IntPairHash> PairCache<DataType>::s_globalCache;