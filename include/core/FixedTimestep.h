#pragma once

namespace Geni
{

// Turns variable frame times into a whole number of fixed-size simulation steps.
// Simulation (physics, gameplay) runs at a constant rate regardless of how fast frames
// are rendered; the leftover time is carried to the next frame and exposed as an
// interpolation factor.
class FixedTimestep
{
  public:
    explicit FixedTimestep(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 8);

    // Adds one frame's elapsed time and returns how many fixed steps to run now.
    // If the frame took so long that more than maxStepsPerFrame would be needed, the
    // excess time is dropped (the simulation slows down instead of spiralling).
    int Advance(double frameSeconds);

    // Forgets accumulated time, e.g. after a level load or a pause.
    void Reset();

    void SetStep(double stepSeconds);
    double GetStep() const;

    void SetMaxStepsPerFrame(int maxSteps);
    int GetMaxStepsPerFrame() const;

    // How far the current time is between the last fixed step (0) and the next one (1).
    double GetAlpha() const;

    // Simulated time dropped by the last Advance call because of the step cap.
    double GetDroppedSeconds() const;

  private:
    double m_step;
    int m_maxStepsPerFrame;
    double m_accumulator = 0.0;
    double m_dropped = 0.0;
};

} // namespace Geni
