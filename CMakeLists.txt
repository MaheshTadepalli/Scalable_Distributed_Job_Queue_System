cmake_minimum_required(VERSION 3.16)
project(distributed_job_queue_system VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(job_queue_core
    src/job_queue.cpp
)
target_include_directories(job_queue_core PUBLIC src)

add_executable(job-queue
    src/main.cpp
    src/http_server.cpp
)
target_link_libraries(job-queue PRIVATE job_queue_core)

if (WIN32)
    target_link_libraries(job-queue PRIVATE ws2_32)
endif()

enable_testing()
add_executable(job_queue_tests
    tests/job_queue_tests.cpp
)
target_link_libraries(job_queue_tests PRIVATE job_queue_core)
add_test(NAME job_queue_tests COMMAND job_queue_tests)
