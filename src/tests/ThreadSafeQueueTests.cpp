// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/ThreadSafeQueue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

using Lightweight::ThreadSafeQueue;

TEST_CASE("ThreadSafeQueue: default-constructed queue is empty", "[ThreadSafeQueue]")
{
    ThreadSafeQueue<int> queue;
    CHECK(queue.Empty());
    CHECK(queue.Size() == 0);
}

TEST_CASE("ThreadSafeQueue: Push grows the queue and Pop returns items in FIFO order", "[ThreadSafeQueue]")
{
    ThreadSafeQueue<int> queue;
    queue.Push(1);
    queue.Push(2);
    queue.Push(3);

    REQUIRE(queue.Size() == 3);
    CHECK_FALSE(queue.Empty());

    queue.MarkFinished(); // so WaitAndPop returns false after the queue drains
    int value = 0;

    REQUIRE(queue.WaitAndPop(value));
    CHECK(value == 1);
    REQUIRE(queue.WaitAndPop(value));
    CHECK(value == 2);
    REQUIRE(queue.WaitAndPop(value));
    CHECK(value == 3);

    CHECK_FALSE(queue.WaitAndPop(value));
    CHECK(queue.Empty());
}

TEST_CASE("ThreadSafeQueue: MarkFinished on an empty queue causes WaitAndPop to return false", "[ThreadSafeQueue]")
{
    ThreadSafeQueue<int> queue;
    queue.MarkFinished();

    int value = 42;
    CHECK_FALSE(queue.WaitAndPop(value));
    CHECK(value == 42); // out-parameter left untouched on the "finished and empty" branch
}

TEST_CASE("ThreadSafeQueue: items pushed before MarkFinished are still drained", "[ThreadSafeQueue]")
{
    ThreadSafeQueue<std::string> queue;
    queue.Push("first");
    queue.Push("second");
    queue.MarkFinished();

    std::string out;
    REQUIRE(queue.WaitAndPop(out));
    CHECK(out == "first");
    REQUIRE(queue.WaitAndPop(out));
    CHECK(out == "second");
    CHECK_FALSE(queue.WaitAndPop(out));
}

TEST_CASE("ThreadSafeQueue: consumer blocks until producer pushes", "[ThreadSafeQueue]")
{
    ThreadSafeQueue<int> queue;
    std::atomic<bool> consumerStarted { false };
    std::atomic<bool> consumerFinished { false };
    int consumed = 0;

    std::thread consumer([&] {
        consumerStarted = true;
        queue.WaitAndPop(consumed);
        consumerFinished = true;
    });

    // Wait until the consumer has entered WaitAndPop. Using a short sleep is acceptable
    // here because the only goal is to demonstrate the wait actually blocks; a missed
    // wakeup would manifest as `consumerFinished` flipping prematurely, which we check below.
    while (!consumerStarted.load())
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(consumerFinished.load());

    queue.Push(7);
    consumer.join();

    CHECK(consumerFinished.load());
    CHECK(consumed == 7);
}

TEST_CASE("ThreadSafeQueue: MarkFinished unblocks waiting consumers", "[ThreadSafeQueue]")
{
    ThreadSafeQueue<int> queue;
    std::atomic<bool> popReturned { false };
    bool popResult = true;

    std::thread consumer([&] {
        int value = 0;
        popResult = queue.WaitAndPop(value);
        popReturned = true;
    });

    // Give the consumer a chance to block on the condvar before we signal completion.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(popReturned.load());

    queue.MarkFinished();
    consumer.join();

    CHECK(popReturned.load());
    CHECK_FALSE(popResult);
}

TEST_CASE("ThreadSafeQueue: multi-producer / multi-consumer drain preserves every item", "[ThreadSafeQueue]")
{
    constexpr int ProducerCount = 4;
    constexpr int ItemsPerProducer = 250;
    constexpr int ConsumerCount = 3;

    ThreadSafeQueue<int> queue;
    std::vector<std::thread> producers;
    producers.reserve(ProducerCount);

    for (int p = 0; p < ProducerCount; ++p)
    {
        producers.emplace_back([&queue, p] {
            int const base = p * ItemsPerProducer;
            for (int i = 0; i < ItemsPerProducer; ++i)
                queue.Push(base + i);
        });
    }

    constexpr std::size_t TotalItems = static_cast<std::size_t>(ProducerCount) * static_cast<std::size_t>(ItemsPerProducer);

    std::vector<std::vector<int>> consumed(ConsumerCount);
    std::vector<std::thread> consumers;
    consumers.reserve(ConsumerCount);

    for (std::size_t c = 0; c < static_cast<std::size_t>(ConsumerCount); ++c)
    {
        consumers.emplace_back([&, c] {
            int value = 0;
            while (queue.WaitAndPop(value))
                consumed[c].push_back(value);
        });
    }

    for (auto& t: producers)
        t.join();
    queue.MarkFinished();
    for (auto& t: consumers)
        t.join();

    std::set<int> seen;
    for (auto const& bucket: consumed)
        for (auto const v: bucket)
            seen.insert(v);

    CHECK(seen.size() == TotalItems);
    CHECK(*seen.begin() == 0);
    CHECK(*seen.rbegin() == (ProducerCount * ItemsPerProducer) - 1);
    CHECK(queue.Empty());
}

TEST_CASE("ThreadSafeQueue: move-only payloads are forwarded without copying", "[ThreadSafeQueue]")
{
    ThreadSafeQueue<std::unique_ptr<int>> queue;
    queue.Push(std::make_unique<int>(17));
    queue.MarkFinished();

    std::unique_ptr<int> out;
    REQUIRE(queue.WaitAndPop(out));
    REQUIRE(out);
    CHECK(*out == 17);
}
