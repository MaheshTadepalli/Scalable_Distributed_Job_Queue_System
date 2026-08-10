#pragma once

#include "job_queue.h"

#include <atomic>
#include <cstdint>
#include <string>

class HttpServer {
public:
    HttpServer(JobQueue& queue, std::uint16_t port);
    int run();

private:
    std::string handleRequest(const std::string& request);
    std::string dashboard() const;
    std::string metricsJson() const;
    std::string jobsJson(bool deadLetterOnly) const;
    std::string createJob(const std::string& request);

    static std::string response(std::string status, std::string contentType, std::string body);
    static std::string headerValue(const std::string& request, const std::string& name);
    static std::string bodyOf(const std::string& request);
    static std::string jsonValue(const std::string& json, const std::string& key, const std::string& fallback);
    static int jsonIntValue(const std::string& json, const std::string& key, int fallback);
    static std::string escapeJson(const std::string& value);
    static std::string statusName(JobStatus status);

    JobQueue& queue_;
    std::uint16_t port_;
};
