#include "core/FixedTimestep.h"
#include <cmath>

namespace Geni
{

FixedTimestep::FixedTimestep(double stepSeconds, int maxStepsPerFrame)
    : m_step(stepSeconds > 0.0 ? stepSeconds : 1.0 / 60.0),
      m_maxStepsPerFrame(maxStepsPerFrame > 0 ? maxStepsPerFrame : 1)
{
}

int FixedTimestep::Advance(double frameSeconds)
{
    m_dropped = 0.0;
    if (frameSeconds > 0.0)
    {
        m_accumulator += frameSeconds;
    }

    // The small epsilon keeps a frame that is exactly one step long (within float error)
    // from alternating between zero and two steps.
    const double epsilon = m_step * 1e-9;

    int steps = 0;
    while (m_accumulator + epsilon >= m_step && steps < m_maxStepsPerFrame)
    {
        m_accumulator -= m_step;
        ++steps;
    }

    if (m_accumulator < 0.0)
    {
        m_accumulator = 0.0;
    }

    if (m_accumulator >= m_step)
    {
        // Hit the cap: keep only the fractional part so alpha stays in [0, 1).
        const double remainder = std::fmod(m_accumulator, m_step);
        m_dropped = m_accumulator - remainder;
        m_accumulator = remainder;
    }

    return steps;
}

void FixedTimestep::Reset()
{
    m_accumulator = 0.0;
    m_dropped = 0.0;
}

void FixedTimestep::SetStep(double stepSeconds)
{
    if (stepSeconds > 0.0)
    {
        m_step = stepSeconds;
        Reset();
    }
}

double FixedTimestep::GetStep() const
{
    return m_step;
}

void FixedTimestep::SetMaxStepsPerFrame(int maxSteps)
{
    m_maxStepsPerFrame = maxSteps > 0 ? maxSteps : 1;
}

int FixedTimestep::GetMaxStepsPerFrame() const
{
    return m_maxStepsPerFrame;
}

double FixedTimestep::GetAlpha() const
{
    return m_accumulator / m_step;
}

double FixedTimestep::GetDroppedSeconds() const
{
    return m_dropped;
}

} // namespace Geni
