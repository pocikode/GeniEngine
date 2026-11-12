#include "core/CrashHandler.h"
#include "core/Log.h"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h must come after windows.h
#include <dbghelp.h>
#else
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#endif

// The handlers below call functions that are not async-signal-safe (malloc, stdio).
// That is a deliberate trade: the process is already dying, and a readable report in the
// common case is worth more than strict safety in the rare case where the crash happened
// inside the allocator. The re-entry guard keeps a second fault from looping.

namespace Geni
{

namespace
{

struct CrashState
{
    CrashHandlerConfig config;
    std::atomic<bool> installed{false};
    std::atomic_flag reporting = ATOMIC_FLAG_INIT;
};

// Leaked on purpose so it outlives static destruction.
CrashState &State()
{
    static CrashState *state = new CrashState();
    return *state;
}

const char *Basename(const char *path)
{
    if (!path)
    {
        return "?";
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

void WriteCrashReport(const char *reason)
{
    auto &state = State();
    if (state.reporting.test_and_set())
    {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm localTime = {};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char readableTime[32];
    char fileTime[32];
    std::strftime(readableTime, sizeof(readableTime), "%Y-%m-%d %H:%M:%S", &localTime);
    std::strftime(fileTime, sizeof(fileTime), "%Y%m%d_%H%M%S", &localTime);

    std::string report;
    report += "\n==== GeniEngine crash report ====\n";
    report += "Application: " + state.config.applicationName + "\n";
    report += "Time:        " + std::string(readableTime) + "\n";
    report += "Reason:      " + std::string(reason ? reason : "unknown") + "\n";
    report += "\nStack trace:\n";
    report += CaptureStackTrace(1);

    std::vector<LogMessage> recent;
    if (Log::TryGetRecent(recent, 50))
    {
        report += "\nRecent log:\n";
        for (const auto &message : recent)
        {
            report += Log::FormatLine(message);
            report += '\n';
        }
    }
    report += "==== end of crash report ====\n";

    std::fputs(report.c_str(), stderr);
    std::fflush(stderr);

    std::string path = state.config.reportDirectory;
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
    {
        path += '/';
    }
    path += "geni_crash_";
    path += fileTime;
    path += ".log";

    if (FILE *file = std::fopen(path.c_str(), "w"))
    {
        std::fputs(report.c_str(), file);
        std::fclose(file);
        std::fprintf(stderr, "Crash report written to %s\n", path.c_str());
        std::fflush(stderr);
    }
}

void TerminateHandler()
{
    std::string reason = "std::terminate called";
    if (auto exception = std::current_exception())
    {
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception &e)
        {
            reason = std::string("Unhandled C++ exception: ") + e.what();
        }
        catch (...)
        {
            reason = "Unhandled C++ exception of unknown type";
        }
    }

    WriteCrashReport(reason.c_str());
    std::abort();
}

#if defined(_WIN32)

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS *exceptionInfo)
{
    char reason[128];
    const auto *record = exceptionInfo ? exceptionInfo->ExceptionRecord : nullptr;
    if (record)
    {
        const char *name = "Unhandled exception";
        switch (record->ExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:
            name = "Access violation";
            break;
        case EXCEPTION_STACK_OVERFLOW:
            name = "Stack overflow";
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            name = "Illegal instruction";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            name = "Integer divide by zero";
            break;
        default:
            break;
        }
        std::snprintf(
            reason, sizeof(reason), "%s (code 0x%08lX) at address %p", name,
            static_cast<unsigned long>(record->ExceptionCode), record->ExceptionAddress
        );
    }
    else
    {
        std::snprintf(reason, sizeof(reason), "Unhandled exception");
    }

    WriteCrashReport(reason);
    return EXCEPTION_EXECUTE_HANDLER;
}

void AbortSignalHandler(int)
{
    WriteCrashReport("abort() called (SIGABRT)");
    // Leave without returning into the CRT, which would show a dialog in debug builds.
    _exit(3);
}

#else

const char *SignalName(int signalNumber)
{
    switch (signalNumber)
    {
    case SIGSEGV:
        return "Segmentation fault (SIGSEGV)";
    case SIGABRT:
        return "abort() called (SIGABRT)";
    case SIGILL:
        return "Illegal instruction (SIGILL)";
    case SIGFPE:
        return "Arithmetic error (SIGFPE)";
    case SIGBUS:
        return "Bus error (SIGBUS)";
    default:
        return "Fatal signal";
    }
}

void FatalSignalHandler(int signalNumber, siginfo_t *info, void *)
{
    char reason[128];
    if (info && (signalNumber == SIGSEGV || signalNumber == SIGBUS))
    {
        std::snprintf(reason, sizeof(reason), "%s at address %p", SignalName(signalNumber), info->si_addr);
    }
    else
    {
        std::snprintf(reason, sizeof(reason), "%s", SignalName(signalNumber));
    }

    WriteCrashReport(reason);

    // Die with the original signal so the exit status and core dump are what the OS,
    // a debugger or CI expects.
    std::signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}

#endif

} // namespace

#if defined(_WIN32)

std::string CaptureStackTrace(int skipFrames, int maxFrames)
{
    static std::atomic<bool> symbolsReady{false};
    HANDLE process = GetCurrentProcess();
    if (!symbolsReady.exchange(true))
    {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);
    }

    void *frames[128];
    if (maxFrames > 128)
    {
        maxFrames = 128;
    }
    const USHORT count =
        CaptureStackBackTrace(static_cast<DWORD>(skipFrames + 1), static_cast<DWORD>(maxFrames), frames, nullptr);

    std::string result;
    char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
    for (USHORT i = 0; i < count; ++i)
    {
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuffer);
        std::memset(symbolBuffer, 0, sizeof(symbolBuffer));
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;

        char line[900];
        DWORD64 displacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol))
        {
            IMAGEHLP_LINE64 lineInfo = {};
            lineInfo.SizeOfStruct = sizeof(lineInfo);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo))
            {
                std::snprintf(
                    line, sizeof(line), "  #%-2d %s (%s:%lu)\n", static_cast<int>(i), symbol->Name,
                    Basename(lineInfo.FileName), static_cast<unsigned long>(lineInfo.LineNumber)
                );
            }
            else
            {
                std::snprintf(
                    line, sizeof(line), "  #%-2d %s + 0x%llx\n", static_cast<int>(i), symbol->Name,
                    static_cast<unsigned long long>(displacement)
                );
            }
        }
        else
        {
            std::snprintf(
                line, sizeof(line), "  #%-2d 0x%016llx\n", static_cast<int>(i), static_cast<unsigned long long>(address)
            );
        }
        result += line;
    }

    if (result.empty())
    {
        result = "  (no frames captured)\n";
    }
    return result;
}

#else

std::string CaptureStackTrace(int skipFrames, int maxFrames)
{
    void *frames[128];
    int wanted = maxFrames + skipFrames + 1;
    if (wanted > 128)
    {
        wanted = 128;
    }
    const int count = backtrace(frames, wanted);

    std::string result;
    int index = 0;
    for (int i = skipFrames + 1; i < count; ++i, ++index)
    {
        char line[900];
        Dl_info info;
        if (dladdr(frames[i], &info) && info.dli_sname)
        {
            int status = 0;
            char *demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            const char *name = (status == 0 && demangled) ? demangled : info.dli_sname;
            const auto offset =
                static_cast<long>(static_cast<const char *>(frames[i]) - static_cast<const char *>(info.dli_saddr));
            std::snprintf(line, sizeof(line), "  #%-2d %s + %ld [%s]\n", index, name, offset, Basename(info.dli_fname));
            std::free(demangled);
        }
        else if (info.dli_fname)
        {
            std::snprintf(line, sizeof(line), "  #%-2d %p [%s]\n", index, frames[i], Basename(info.dli_fname));
        }
        else
        {
            std::snprintf(line, sizeof(line), "  #%-2d %p\n", index, frames[i]);
        }
        result += line;
    }

    if (result.empty())
    {
        result = "  (no frames captured)\n";
    }
    return result;
}

#endif

void InstallCrashHandler(const CrashHandlerConfig &config)
{
    auto &state = State();
    state.config = config;
    if (state.installed.exchange(true))
    {
        return;
    }

    std::set_terminate(TerminateHandler);

#if defined(_WIN32)
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
    std::signal(SIGABRT, AbortSignalHandler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    // A separate stack lets the handler run after a stack overflow.
    static char alternateStack[64 * 1024];
    stack_t stack = {};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof(alternateStack);
    sigaltstack(&stack, nullptr);

    struct sigaction action = {};
    action.sa_sigaction = FatalSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);

    for (int signalNumber : {SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS})
    {
        sigaction(signalNumber, &action, nullptr);
    }
#endif
}

bool IsCrashHandlerInstalled()
{
    return State().installed.load();
}

} // namespace Geni
