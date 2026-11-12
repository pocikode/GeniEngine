#include "core/JobSystem.h"
#include "core/Log.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace Geni
{

namespace
{

thread_local uint32_t t_threadIndex = 0;

uint32_t DefaultThreadCount()
{
    const unsigned int hardware = std::thread::hardware_concurrency();
    // Leave one core for the main thread; small machines still get one worker.
    return hardware > 2 ? hardware - 1 : 1;
}

} // namespace

JobSystem::JobSystem(uint32_t threadCount)
{
    if (threadCount == 0)
    {
        threadCount = DefaultThreadCount();
    }
    m_threads.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        m_threads.emplace_back([this, i] { WorkerLoop(i + 1); });
    }
}

JobSystem::~JobSystem()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopping.store(true, std::memory_order_release);
    }
    m_queueCondition.notify_all();
    for (auto &thread : m_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    // Callbacks still queued belong to the destroyed system; drop them.
    std::lock_guard<std::mutex> lock(m_mainMutex);
    m_mainQueue.clear();
}

JobSystem::JobHandle JobSystem::Schedule(std::function<void()> function, const char *debugName)
{
    auto state = std::make_shared<JobState>();
    JobHandle handle(state);

    if (!function)
    {
        state->Complete();
        return handle;
    }

    m_outstanding.fetch_add(1, std::memory_order_acq_rel);
    Enqueue({std::move(function), std::move(state)});
    (void)debugName;
    return handle;
}

void JobSystem::ParallelFor(size_t count, size_t grain, const std::function<void(size_t, size_t)> &function)
{
    if (count == 0)
    {
        return;
    }
    if (grain == 0)
    {
        grain = 1;
    }

    // One shared state for every task: Run() completes it once per task, so it is done
    // when the last task finishes. The task bodies must not complete it themselves.
    auto state = std::make_shared<JobState>();
    const size_t tasks = (count + grain - 1) / grain;
    state->pending.store(static_cast<int>(tasks), std::memory_order_release);

    for (size_t task = 0; task < tasks; ++task)
    {
        const size_t begin = task * grain;
        const size_t end = std::min(count, begin + grain);
        m_outstanding.fetch_add(1, std::memory_order_acq_rel);
        Enqueue({[&function, begin, end] { function(begin, end); }, state});
    }

    // `function` is captured by reference: this call does not return before every task
    // has run, so the reference stays valid.
    Wait(JobHandle(state));
}

void JobSystem::Wait(const JobHandle &handle)
{
    const auto &state = handle.m_state;
    if (!state)
    {
        return;
    }
    while (!state->done.load(std::memory_order_acquire))
    {
        if (!TryRunOne())
        {
            // Nothing queued: the remaining tasks are running on other threads.
            state->Wait();
            return;
        }
    }
}

void JobSystem::PostToMain(std::function<void()> function)
{
    if (!function)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mainMutex);
        m_mainQueue.push_back(std::move(function));
    }
}

size_t JobSystem::PumpMainThread()
{
    std::deque<std::function<void()>> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mainMutex);
        callbacks.swap(m_mainQueue);
    }

    for (auto &callback : callbacks)
    {
        try
        {
            callback();
        }
        catch (const std::exception &e)
        {
            GENI_LOG_ERROR("main-thread callback failed: %s", e.what());
        }
        catch (...)
        {
            GENI_LOG_ERROR("main-thread callback failed with an unknown exception");
        }
    }
    return callbacks.size();
}

void JobSystem::WaitIdle()
{
    std::unique_lock<std::mutex> lock(m_idleMutex);
    m_idleCondition.wait(lock, [this] { return m_outstanding.load(std::memory_order_acquire) == 0; });
}

uint32_t JobSystem::GetCurrentThreadIndex()
{
    return t_threadIndex;
}

bool JobSystem::IsWorkerThread()
{
    return t_threadIndex != 0;
}

void JobSystem::Enqueue(Task task)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(std::move(task));
    }
    m_queueCondition.notify_one();
}

bool JobSystem::TryRunOne()
{
    Task task;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.empty())
        {
            return false;
        }
        task = std::move(m_queue.front());
        m_queue.pop_front();
    }
    Run(task);
    return true;
}

void JobSystem::Run(Task &task)
{
    try
    {
        task.function();
    }
    catch (const std::exception &e)
    {
        GENI_LOG_ERROR("job failed: %s", e.what());
    }
    catch (...)
    {
        GENI_LOG_ERROR("job failed with an unknown exception");
    }

    task.state->Complete();
    task.state.reset();
    task.function = nullptr;

    if (m_outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        std::lock_guard<std::mutex> lock(m_idleMutex);
        m_idleCondition.notify_all();
    }
}

void JobSystem::WorkerLoop(uint32_t index)
{
    t_threadIndex = index;

    for (;;)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCondition.wait(lock, [this] {
                return m_stopping.load(std::memory_order_acquire) || !m_queue.empty();
            });
            if (m_stopping.load(std::memory_order_acquire) && m_queue.empty())
            {
                return;
            }
            task = std::move(m_queue.front());
            m_queue.pop_front();
        }

        Run(task);
    }
}

} // namespace Geni
