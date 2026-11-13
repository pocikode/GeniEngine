#include "AtlasCooker.h"

#include <algorithm>
#include <numeric>

namespace Geni::cook
{

namespace
{

// Shelf packing into width x height; false when something does not fit.
bool TryPack(std::vector<AtlasImage> &images, const std::vector<size_t> &order, int padding, int width, int height)
{
    int shelfY = 0;
    int shelfHeight = 0;
    int x = 0;
    for (size_t index : order)
    {
        AtlasImage &image = images[index];
        const int w = image.width + 2 * padding;
        const int h = image.height + 2 * padding;
        if (w > width)
        {
            return false;
        }
        if (x + w > width)
        {
            shelfY += shelfHeight;
            x = 0;
            shelfHeight = 0;
        }
        if (shelfY + h > height)
        {
            return false;
        }
        image.x = x + padding;
        image.y = shelfY + padding;
        x += w;
        shelfHeight = std::max(shelfHeight, h);
    }
    return true;
}

} // namespace

bool PackAtlas(std::vector<AtlasImage> &images, int padding, int maxSize, int &atlasWidth, int &atlasHeight)
{
    std::vector<size_t> order(images.size());
    std::iota(order.begin(), order.end(), size_t(0));
    // Tallest first, then widest, then by name so the result does not depend on list order.
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (images[a].height != images[b].height)
        {
            return images[a].height > images[b].height;
        }
        if (images[a].width != images[b].width)
        {
            return images[a].width > images[b].width;
        }
        return images[a].name < images[b].name;
    });
    // Grow the smaller side first: 64x64, 128x64, 128x128, ...
    for (int width = 64, height = 64; width <= maxSize && height <= maxSize;)
    {
        if (TryPack(images, order, padding, width, height))
        {
            atlasWidth = width;
            atlasHeight = height;
            return true;
        }
        if (width <= height)
        {
            width *= 2;
        }
        else
        {
            height *= 2;
        }
    }
    return false;
}

std::vector<uint8_t> ComposeAtlas(const std::vector<AtlasImage> &images, int padding, int atlasWidth, int atlasHeight)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
    for (const AtlasImage &image : images)
    {
        for (int y = -padding; y < image.height + padding; ++y)
        {
            const int sy = std::clamp(y, 0, image.height - 1);
            for (int x = -padding; x < image.width + padding; ++x)
            {
                const int sx = std::clamp(x, 0, image.width - 1);
                const size_t from = (static_cast<size_t>(sy) * image.width + sx) * 4;
                const size_t to = (static_cast<size_t>(image.y + y) * atlasWidth + image.x + x) * 4;
                std::copy_n(
                    image.rgba.begin() + static_cast<std::ptrdiff_t>(from), 4,
                    pixels.begin() + static_cast<std::ptrdiff_t>(to)
                );
            }
        }
    }
    return pixels;
}

} // namespace Geni::cook
