#pragma once

// The engine's version (docs/api-policy.md). CMakeLists.txt reads these three numbers
// for project(VERSION), package names and geni_package.json, so this is the one place
// to change it. GENI_VERSION_SUFFIX is "-dev" between releases and empty in a tagged one.
#define GENI_VERSION_MAJOR 0
#define GENI_VERSION_MINOR 2
#define GENI_VERSION_PATCH 0
#define GENI_VERSION_SUFFIX "-dev"

#define GENI_VERSION_STRINGIFY_(x) #x
#define GENI_VERSION_STRINGIFY(x) GENI_VERSION_STRINGIFY_(x)
#define GENI_VERSION_STRING                                                                                            \
    GENI_VERSION_STRINGIFY(GENI_VERSION_MAJOR)                                                                         \
    "." GENI_VERSION_STRINGIFY(GENI_VERSION_MINOR) "." GENI_VERSION_STRINGIFY(GENI_VERSION_PATCH) GENI_VERSION_SUFFIX

// A number that grows with every release, for compile-time checks:
// #if GENI_VERSION_NUMBER >= GENI_MAKE_VERSION(0, 2, 0)
#define GENI_MAKE_VERSION(major, minor, patch) ((major) * 10000 + (minor) * 100 + (patch))
#define GENI_VERSION_NUMBER GENI_MAKE_VERSION(GENI_VERSION_MAJOR, GENI_VERSION_MINOR, GENI_VERSION_PATCH)

// Marks public API that is going away (docs/api-policy.md): it stays for at least one
// minor release with this warning, then is removed.
#if defined(_MSC_VER)
#define GENI_DEPRECATED(message) __declspec(deprecated(message))
#else
#define GENI_DEPRECATED(message) __attribute__((deprecated(message)))
#endif
