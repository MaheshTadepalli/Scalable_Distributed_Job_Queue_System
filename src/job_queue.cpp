#include "job_queue.h"

#include <ctime>
#include <iomanip>
#include <sstream>

JobQueue::JobQueue(std::size_t workerCount, int maxRetries, std::chrono::milliseconds retryDelay)
    : workerCount_(workerCount == 0 ? 1 : workerCount),
      maxRetries_(maxRetries < 0 ? 0 : maxRetries),
      retryDelay_(retryDelay) {
    metrics_.workers = workerCount_;
}

JobQueue::~JobQueue() {
    stop();
}

void JobQueue::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    retryThread_ = std::thread(&JobQueue::retryLoop, this);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        workers_.emplace_back(&JobQueue::workerLoop, this);
    }
}

void JobQueue::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    cv_.notify_all();
    retryCv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    if (retryThread_.joinable()) {
        retryThread_.join();
    }
}

Job JobQueue::submit(std::string type, std::string payload, int failuresBeforeSuccess) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job job;
    job.id = nextId_++;
    job.type = type.empty() ? "default" : std::move(type);
    job.payload = std::move(payload);
    job.maxRetries = maxRetries_;
    job.failuresBeforeSuccess = failuresBeforeSuccess < 0 ? 0 : failuresBeforeSuccess;
    job.arrivalTime = nowIso8601();

    jobs_[job.id] = job;
    pending_.push_back(job.id);
    metrics_.submitted++;
    metrics_.queued = pending_.size();
    cv_.notify_one();
    return job;
}

QueueMetrics JobQueue::metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    QueueMetrics snapshot = metrics_;
    snapshot.queued = pending_.size();
    return snapshot;
}

std::vector<Job> JobQueue::jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Job> snapshot;
    snapshot.reserve(jobs_.size());
    for (const auto& entry : jobs_) {
        snapshot.push_back(entry.second);
    }
    return snapshot;
}

std::vector<Job> JobQueue::deadLetterJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Job> snapshot;
    snapshot.reserve(deadLetters_.size());
    for (auto id : deadLetters_) {
        auto it = jobs_.find(id);
        if (it != jobs_.end()) {
            snapshot.push_back(it->second);
        }
    }
    return snapshot;
}

void JobQueue::workerLoop() {
    while (running_) {
        std::uint64_t jobId = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return !running_ || !pending_.empty(); });
            if (!running_) {
                return;
            }
            jobId = pending_.front();
            pending_.pop_front();
            metrics_.queued = pending_.size();
            metrics_.running++;
            jobs_[jobId].status = JobStatus::Running;
        }

        processJob(jobId);
    }
}

void JobQueue::retryLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (retryQueue_.empty()) {
            retryCv_.wait(lock, [&] { return !running_ || !retryQueue_.empty(); });
        } else {
            retryCv_.wait_until(lock, retryQueue_.top().readyAt, [&] {
                return !running_ || retryQueue_.empty() ||
                       retryQueue_.top().readyAt <= std::chrono::steady_clock::now();
            });
        }

        if (!running_) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        while (!retryQueue_.empty() && retryQueue_.top().readyAt <= now) {
            const auto jobId = retryQueue_.top().jobId;
            retryQueue_.pop();
            auto it = jobs_.find(jobId);
            if (it != jobs_.end() && it->second.status == JobStatus::Retrying) {
                it->second.status = JobStatus::Queued;
                pending_.push_back(jobId);
                metrics_.queued = pending_.size();
                cv_.notify_one();
            }
        }
    }
}

void JobQueue::processJob(std::uint64_t jobId) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    Job job;
    bool shouldFail = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = jobs_.at(jobId);
        shouldFail = job.attempts < job.failuresBeforeSuccess;
    }

    if (shouldFail) {
        std::lock_guard<std::mutex> lock(mutex_);
        Job& failedJob = jobs_.at(jobId);
        failedJob.attempts++;
        failedJob.lastError = "simulated processor failure";
        metrics_.failedAttempts++;
        metrics_.running--;

        if (failedJob.attempts > failedJob.maxRetries) {
            failedJob.status = JobStatus::DeadLettered;
            deadLetters_.push_back(jobId);
            metrics_.deadLettered++;
            return;
        }

        scheduleRetry(failedJob);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Job& completedJob = jobs_.at(jobId);
    completedJob.attempts++;
    completedJob.status = JobStatus::Completed;
    completedJob.lastError.clear();
    metrics_.completed++;
    metrics_.running--;
}

void JobQueue::scheduleRetry(const Job& job) {
    jobs_.at(job.id).status = JobStatus::Retrying;
    retryQueue_.push(RetryItem{std::chrono::steady_clock::now() + retryDelay_, job.id});
    metrics_.retriesScheduled++;
    retryCv_.notify_one();
}

std::string JobQueue::nowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};

#if defined(_MSC_VER)
    gmtime_s(&tm, &time);
#elif defined(_WIN32)
    tm = *std::gmtime(&time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
