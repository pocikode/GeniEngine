#pragma once

#include "core/Log.h"

// Assertions are on in debug builds and off when NDEBUG is defined. Define
// GENI_ENABLE_ASSERTS to 0 or 1 for the whole build (CMake: GENI_FORCE_ASSERTS) to override.
#if !defined(GENI_ENABLE_ASSERTS)
#if defined(NDEBUG)
#define GENI_ENABLE_ASSERTS 0
#else
#define GENI_ENABLE_ASSERTS 1
#endif
#endif

namespace Geni
{

// `message` is empty when the assertion carried none.
using AssertHandler = void (*)(const char *expression, const char *file, int line, const char *message);

// Replaces what happens when an assertion fails and returns the previous handler.
// Pass nullptr to restore the default, which logs the failure with a stack trace and aborts.
// A custom handler may return, in which case execution continues after the assertion;
// tests use this to check that an assertion fires.
AssertHandler SetAssertHandler(AssertHandler handler);

void ReportAssertFailure(const char *expression, const char *file, int line);
void ReportAssertFailure(const char *expression, const char *file, int line, const char *fmt, ...) GENI_PRINTF_FORMAT(
    4, 5
);

// Logs a fatal message with a stack trace and aborts. Not affected by GENI_ENABLE_ASSERTS.
[[noreturn]] void FatalError(const char *file, int line, const char *fmt, ...) GENI_PRINTF_FORMAT(3, 4);

} // namespace Geni

#if GENI_ENABLE_ASSERTS

#define GENI_ASSERT(condition)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            ::Geni::ReportAssertFailure(#condition, __FILE__, __LINE__);                                               \
        }                                                                                                              \
    } while (false)

// GENI_ASSERT_MSG(index < count, "index %d out of range (count %d)", index, count);
#define GENI_ASSERT_MSG(condition, ...)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            ::Geni::ReportAssertFailure(#condition, __FILE__, __LINE__, __VA_ARGS__);                                  \
        }                                                                                                              \
    } while (false)

#else

// The condition is not evaluated, but still has to compile.
#define GENI_ASSERT(condition)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(!(condition));                                                                                    \
    } while (false)

#define GENI_ASSERT_MSG(condition, ...)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(!(condition));                                                                                    \
    } while (false)

#endif

// Always on, in every build type.
#define GENI_FATAL(...) ::Geni::FatalError(__FILE__, __LINE__, __VA_ARGS__)
