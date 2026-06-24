// JobManager.cpp
#include "JobManager.h"

JobManager::JobManager(TimeHandler* timeHandler)
    : m_timeHandler(timeHandler)
{
    if (!m_timeHandler) {
        throw std::invalid_argument("TimeHandler cannot be null");
    }
}

std::weak_ptr<Job> JobManager::schedule(
    std::function<bool(std::chrono::time_point<std::chrono::high_resolution_clock>)> callback, 
    int priority) {
    
    if (!callback) {
        return std::weak_ptr<Job>{}; // Return empty weak_ptr for null callback
    }
    
    auto job = std::make_shared<Job>(callback, priority, m_nextSequenceId++);
    m_jobQueue.push(job);
    
    return std::weak_ptr<Job>(job);
}

void JobManager::work(std::chrono::time_point<std::chrono::high_resolution_clock> endTime) {
    while (!m_jobQueue.empty()) {
        auto currentTime = m_timeHandler->now();
        if (currentTime >= endTime) {
            break; // Time budget exhausted
        }
        
        // Peek at the highest priority job
        auto job = m_jobQueue.top();
        
        // Skip if job was cancelled
        //static int xxxxxx = 0;
        if (job->cancelled) {
            //++xxxxxx; std::cout << "Hit: " << xxxxxx << std::endl;
            m_jobQueue.pop();
            continue;
        }
        
        // Execute the job
        bool hasMoreWork = job->execute(endTime);
        
        // Only pop if job is complete or cancelled
        if (!hasMoreWork || job->cancelled) {
            m_jobQueue.pop();
        }
        // If job has more work, it stays at the top and will be re-executed
        // after other jobs of the same priority (due to time passing)
    }
}

void JobManager::cancel(std::weak_ptr<Job> jobHandle) {
    auto job = jobHandle.lock();
    if (job) {
        job->cancelled = true;
    }
}

bool JobManager::hasJobs() const {
    return !m_jobQueue.empty();
}