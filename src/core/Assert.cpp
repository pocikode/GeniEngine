#include "core/Assert.h"
#include "core/CrashHandler.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace Geni
{

namespace
{

std::atomic<AssertHandler> g_assertHandler{nullptr};

[[noreturn]] void Die()
{
    // With the crash handler installed, abort() produces the report and its stack trace.
    if (!IsCrashHandlerInstalled())
    {
        std::fputs("Stack trace:\n", stderr);
        std::fputs(CaptureStackTrace(2).c_str(), stderr);
    }
    Log::Flush();
    std::abort();
}

void DefaultAssertHandler(const char *expression, const char *file, int line, const char *message)
{
    if (message && message[0] != '\0')
    {
        Log::Write(LogLevel::Fatal, file, line, "Assertion failed: %s: %s", expression, message);
    }
    else
    {
        Log::Write(LogLevel::Fatal, file, line, "Assertion failed: %s", expression);
    }
    Die();
}

void Dispatch(const char *expression, const char *file, int line, const char *message)
{
    AssertHandler handler = g_assertHandler.load();
    if (!handler)
    {
        handler = DefaultAssertHandler;
    }
    handler(expression, file, line, message);
}

} // namespace

AssertHandler SetAssertHandler(AssertHandler handler)
{
    return g_assertHandler.exchange(handler);
}

void ReportAssertFailure(const char *expression, const char *file, int line)
{
    Dispatch(expression, file, line, "");
}

void ReportAssertFailure(const char *expression, const char *file, int line, const char *fmt, ...)
{
    char message[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    Dispatch(expression, file, line, message);
}

void FatalError(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Log::WriteV(LogLevel::Fatal, file, line, fmt, args);
    va_end(args);

    Die();
}

} // namespace Geni
