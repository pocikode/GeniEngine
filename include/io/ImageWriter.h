#pragma once

#include <cstdint>
#include <string>

namespace Geni
{

// Writes 8-bit pixels, top row first, as PNG when the path ends in ".png" and as binary
// PPM otherwise (channels must then be 3). Returns false and logs on failure.
bool WriteImage(const std::string &path, int width, int height, int channels, const uint8_t *pixels);

} // namespace Geni
