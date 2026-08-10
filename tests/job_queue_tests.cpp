#include "job_queue.h"

#include <cassert>
#include <chrono>
#include <thread>

namespace {
void waitFor(const std::function<bool()>& predicate) {
    for (int i = 0; i < 80; ++i) {
        if (predicate()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(predicate());
}
}

int main() {
    JobQueue queue(2, 2, std::chrono::milliseconds(20));
    queue.start();

    queue.submit("success", "runs once", 0);
    queue.submit("retry", "fails once then succeeds", 1);
    queue.submit("dead", "always fails", 99);

    waitFor([&] {
        const auto metrics = queue.metrics();
        return metrics.completed == 2 && metrics.deadLettered == 1;
    });

    const auto metrics = queue.metrics();
    assert(metrics.submitted == 3);
    assert(metrics.completed == 2);
    assert(metrics.deadLettered == 1);
    assert(metrics.failedAttempts == 4);
    assert(metrics.retriesScheduled == 3);

    const auto dead = queue.deadLetterJobs();
    assert(dead.size() == 1);
    assert(dead.front().status == JobStatus::DeadLettered);

    queue.stop();
    return 0;
}
