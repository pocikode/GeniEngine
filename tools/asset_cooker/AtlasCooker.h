#pragma once

// Packing sprite images into one texture (docs/design/0021).

#include <cstdint>
#include <string>
#include <vector>

namespace Geni::cook
{

struct AtlasImage
{
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    // Set by PackAtlas: the image's top-left corner in the atlas (inside its padding).
    int x = 0;
    int y = 0;
};

// Places every image (shelves, tallest first) in the smallest power-of-two atlas up to
// `maxSize` square that fits, leaving `padding` pixels around each. False when they do
// not fit.
bool PackAtlas(std::vector<AtlasImage> &images, int padding, int maxSize, int &atlasWidth, int &atlasHeight);

// The atlas pixels: each image at its place, its border pixels copied outwards into the
// padding so filtering at a region's edge never picks up a neighbour.
std::vector<uint8_t> ComposeAtlas(const std::vector<AtlasImage> &images, int padding, int atlasWidth, int atlasHeight);

} // namespace Geni::cook
