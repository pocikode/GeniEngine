#include "io/ImageWriter.h"
#include "core/Log.h"
#include <cstdio>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace Geni
{

static bool EndsWith(const std::string &text, const char *suffix)
{
    const std::string s(suffix);
    return text.size() >= s.size() && text.compare(text.size() - s.size(), s.size(), s) == 0;
}

bool WriteImage(const std::string &path, int width, int height, int channels, const uint8_t *pixels)
{
    if (width <= 0 || height <= 0 || !pixels)
    {
        GENI_LOG_ERROR("WriteImage '%s': empty image", path.c_str());
        return false;
    }

    if (EndsWith(path, ".png") || EndsWith(path, ".PNG"))
    {
        if (!stbi_write_png(path.c_str(), width, height, channels, pixels, width * channels))
        {
            GENI_LOG_ERROR("WriteImage: cannot write '%s'", path.c_str());
            return false;
        }
        return true;
    }

    if (channels != 3)
    {
        GENI_LOG_ERROR("WriteImage '%s': PPM needs 3 channels, got %d", path.c_str(), channels);
        return false;
    }

    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
    {
        GENI_LOG_ERROR("WriteImage: cannot write '%s'", path.c_str());
        return false;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    std::fwrite(pixels, 1, static_cast<size_t>(width) * static_cast<size_t>(height) * 3, file);
    std::fclose(file);
    return true;
}

} // namespace Geni
