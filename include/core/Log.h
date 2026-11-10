#pragma once

#include <cstdarg>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define GENI_PRINTF_FORMAT(fmtIndex, argsIndex) __attribute__((format(printf, fmtIndex, argsIndex)))
#else
#define GENI_PRINTF_FORMAT(fmtIndex, argsIndex)
#endif

namespace Geni
{

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

struct LogMessage
{
    LogLevel level = LogLevel::Info;
    double time = 0.0; // seconds since the first log call
    std::string text;
    const char *file = ""; // basename of the source file
    int line = 0;
};

// Process-wide logger. Thread-safe. Messages go to stderr, to an optional log file,
// to registered sinks, and into a ring buffer that the debug overlay and the crash
// handler read back.
class Log
{
  public:
    using Sink = std::function<void(const LogMessage &)>;

    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();
    static void SetConsoleEnabled(bool enabled);

    // Truncates the file. Returns false if it cannot be opened.
    static bool OpenLogFile(const std::string &path);
    static void CloseLogFile();

    // Sinks are called with the logger's lock held: do not log from inside a sink.
    static int AddSink(Sink sink);
    static void RemoveSink(int id);

    // Most recent messages, oldest first.
    static std::vector<LogMessage> GetRecent(size_t maxCount = 256);
    // Monotonic counter of messages written; lets readers skip GetRecent() when nothing changed.
    static size_t GetMessageCount();
    // Like GetRecent, but gives up instead of waiting if another thread (or the code that
    // was interrupted by a crash) holds the logger's lock. Used by the crash handler.
    static bool TryGetRecent(std::vector<LogMessage> &out, size_t maxCount = 256);

    static void Write(LogLevel level, const char *file, int line, const char *fmt, ...) GENI_PRINTF_FORMAT(4, 5);
    static void WriteV(LogLevel level, const char *file, int line, const char *fmt, va_list args);
    static void Flush();

    static const char *LevelName(LogLevel level);
    static std::string FormatLine(const LogMessage &message);
};

} // namespace Geni

#define GENI_LOG_TRACE(...) ::Geni::Log::Write(::Geni::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define GENI_LOG_DEBUG(...) ::Geni::Log::Write(::Geni::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define GENI_LOG_INFO(...) ::Geni::Log::Write(::Geni::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define GENI_LOG_WARN(...) ::Geni::Log::Write(::Geni::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define GENI_LOG_ERROR(...) ::Geni::Log::Write(::Geni::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define GENI_LOG_FATAL(...) ::Geni::Log::Write(::Geni::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)
