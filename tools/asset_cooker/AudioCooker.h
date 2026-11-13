#pragma once

// Cooking sounds (docs/design/0012, "Cooked audio"): 16-bit PCM WAV in, per platform either
// the same PCM or IMA ADPCM WAV (4 bits per sample, about a quarter of the size), which
// miniaudio decodes at run time.

#include <cstdint>
#include <string>
#include <vector>

namespace Geni::cook
{

struct PcmSound
{
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    // Interleaved 16-bit samples.
    std::vector<int16_t> samples;

    uint64_t Frames() const
    {
        return channels ? samples.size() / channels : 0;
    }
    double Seconds() const
    {
        return sampleRate ? static_cast<double>(Frames()) / sampleRate : 0.0;
    }
};

// Reads a RIFF WAV holding 16-bit PCM (format 1, or extensible with a PCM subformat).
// False for anything else, with the reason.
bool ReadPcmWav(const std::vector<uint8_t> &bytes, PcmSound &out, std::string *error = nullptr);

// An IMA ADPCM WAV (format 0x11) of `sound` (1 or 2 channels). The last block is cut short
// so the decoded length is the source length, rounded up to the next whole nibble group
// (at most 7 frames of silence).
std::vector<uint8_t> EncodeImaAdpcmWav(const PcmSound &sound);

// Decodes an IMA ADPCM WAV written by EncodeImaAdpcmWav (for tests and checks).
bool DecodeImaAdpcmWav(const std::vector<uint8_t> &bytes, PcmSound &out, std::string *error = nullptr);

enum class AudioCompression
{
    Auto,
    Pcm,
    Adpcm
};

// The per-platform rule: desktop keeps short sounds (under 2 s, effects that must start
// at once and loop cleanly) as PCM and compresses longer ones; mobile and web compress
// everything. A .meta "compression" of "pcm" or "adpcm" overrides it.
bool ShouldCompress(const PcmSound &sound, const std::string &platform, AudioCompression setting);

} // namespace Geni::cook
