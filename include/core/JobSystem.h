#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Geni
{

// A small task pool. Workers run scheduled functions; the main thread never runs jobs
// implicitly, it drains callbacks with PumpMainThread once per frame. Asset loading uses
// that split: workers decode, the main thread creates GPU objects.
//
// Threads: Schedule, ParallelFor, PostToMain and Wait may be called from any thread.
// PumpMainThread must be called from the thread that created the system (the main
// thread). A job must not call Wait on itself or on a job that is waiting for it.
class JobSystem
{
  public:
    struct JobState
    {
        std::atomic<int> pending{1};
        std::atomic<bool> done{false};
        std::mutex mutex;
        std::condition_variable condition;

        void Complete()
        {
            // Only the call that takes the count to zero publishes `done`.
            if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    done.store(true, std::memory_order_release);
                }
                condition.notify_all();
            }
        }

        void Wait()
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this] { return done.load(std::memory_order_acquire); });
        }
    };

    class JobHandle
    {
      public:
        JobHandle() = default;
        explicit JobHandle(std::shared_ptr<JobState> state) : m_state(std::move(state))
        {
        }

        bool IsValid() const
        {
            return m_state != nullptr;
        }
        bool IsComplete() const
        {
            return !m_state || m_state->done.load(std::memory_order_acquire);
        }
        void Wait() const
        {
            if (m_state)
            {
                m_state->Wait();
            }
        }

      private:
        friend class JobSystem;
        std::shared_ptr<JobState> m_state;
    };

    // `threadCount` 0 uses hardware_concurrency (at least one worker).
    explicit JobSystem(uint32_t threadCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem &) = delete;
    JobSystem &operator=(const JobSystem &) = delete;

    JobHandle Schedule(std::function<void()> function, const char *debugName = nullptr);
    // Runs fn(begin, end) over [0, count), split into tasks of about `grain` items.
    // Returns when every task has finished. The calling thread runs queued tasks while it
    // waits, so ParallelFor may be called from a job without deadlocking the pool.
    void ParallelFor(size_t count, size_t grain, const std::function<void(size_t, size_t)> &function);
    // Queues `function` for PumpMainThread. Safe from any thread.
    void PostToMain(std::function<void()> function);
    // Runs the callbacks queued with PostToMain. Returns how many ran.
    size_t PumpMainThread();
    // Waits for `handle`, running queued jobs on the calling thread in the meantime.
    // Prefer this to JobHandle::Wait, which only blocks.
    void Wait(const JobHandle &handle);
    // Waits for every scheduled job (not for main-thread callbacks).
    void WaitIdle();

    uint32_t GetThreadCount() const
    {
        return static_cast<uint32_t>(m_threads.size());
    }
    // Number of jobs scheduled and not yet finished, including queued ones.
    uint32_t GetOutstandingJobs() const
    {
        return m_outstanding.load(std::memory_order_acquire);
    }
    // 0 on the main thread; 1..threadCount on workers.
    static uint32_t GetCurrentThreadIndex();
    static bool IsWorkerThread();

  private:
    struct Task
    {
        std::function<void()> function;
        std::shared_ptr<JobState> state;
    };

    void WorkerLoop(uint32_t index);
    void Enqueue(Task task);
    // Pops one queued task and runs it on the calling thread. False if the queue is empty.
    bool TryRunOne();
    void Run(Task &task);

    std::vector<std::thread> m_threads;
    std::deque<Task> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::atomic<bool> m_stopping{false};
    std::atomic<uint32_t> m_outstanding{0};

    std::mutex m_idleMutex;
    std::condition_variable m_idleCondition;

    std::mutex m_mainMutex;
    std::deque<std::function<void()>> m_mainQueue;
};

} // namespace Geni
