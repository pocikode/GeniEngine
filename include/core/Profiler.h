#pragma once

// Thin wrapper over Tracy so engine and game code never include Tracy directly.
// Everything compiles to nothing unless the build sets GENI_ENABLE_TRACY (CMake option
// of the same name), so the macros are safe to leave in shipping code.

#if defined(GENI_ENABLE_TRACY)

#include <tracy/Tracy.hpp>

// Marks the end of a frame. Called once per frame by Engine::Tick.
#define GENI_PROFILE_FRAME() FrameMark
// Times the enclosing scope. `name` must be a string literal.
#define GENI_PROFILE_SCOPE(name) ZoneScopedN(name)
// Times the enclosing function, named after it.
#define GENI_PROFILE_FUNCTION() ZoneScoped
// Plots a numeric value over time. `name` must be a string literal.
#define GENI_PROFILE_PLOT(name, value) TracyPlot(name, value)
// Adds a text message to the Tracy timeline.
#define GENI_PROFILE_MESSAGE(text, size) TracyMessage(text, size)
// Names the calling thread in the profiler.
#define GENI_PROFILE_THREAD(name) tracy::SetThreadName(name)

#else

#define GENI_PROFILE_FRAME() ((void)0)
#define GENI_PROFILE_SCOPE(name) ((void)0)
#define GENI_PROFILE_FUNCTION() ((void)0)
#define GENI_PROFILE_PLOT(name, value) ((void)0)
#define GENI_PROFILE_MESSAGE(text, size) ((void)0)
#define GENI_PROFILE_THREAD(name) ((void)0)

#endif
