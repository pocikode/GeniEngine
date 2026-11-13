#pragma once

// Reading back what the engine drew: Engine::RequestScreenshot writes a binary PPM.

#include <glm/vec3.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace GeniTest
{

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgb;

    glm::ivec3 At(int x, int y) const
    {
        const size_t i = (static_cast<size_t>(y) * width + x) * 3;
        return glm::ivec3(rgb[i], rgb[i + 1], rgb[i + 2]);
    }
};

inline bool ReadPpm(const std::filesystem::path &path, Image &image)
{
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    int maxValue = 0;
    file >> magic >> image.width >> image.height >> maxValue;
    file.get();
    if (magic != "P6" || image.width <= 0 || image.height <= 0)
    {
        return false;
    }
    image.rgb.resize(static_cast<size_t>(image.width) * image.height * 3);
    file.read(reinterpret_cast<char *>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    return static_cast<bool>(file);
}

inline bool NearColor(const glm::ivec3 &a, const glm::ivec3 &b, int tolerance)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

} // namespace GeniTest
