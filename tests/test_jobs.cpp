#include "core/JobSystem.h"
#include <atomic>
#include <chrono>
#include <doctest/doctest.h>
#include <numeric>
#include <thread>
#include <vector>

TEST_SUITE("jobs")
{
    TEST_CASE("scheduled jobs run on workers and Wait completes")
    {
        Geni::JobSystem jobs(4);
        std::atomic<int> counter{0};
        std::atomic<int> workerIndex{0};

        std::vector<Geni::JobSystem::JobHandle> handles;
        for (int i = 0; i < 1000; ++i)
        {
            handles.push_back(jobs.Schedule([&] {
                counter.fetch_add(1, std::memory_order_relaxed);
                workerIndex.store(
                    static_cast<int>(Geni::JobSystem::GetCurrentThreadIndex()), std::memory_order_relaxed
                );
            }));
        }
        for (const auto &handle : handles)
        {
            handle.Wait();
        }
        CHECK(counter.load() == 1000);
        CHECK(workerIndex.load() != 0);
        CHECK(jobs.GetOutstandingJobs() == 0);
    }

    TEST_CASE("ParallelFor visits every index exactly once")
    {
        Geni::JobSystem jobs(4);
        constexpr size_t count = 10000;
        std::vector<int> visits(count, 0);
        jobs.ParallelFor(count, 256, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i)
            {
                visits[i] += 1;
            }
        });
        for (int visit : visits)
        {
            CHECK(visit == 1);
        }
    }

    TEST_CASE("ParallelFor returns only after every task has finished")
    {
        // Slow tasks: a ParallelFor that returns early is caught by the counter. Each
        // task used to complete the shared state twice, so it returned after half.
        Geni::JobSystem jobs(4);
        for (int run = 0; run < 10; ++run)
        {
            std::atomic<int> finished{0};
            jobs.ParallelFor(16, 1, [&](size_t, size_t) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                finished.fetch_add(1, std::memory_order_relaxed);
            });
            REQUIRE(finished.load() == 16);
        }
        CHECK(jobs.GetOutstandingJobs() == 0);
    }

    TEST_CASE("ParallelFor nested inside jobs does not deadlock")
    {
        // Every worker blocks in an inner ParallelFor; the waiting threads must run the
        // queued inner tasks themselves.
        Geni::JobSystem jobs(2);
        std::atomic<int> visits{0};
        jobs.ParallelFor(8, 1, [&](size_t, size_t) {
            jobs.ParallelFor(8, 1, [&](size_t, size_t) { visits.fetch_add(1, std::memory_order_relaxed); });
        });
        CHECK(visits.load() == 64);
    }

    TEST_CASE("PostToMain runs only when the main thread pumps")
    {
        Geni::JobSystem jobs(2);
        std::atomic<bool> ran{false};
        std::atomic<bool> onMain{false};

        jobs.Schedule(
                [&] {
                    jobs.PostToMain([&] {
                        ran.store(true);
                        onMain.store(Geni::JobSystem::GetCurrentThreadIndex() == 0);
                    });
                }
        ).Wait();

        CHECK_FALSE(ran.load());
        CHECK(jobs.PumpMainThread() == 1);
        CHECK(ran.load());
        CHECK(onMain.load());
    }

    TEST_CASE("WaitIdle waits for every scheduled job")
    {
        Geni::JobSystem jobs(3);
        std::atomic<int> finished{0};
        for (int i = 0; i < 200; ++i)
        {
            jobs.Schedule([&] { finished.fetch_add(1, std::memory_order_relaxed); });
        }
        jobs.WaitIdle();
        CHECK(finished.load() == 200);
    }
}
