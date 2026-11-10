// Dies in a chosen way so CTest can check that the crash handler writes a report.
// Usage: geni_crash_probe <segfault|abort|assert|exception> <report-directory>

#include "core/Assert.h"
#include "core/CrashHandler.h"
#include "core/Log.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>

#if defined(_MSC_VER)
#define PROBE_NOINLINE __declspec(noinline)
#else
#define PROBE_NOINLINE __attribute__((noinline))
#endif

// volatile keeps the compiler from proving the write is to null and removing it.
static int *volatile g_nullPointer = nullptr;

PROBE_NOINLINE void ProbeTriggerCrash(const char *mode)
{
    if (std::strcmp(mode, "segfault") == 0)
    {
        *g_nullPointer = 42;
    }
    else if (std::strcmp(mode, "abort") == 0)
    {
        std::abort();
    }
    else if (std::strcmp(mode, "assert") == 0)
    {
        // GENI_FATAL rather than GENI_ASSERT so the probe also dies in release builds.
        GENI_FATAL("probe failure: value was %d", 7);
    }
    else if (std::strcmp(mode, "exception") == 0)
    {
        throw std::runtime_error("probe exception");
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        return 2;
    }

    std::error_code ec;
    std::filesystem::create_directories(argv[2], ec);

    Geni::CrashHandlerConfig config;
    config.reportDirectory = argv[2];
    config.applicationName = "geni_crash_probe";
    Geni::InstallCrashHandler(config);

    GENI_LOG_INFO("probe about to crash with mode '%s'", argv[1]);
    ProbeTriggerCrash(argv[1]);

    // Reaching this line means the crash did not happen.
    return 0;
}
