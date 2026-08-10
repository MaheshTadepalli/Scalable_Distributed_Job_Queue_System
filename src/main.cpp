#include "http_server.h"
#include "job_queue.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int envInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}
}

int main() {
    const auto port = static_cast<std::uint16_t>(envInt("JOB_QUEUE_PORT", 8080));
    const auto workers = static_cast<std::size_t>(envInt("JOB_QUEUE_WORKERS", 4));
    const auto maxRetries = envInt("JOB_QUEUE_MAX_RETRIES", 3);
    const auto retryDelayMs = envInt("JOB_QUEUE_RETRY_DELAY_MS", 1500);

    JobQueue queue(workers, maxRetries, std::chrono::milliseconds(retryDelayMs));
    queue.start();

    queue.submit("report", "generate daily report", 0);
    queue.submit("email", "send welcome email", 1);
    queue.submit("billing", "charge customer", 99);

    HttpServer server(queue, port);
    return server.run();
}
