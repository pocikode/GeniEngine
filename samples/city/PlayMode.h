#pragma once

#include <string>

// `city --play`: the reference game (ROADMAP M6). Walk the streamed metro world, drive
// the red car, among pedestrians and traffic; the rules are Lua (assets/scripts/game.lua).
struct PlayOptions
{
    // Plays itself through the input actions and checks the result (exit code 9 when a
    // step fails, 10 on a Lua error).
    bool autopilot = false;
    bool crowd = true;
    bool traffic = true;
    // Stay on the title screen (for the screenshot test).
    bool titleOnly = false;
    long frames = 0;
    bool hidden = false;
    int width = 1280;
    int height = 720;
    std::string screenshotPath;
};

int RunPlay(const PlayOptions &options);
