#include "app/core/AnalysisService.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

TEST_CASE ("AnalysisService runs work off the calling thread and delivers results on it", "[scaffold][threads]")
{
    jaudios::AnalysisService service;

    const auto callerThread = std::this_thread::get_id();
    std::thread::id workerThread;
    std::thread::id callbackThread;
    int result = 0;

    service.submit<int> ([&] { workerThread = std::this_thread::get_id(); return 42; },
                         [&] (int r) { callbackThread = std::this_thread::get_id(); result = r; });

    service.waitUntilIdle();

    CHECK (result == 42);
    CHECK (workerThread != callerThread);
    CHECK (callbackThread == callerThread);
}

TEST_CASE ("AnalysisService runs jobs sequentially in submission order", "[scaffold][threads]")
{
    jaudios::AnalysisService service;
    std::vector<int> order;
    std::atomic<int> concurrent { 0 }, maxConcurrent { 0 };

    for (int i = 0; i < 10; ++i)
        service.submit<int> ([&, i]
                             {
                                 const int c = ++concurrent;
                                 maxConcurrent = std::max (maxConcurrent.load(), c);
                                 juce::Thread::sleep (1);
                                 --concurrent;
                                 return i;
                             },
                             [&] (int r) { order.push_back (r); });

    service.waitUntilIdle();

    REQUIRE (order.size() == 10);
    for (int i = 0; i < 10; ++i)
        CHECK (order[(size_t) i] == i);
    CHECK (maxConcurrent.load() == 1);
}
