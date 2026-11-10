// image_compare: checks a rendered image against a reference.
//
// Usage: image_compare ACTUAL REFERENCE [--tolerance N] [--max-fraction F] [--diff OUT.png]
//
// A pixel differs when any RGB channel is more than N apart (default 8 of 255). The images
// match when at most F of the pixels differ (default 0.005, i.e. 0.5%). Exit code 0 means
// they match, 1 that they do not, 2 a usage or file error. With --diff, a failing run
// writes an image with differing pixels in red over a dimmed copy of the reference.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgb;
};

bool Load(const char *path, Image &image)
{
    int channels = 0;
    unsigned char *data = stbi_load(path, &image.width, &image.height, &channels, 3);
    if (!data)
    {
        std::fprintf(stderr, "image_compare: cannot read '%s': %s\n", path, stbi_failure_reason());
        return false;
    }
    image.rgb.assign(data, data + static_cast<size_t>(image.width) * image.height * 3);
    stbi_image_free(data);
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(
            stderr, "Usage: image_compare ACTUAL REFERENCE [--tolerance N] [--max-fraction F] [--diff OUT.png]\n"
        );
        return 2;
    }

    int tolerance = 8;
    double maxFraction = 0.005;
    std::string diffPath;
    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--tolerance") == 0 && i + 1 < argc)
        {
            tolerance = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--max-fraction") == 0 && i + 1 < argc)
        {
            maxFraction = std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--diff") == 0 && i + 1 < argc)
        {
            diffPath = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "image_compare: unknown argument '%s'\n", argv[i]);
            return 2;
        }
    }

    Image actual;
    Image reference;
    if (!Load(argv[1], actual) || !Load(argv[2], reference))
    {
        return 2;
    }

    if (actual.width != reference.width || actual.height != reference.height)
    {
        std::fprintf(
            stderr, "image_compare: size %dx%d does not match reference %dx%d\n", actual.width, actual.height,
            reference.width, reference.height
        );
        return 1;
    }

    const size_t pixelCount = static_cast<size_t>(actual.width) * actual.height;
    std::vector<unsigned char> diff(pixelCount * 3);
    size_t differing = 0;
    int largest = 0;
    for (size_t p = 0; p < pixelCount; ++p)
    {
        int worst = 0;
        for (int c = 0; c < 3; ++c)
        {
            const int delta = std::abs(int(actual.rgb[p * 3 + c]) - int(reference.rgb[p * 3 + c]));
            worst = delta > worst ? delta : worst;
        }
        largest = worst > largest ? worst : largest;

        if (worst > tolerance)
        {
            ++differing;
            diff[p * 3 + 0] = 255;
            diff[p * 3 + 1] = 0;
            diff[p * 3 + 2] = 0;
        }
        else
        {
            for (int c = 0; c < 3; ++c)
            {
                diff[p * 3 + c] = static_cast<unsigned char>(reference.rgb[p * 3 + c] / 4);
            }
        }
    }

    const double fraction = static_cast<double>(differing) / static_cast<double>(pixelCount);
    const bool match = fraction <= maxFraction;
    std::printf(
        "image_compare: %zu of %zu pixels differ by more than %d (%.4f%%, allowed %.4f%%); largest "
        "difference %d -> %s\n",
        differing, pixelCount, tolerance, fraction * 100.0, maxFraction * 100.0, largest, match ? "MATCH" : "DIFFERENT"
    );

    if (!match && !diffPath.empty())
    {
        if (stbi_write_png(diffPath.c_str(), actual.width, actual.height, 3, diff.data(), actual.width * 3))
        {
            std::printf("image_compare: diff written to %s\n", diffPath.c_str());
        }
    }
    return match ? 0 : 1;
}
