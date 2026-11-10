#include "core/FixedTimestep.h"
#include <doctest/doctest.h>

using Geni::FixedTimestep;

TEST_SUITE("fixed timestep")
{
    TEST_CASE("a frame exactly one step long runs one step")
    {
        FixedTimestep clock(1.0 / 60.0);
        for (int frame = 0; frame < 600; ++frame)
        {
            CHECK(clock.Advance(1.0 / 60.0) == 1);
        }
    }

    TEST_CASE("fast frames accumulate until a step is due")
    {
        FixedTimestep clock(0.010);
        CHECK(clock.Advance(0.004) == 0);
        CHECK(clock.GetAlpha() == doctest::Approx(0.4));
        CHECK(clock.Advance(0.004) == 0);
        CHECK(clock.Advance(0.004) == 1);
        CHECK(clock.GetAlpha() == doctest::Approx(0.2));
    }

    TEST_CASE("a slow frame runs several steps")
    {
        FixedTimestep clock(0.010, 8);
        CHECK(clock.Advance(0.035) == 3);
        CHECK(clock.GetAlpha() == doctest::Approx(0.5));
        CHECK(clock.GetDroppedSeconds() == doctest::Approx(0.0));
    }

    TEST_CASE("step count does not depend on how time is sliced into frames")
    {
        const double frameTimes[] = {1.0 / 30.0, 1.0 / 60.0, 1.0 / 144.0, 1.0 / 240.0};
        for (double frameTime : frameTimes)
        {
            FixedTimestep clock(1.0 / 60.0);
            int steps = 0;
            double elapsed = 0.0;
            while (elapsed < 10.0 - 1e-9)
            {
                steps += clock.Advance(frameTime);
                elapsed += frameTime;
            }
            // 10 seconds at 60 Hz, give or take the step still accumulating.
            CHECK(steps >= 599);
            CHECK(steps <= 601);
        }
    }

    TEST_CASE("a stall is capped and the excess time is dropped")
    {
        // Powers of two keep the arithmetic exact.
        FixedTimestep clock(0.25, 4);
        CHECK(clock.Advance(10.0) == 4);
        CHECK(clock.GetDroppedSeconds() == doctest::Approx(9.0));
        CHECK(clock.GetAlpha() >= 0.0);
        CHECK(clock.GetAlpha() < 1.0);

        // The next normal frame is back to normal: no burst of catch-up steps.
        CHECK(clock.Advance(0.25) == 1);
        CHECK(clock.GetDroppedSeconds() == doctest::Approx(0.0));
    }

    TEST_CASE("zero and negative frame times run nothing")
    {
        FixedTimestep clock(0.010);
        CHECK(clock.Advance(0.0) == 0);
        CHECK(clock.Advance(-5.0) == 0);
        CHECK(clock.GetAlpha() == doctest::Approx(0.0));
    }

    TEST_CASE("invalid settings fall back to usable values")
    {
        FixedTimestep clock(0.0, 0);
        CHECK(clock.GetStep() == doctest::Approx(1.0 / 60.0));
        CHECK(clock.GetMaxStepsPerFrame() == 1);

        clock.SetStep(-1.0);
        CHECK(clock.GetStep() == doctest::Approx(1.0 / 60.0));
    }

    TEST_CASE("Reset forgets accumulated time")
    {
        FixedTimestep clock(0.010);
        clock.Advance(0.009);
        clock.Reset();
        CHECK(clock.Advance(0.009) == 0);
    }
}
