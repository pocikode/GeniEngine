#pragma once

#include <string>

// `city --terrain`: the reference game's terrain view (docs/design/0022, 0023): 512 m of
// cooked hills with a time of day and optional weather; fly with W/A/S/D, Q/E and the
// right mouse button.
struct TerrainOptions
{
    std::string terrain = "terrain/hills.gterrain";
    // Hours: a stopped TimeOfDayComponent on the sun.
    double timeOfDay = 10.5;
    // A WeatherComponent preset: rain, snow or fog; empty: clear.
    std::string weather;
    long frames = 0;
    bool hidden = false;
    int width = 1280;
    int height = 720;
    std::string screenshotPath;
};

int ParseTerrainOptions(int argc, char **argv, TerrainOptions &options);
int RunTerrain(const TerrainOptions &options);
