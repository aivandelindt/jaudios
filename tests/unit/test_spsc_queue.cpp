#include "app/core/SpscQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using jaudios::SpscQueue;

TEST_CASE ("SpscQueue preserves FIFO order and reports full/empty", "[scaffold][fifo]")
{
    SpscQueue<int, 4> q; // capacity 3 (AbstractFifo keeps one slot free)
    STATIC_REQUIRE (SpscQueue<int, 4>::capacity() == 3);

    int out = 0;
    REQUIRE_FALSE (q.pop (out));

    REQUIRE (q.push (1));
    REQUIRE (q.push (2));
    REQUIRE (q.push (3));
    REQUIRE_FALSE (q.push (4));

    REQUIRE (q.pop (out)); CHECK (out == 1);
    REQUIRE (q.push (5)); // wraps around
    REQUIRE (q.pop (out)); CHECK (out == 2);
    REQUIRE (q.pop (out)); CHECK (out == 3);
    REQUIRE (q.pop (out)); CHECK (out == 5);
    REQUIRE_FALSE (q.pop (out));
}

TEST_CASE ("SpscQueue is safe with one producer and one consumer thread", "[scaffold][fifo]")
{
    SpscQueue<int, 64> q;
    constexpr int count = 200000;

    std::thread producer ([&]
    {
        for (int i = 0; i < count; ++i)
            while (! q.push (i))
                std::this_thread::yield();
    });

    int expected = 0;
    bool ordered = true;
    while (expected < count)
    {
        int v = -1;
        if (q.pop (v))
        {
            ordered = ordered && (v == expected);
            ++expected;
        }
    }

    producer.join();
    CHECK (ordered);
}
