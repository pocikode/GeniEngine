#pragma once

#include <string>

// `city --world`: the reference game's streamed world (ROADMAP M5, docs/design/0007).
struct WorldOptions
{
    std::string world = "worlds/metro/metro.gworld";
    // Fly a straight route across the world at `speed` m/s instead of flying by hand.
    bool route = false;
    double speed = 40.0;
    // Route length in metres; 0: corner to corner across the whole world.
    double distance = 0.0;
    double altitude = 130.0;
    // Quit after this many frames (0: at the end of the route, or never when flying).
    long frames = 0;
    // Exit codes 6, 7 and 8 when, after the initial load: a frame took longer than this,
    // a cell near the camera was not loaded in time, or the memory budget was exceeded.
    double hitchMs = 0.0;
    bool hidden = false;
    int width = 1280;
    int height = 720;
    std::string screenshotPath;
    // Streaming budget preset: desktop, mobile or web.
    std::string platform;
    // Hours (0..24): a stopped TimeOfDayComponent on the sun (docs/design/0018). Negative:
    // the world's authored sun.
    double timeOfDay = -1.0;
};

int RunWorld(const WorldOptions &options);
