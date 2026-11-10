#include "core/Log.h"
#include "core/Profiler.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

namespace Geni
{

namespace
{

constexpr size_t kRingCapacity = 512;

struct LogState
{
    std::mutex mutex;
    LogLevel level = LogLevel::Trace;
    bool console = true;
    FILE *file = nullptr;
    std::vector<std::pair<int, Log::Sink>> sinks;
    int nextSinkId = 1;
    std::vector<LogMessage> ring;
    size_t ringHead = 0; // index of the oldest message once the ring is full
    size_t messageCount = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

// Leaked on purpose: logging must keep working during static destruction.
LogState &State()
{
    static LogState *state = new LogState();
    return *state;
}

const char *Basename(const char *path)
{
    if (!path)
    {
        return "";
    }

    const char *result = path;
    for (const char *p = path; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            result = p + 1;
        }
    }
    return result;
}

} // namespace

void Log::SetLevel(LogLevel level)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.level = level;
}

LogLevel Log::GetLevel()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.level;
}

void Log::SetConsoleEnabled(bool enabled)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.console = enabled;
}

bool Log::OpenLogFile(const std::string &path)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.file)
    {
        std::fclose(state.file);
        state.file = nullptr;
    }

    state.file = std::fopen(path.c_str(), "w");
    return state.file != nullptr;
}

void Log::CloseLogFile()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.file)
    {
        std::fclose(state.file);
        state.file = nullptr;
    }
}

int Log::AddSink(Sink sink)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    int id = state.nextSinkId++;
    state.sinks.emplace_back(id, std::move(sink));
    return id;
}

void Log::RemoveSink(int id)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (auto it = state.sinks.begin(); it != state.sinks.end(); ++it)
    {
        if (it->first == id)
        {
            state.sinks.erase(it);
            return;
        }
    }
}

static void CopyRecent(const LogState &state, std::vector<LogMessage> &out, size_t maxCount)
{
    const size_t available = state.ring.size();
    const size_t count = maxCount < available ? maxCount : available;

    out.clear();
    out.reserve(count);
    for (size_t i = available - count; i < available; ++i)
    {
        out.push_back(state.ring[(state.ringHead + i) % available]);
    }
}

std::vector<LogMessage> Log::GetRecent(size_t maxCount)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);

    std::vector<LogMessage> result;
    CopyRecent(state, result, maxCount);
    return result;
}

bool Log::TryGetRecent(std::vector<LogMessage> &out, size_t maxCount)
{
    auto &state = State();
    std::unique_lock<std::mutex> lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        return false;
    }

    CopyRecent(state, out, maxCount);
    return true;
}

size_t Log::GetMessageCount()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.messageCount;
}

void Log::Write(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteV(level, file, line, fmt, args);
    va_end(args);
}

void Log::WriteV(LogLevel level, const char *file, int line, const char *fmt, va_list args)
{
    auto &state = State();
    if (level == LogLevel::Off)
    {
        return;
    }

    char stackBuffer[1024];
    va_list argsCopy;
    va_copy(argsCopy, args);
    int needed = std::vsnprintf(stackBuffer, sizeof(stackBuffer), fmt, argsCopy);
    va_end(argsCopy);

    LogMessage message;
    message.level = level;
    message.file = Basename(file);
    message.line = line;

    if (needed < 0)
    {
        message.text = fmt ? fmt : "";
    }
    else if (static_cast<size_t>(needed) < sizeof(stackBuffer))
    {
        message.text.assign(stackBuffer, static_cast<size_t>(needed));
    }
    else
    {
        message.text.resize(static_cast<size_t>(needed));
        std::vsnprintf(&message.text[0], static_cast<size_t>(needed) + 1, fmt, args);
    }

    std::lock_guard<std::mutex> lock(state.mutex);
    if (level < state.level)
    {
        return;
    }

    message.time = std::chrono::duration<double>(std::chrono::steady_clock::now() - state.start).count();
    const std::string formatted = FormatLine(message);

    if (state.console)
    {
        std::fputs(formatted.c_str(), stderr);
        std::fputc('\n', stderr);
    }

    if (state.file)
    {
        std::fputs(formatted.c_str(), state.file);
        std::fputc('\n', state.file);
        // Warnings and errors are the lines that matter after a crash.
        if (level >= LogLevel::Warn)
        {
            std::fflush(state.file);
        }
    }

    for (auto &sink : state.sinks)
    {
        sink.second(message);
    }

    GENI_PROFILE_MESSAGE(message.text.c_str(), message.text.size());

    if (state.ring.size() < kRingCapacity)
    {
        state.ring.push_back(std::move(message));
    }
    else
    {
        state.ring[state.ringHead] = std::move(message);
        state.ringHead = (state.ringHead + 1) % kRingCapacity;
    }
    ++state.messageCount;
}

void Log::Flush()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    std::fflush(stderr);
    if (state.file)
    {
        std::fflush(state.file);
    }
}

const char *Log::LevelName(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    case LogLevel::Off:
        return "OFF";
    }
    return "?";
}

std::string Log::FormatLine(const LogMessage &message)
{
    char prefix[48];
    std::snprintf(prefix, sizeof(prefix), "[%9.3f] [%-5s] ", message.time, LevelName(message.level));

    std::string line = prefix;
    line += message.text;

    // Source location only where it helps find the problem.
    if (message.level >= LogLevel::Warn && message.file && message.file[0] != '\0')
    {
        char suffix[160];
        std::snprintf(suffix, sizeof(suffix), " (%s:%d)", message.file, message.line);
        line += suffix;
    }
    return line;
}

} // namespace Geni
