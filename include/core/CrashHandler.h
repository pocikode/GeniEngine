#pragma once

#include <string>

namespace Geni
{

// Symbolized stack trace of the calling thread, one frame per line.
// `skipFrames` drops that many innermost frames in addition to CaptureStackTrace itself.
std::string CaptureStackTrace(int skipFrames = 0, int maxFrames = 64);

struct CrashHandlerConfig
{
    // Folder for crash report files. Empty means the current working directory.
    std::string reportDirectory;
    // Shown in the report header.
    std::string applicationName = "GeniEngine";
};

// Installs process-wide handlers for fatal signals (SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS),
// unhandled C++ exceptions and, on Windows, unhandled structured exceptions.
// On a crash the handler writes a report (reason, stack trace, recent log lines) to stderr
// and to `geni_crash_<date>_<time>.log`, then lets the process die with its usual exit status.
// Safe to call more than once; later calls only update the configuration.
void InstallCrashHandler(const CrashHandlerConfig &config = {});

bool IsCrashHandlerInstalled();

} // namespace Geni
