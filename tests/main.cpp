#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "TestEngine.h"

int main(int argc, char **argv)
{
    doctest::Context context(argc, argv);
    const int result = context.run();

    // The engine is started lazily by the first gpu test and shared by the rest.
    GeniTest::ShutdownEngine();
    return result;
}
