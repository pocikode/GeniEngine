#pragma once

#include "rhi/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Geni
{

// A KTX2 image with uncompressed or block-compressed levels. The cooker writes one
// format per file; the runtime picks the variant the device supports.
struct Ktx2Image
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    rhi::Format format = rhi::Format::Undefined;
    std::vector<std::vector<uint8_t>> levels;
};

// Reads a KTX2 file with no supercompression. Returns false and fills `error` for a
// malformed file, an unsupported format, or a level that lies outside the file.
bool ReadKtx2(const std::vector<uint8_t> &bytes, Ktx2Image &out, std::string *error = nullptr);

} // namespace Geni
