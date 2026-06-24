// JobManager.h
#pragma once

#include <memory>
#include <queue>
#include <chrono>
#include <functional>
#include "TimeHandler.h"

class Job {
public:
    std::function<bool(std::chrono::time_point<std::chrono::high_resolution_clock>)> execute;
    int priority;
    uint64_t sequenceId;
    bool cancelled = false;
    
    Job(std::function<bool(std::chrono::time_point<std::chrono::high_resolution_clock>)> callback, 
        int prio, uint64_t seqId)
        : execute(callback), priority(prio), sequenceId(seqId) {}
};

// Custom comparator for the priority queue
struct JobComparator {
    bool operator()(const std::shared_ptr<Job>& a, const std::shared_ptr<Job>& b) const {
        // Higher priority comes first
        if (a->priority != b->priority) {
            return a->priority < b->priority; // Higher priority first
        }
        return a->sequenceId > b->sequenceId; // Earlier sequence first (FIFO within priority)
    }
};

class JobManager {
private:
    std::priority_queue<std::shared_ptr<Job>, 
                       std::vector<std::shared_ptr<Job>>, 
                       JobComparator> m_jobQueue;
    uint64_t m_nextSequenceId = 0;
    TimeHandler* m_timeHandler;
    
public:
    explicit JobManager(TimeHandler* timeHandler);
    ~JobManager() = default;
    
    // Schedule a job with given priority (higher number = higher priority)
    // Returns weak_ptr that becomes invalid when job completes
    std::weak_ptr<Job> schedule(
        std::function<bool(std::chrono::time_point<std::chrono::high_resolution_clock>)> callback, 
        int priority);
    
    // Work until the given end time
    void work(std::chrono::time_point<std::chrono::high_resolution_clock> endTime);
    
    // Cancel a job
    void cancel(std::weak_ptr<Job> jobHandle);
    
    // Check if there are any pending jobs
    bool hasJobs() const;
};