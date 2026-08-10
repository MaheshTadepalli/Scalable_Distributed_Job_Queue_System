#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

enum class JobStatus {
    Queued,
    Running,
    Completed,
    Retrying,
    DeadLettered
};

struct Job {
    std::uint64_t id = 0;
    std::string type;
    std::string payload;
    int attempts = 0;
    int maxRetries = 3;
    int failuresBeforeSuccess = 0;
    JobStatus status = JobStatus::Queued;
    std::string arrivalTime;
    std::string lastError;
};

struct QueueMetrics {
    std::uint64_t submitted = 0;
    std::uint64_t completed = 0;
    std::uint64_t failedAttempts = 0;
    std::uint64_t retriesScheduled = 0;
    std::uint64_t deadLettered = 0;
    std::size_t queued = 0;
    std::size_t running = 0;
    std::size_t workers = 0;
};

class JobQueue {
public:
    JobQueue(std::size_t workerCount, int maxRetries, std::chrono::milliseconds retryDelay);
    ~JobQueue();

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    void start();
    void stop();

    Job submit(std::string type, std::string payload, int failuresBeforeSuccess);
    QueueMetrics metrics() const;
    std::vector<Job> jobs() const;
    std::vector<Job> deadLetterJobs() const;

private:
    struct RetryItem {
        std::chrono::steady_clock::time_point readyAt;
        std::uint64_t jobId;

        bool operator>(const RetryItem& other) const {
            return readyAt > other.readyAt;
        }
    };

    void workerLoop();
    void retryLoop();
    void processJob(std::uint64_t jobId);
    void scheduleRetry(const Job& job);
    static std::string nowIso8601();

    const std::size_t workerCount_;
    const int maxRetries_;
    const std::chrono::milliseconds retryDelay_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable retryCv_;
    std::deque<std::uint64_t> pending_;
    std::priority_queue<RetryItem, std::vector<RetryItem>, std::greater<RetryItem>> retryQueue_;
    std::map<std::uint64_t, Job> jobs_;
    std::vector<std::uint64_t> deadLetters_;
    std::vector<std::thread> workers_;
    std::thread retryThread_;
    std::atomic<bool> running_{false};
    std::uint64_t nextId_ = 1;
    QueueMetrics metrics_;
};
