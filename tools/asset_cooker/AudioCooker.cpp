#include "AudioCooker.h"

#include <algorithm>
#include <cstring>

namespace Geni::cook
{

namespace
{

const int kStepTable[89] = {
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
const int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatImaAdpcm = 0x11;
constexpr uint16_t kFormatExtensible = 0xFFFE;

uint16_t U16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t U32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
}
void Put16(std::vector<uint8_t> &out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}
void Put32(std::vector<uint8_t> &out, uint32_t value)
{
    Put16(out, value & 0xFFFF);
    Put16(out, value >> 16);
}
void PutTag(std::vector<uint8_t> &out, const char *tag)
{
    out.insert(out.end(), tag, tag + 4);
}

bool Fail(std::string *error, const char *message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

struct Channel
{
    int predictor = 0;
    int index = 0;

    uint8_t Encode(int sample)
    {
        int diff = sample - predictor;
        uint8_t nibble = 0;
        if (diff < 0)
        {
            nibble = 8;
            diff = -diff;
        }
        int step = kStepTable[index];
        int delta = step >> 3;
        if (diff >= step)
        {
            nibble |= 4;
            diff -= step;
            delta += step;
        }
        step >>= 1;
        if (diff >= step)
        {
            nibble |= 2;
            diff -= step;
            delta += step;
        }
        step >>= 1;
        if (diff >= step)
        {
            nibble |= 1;
            delta += step;
        }
        Advance(nibble, delta);
        return nibble;
    }

    int Decode(uint8_t nibble)
    {
        const int step = kStepTable[index];
        int delta = step >> 3;
        if (nibble & 4)
        {
            delta += step;
        }
        if (nibble & 2)
        {
            delta += step >> 1;
        }
        if (nibble & 1)
        {
            delta += step >> 2;
        }
        Advance(nibble, delta);
        return predictor;
    }

  private:
    void Advance(uint8_t nibble, int delta)
    {
        predictor += (nibble & 8) ? -delta : delta;
        predictor = std::clamp(predictor, -32768, 32767);
        index = std::clamp(index + kIndexTable[nibble], 0, 88);
    }
};

// Bytes per block: 256 per channel at 11 kHz, doubling with the rate (the usual choice).
uint32_t BlockAlign(uint32_t sampleRate, uint32_t channels)
{
    return 256u * channels * std::max(1u, sampleRate / 11025u);
}

uint32_t SamplesPerBlock(uint32_t blockAlign, uint32_t channels)
{
    return (blockAlign - 4u * channels) * 8u / (4u * channels) + 1u;
}

} // namespace

bool ReadPcmWav(const std::vector<uint8_t> &bytes, PcmSound &out, std::string *error)
{
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        return Fail(error, "not a RIFF WAVE file");
    }
    bool haveFormat = false;
    uint16_t bits = 0;
    size_t offset = 12;
    while (offset + 8 <= bytes.size())
    {
        const uint8_t *chunk = bytes.data() + offset;
        const uint32_t size = U32(chunk + 4);
        const size_t body = offset + 8;
        if (body + size > bytes.size())
        {
            return Fail(error, "a chunk runs past the end of the file");
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16)
        {
            uint16_t format = U16(chunk + 8);
            out.channels = U16(chunk + 10);
            out.sampleRate = U32(chunk + 12);
            bits = U16(chunk + 22);
            if (format == kFormatExtensible && size >= 26)
            {
                format = U16(chunk + 8 + 24); // the first two bytes of the subformat GUID
            }
            if (format != kFormatPcm || bits != 16)
            {
                return Fail(error, "only 16-bit PCM WAV files are compressed");
            }
            haveFormat = true;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            if (!haveFormat || out.channels == 0 || out.sampleRate == 0)
            {
                return Fail(error, "data before a usable fmt chunk");
            }
            out.samples.resize(size / 2);
            for (size_t i = 0; i < out.samples.size(); ++i)
            {
                out.samples[i] = static_cast<int16_t>(U16(bytes.data() + body + 2 * i));
            }
            out.samples.resize(out.samples.size() - out.samples.size() % out.channels);
            return true;
        }
        offset = body + size + (size & 1); // chunks are padded to even sizes
    }
    return Fail(error, "no data chunk");
}

std::vector<uint8_t> EncodeImaAdpcmWav(const PcmSound &sound)
{
    const uint32_t channels = sound.channels;
    const uint32_t blockAlign = BlockAlign(sound.sampleRate, channels);
    const uint32_t perBlock = SamplesPerBlock(blockAlign, channels);
    const uint64_t frames = sound.Frames();

    std::vector<uint8_t> data;
    std::vector<Channel> state(channels);
    for (uint64_t start = 0; start < frames; start += perBlock)
    {
        const uint64_t count = std::min<uint64_t>(perBlock, frames - start);
        auto sample = [&](uint64_t frame, uint32_t channel) -> int {
            // Past the end (the last nibble group): repeat the last sample.
            const uint64_t clamped = std::min(frame, frames - 1);
            return sound.samples[clamped * channels + channel];
        };
        // Header per channel: the first sample exactly, then the step index.
        for (uint32_t c = 0; c < channels; ++c)
        {
            state[c].predictor = sample(start, c);
            Put16(data, static_cast<uint16_t>(static_cast<int16_t>(state[c].predictor)));
            data.push_back(static_cast<uint8_t>(state[c].index));
            data.push_back(0);
        }
        // The rest in groups of eight samples per channel (four bytes), channels taking turns.
        const uint64_t rest = count - 1;
        const uint64_t groups = (rest + 7) / 8;
        for (uint64_t g = 0; g < groups; ++g)
        {
            for (uint32_t c = 0; c < channels; ++c)
            {
                for (int b = 0; b < 4; ++b)
                {
                    const uint64_t frame = start + 1 + g * 8 + static_cast<uint64_t>(b) * 2;
                    const uint8_t low = state[c].Encode(sample(frame, c));
                    const uint8_t high = state[c].Encode(sample(frame + 1, c));
                    data.push_back(static_cast<uint8_t>(low | (high << 4)));
                }
            }
        }
    }

    std::vector<uint8_t> out;
    PutTag(out, "RIFF");
    Put32(out, static_cast<uint32_t>(4 + (8 + 20) + (8 + 4) + 8 + data.size() + (data.size() & 1)));
    PutTag(out, "WAVE");
    PutTag(out, "fmt ");
    Put32(out, 20);
    Put16(out, kFormatImaAdpcm);
    Put16(out, channels);
    Put32(out, sound.sampleRate);
    Put32(out, static_cast<uint32_t>(static_cast<uint64_t>(sound.sampleRate) * blockAlign / perBlock));
    Put16(out, blockAlign);
    Put16(out, 4);
    Put16(out, 2);
    Put16(out, perBlock);
    PutTag(out, "fact");
    Put32(out, 4);
    Put32(out, static_cast<uint32_t>(frames));
    PutTag(out, "data");
    Put32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    if (data.size() & 1)
    {
        out.push_back(0);
    }
    return out;
}

bool DecodeImaAdpcmWav(const std::vector<uint8_t> &bytes, PcmSound &out, std::string *error)
{
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0)
    {
        return Fail(error, "not a RIFF file");
    }
    uint32_t blockAlign = 0;
    uint32_t frames = 0;
    size_t offset = 12;
    while (offset + 8 <= bytes.size())
    {
        const uint8_t *chunk = bytes.data() + offset;
        const uint32_t size = U32(chunk + 4);
        const size_t body = offset + 8;
        if (body + size > bytes.size())
        {
            return Fail(error, "truncated chunk");
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            if (U16(chunk + 8) != kFormatImaAdpcm)
            {
                return Fail(error, "not IMA ADPCM");
            }
            out.channels = U16(chunk + 10);
            out.sampleRate = U32(chunk + 12);
            blockAlign = U16(chunk + 20);
        }
        else if (std::memcmp(chunk, "fact", 4) == 0)
        {
            frames = U32(chunk + 8);
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            if (!blockAlign || !out.channels)
            {
                return Fail(error, "data before fmt");
            }
            const uint32_t channels = out.channels;
            out.samples.clear();
            for (size_t block = body; block < body + size; block += blockAlign)
            {
                const size_t end = std::min<size_t>(block + blockAlign, body + size);
                std::vector<Channel> state(channels);
                std::vector<std::vector<int16_t>> decoded(channels);
                for (uint32_t c = 0; c < channels; ++c)
                {
                    state[c].predictor = static_cast<int16_t>(U16(bytes.data() + block + 4 * c));
                    state[c].index = std::min<int>(bytes[block + 4 * c + 2], 88);
                    decoded[c].push_back(static_cast<int16_t>(state[c].predictor));
                }
                for (size_t p = block + 4 * channels; p + 4 * channels <= end; p += 4 * channels)
                {
                    for (uint32_t c = 0; c < channels; ++c)
                    {
                        for (int b = 0; b < 4; ++b)
                        {
                            const uint8_t byte = bytes[p + 4 * c + b];
                            decoded[c].push_back(static_cast<int16_t>(state[c].Decode(byte & 0x0F)));
                            decoded[c].push_back(static_cast<int16_t>(state[c].Decode(byte >> 4)));
                        }
                    }
                }
                for (size_t i = 0; i < decoded[0].size(); ++i)
                {
                    for (uint32_t c = 0; c < channels; ++c)
                    {
                        out.samples.push_back(decoded[c][i]);
                    }
                }
            }
            if (frames && out.samples.size() > static_cast<size_t>(frames) * channels)
            {
                out.samples.resize(static_cast<size_t>(frames) * channels);
            }
            return true;
        }
        offset = body + size + (size & 1);
    }
    return Fail(error, "no data chunk");
}

bool ShouldCompress(const PcmSound &sound, const std::string &platform, AudioCompression setting)
{
    if (setting != AudioCompression::Auto)
    {
        return setting == AudioCompression::Adpcm;
    }
    if (platform == "desktop")
    {
        return sound.Seconds() >= 2.0;
    }
    return true;
}

} // namespace Geni::cook
