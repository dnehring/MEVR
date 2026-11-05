// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// RIFF WAVE files for the voice path's developer channel and bench (research/54 step 2): PCM 8/16/24/32-bit and IEEE
// float, any channel count mixed down to mono, read whole; and a 16-bit mono writer for test fixtures and diagnostics
// the player starts. Unit-tested.
namespace mevr::voice::wav {

struct Audio {
    int sampleRate = 0;
    int channels = 0;           // as stored (the samples are mixed down)
    std::vector<float> mono;    // -1..1
    double Seconds() const { return sampleRate > 0 ? static_cast<double>(mono.size()) / sampleRate : 0.0; }
};

// The parsed file, or nullopt with `why` set.
std::optional<Audio> Parse(std::vector<std::uint8_t> const& bytes, std::string* why = nullptr);
std::optional<Audio> Read(std::filesystem::path const& path, std::string* why = nullptr);
// 16-bit PCM mono.
std::vector<std::uint8_t> Encode16(std::vector<float> const& mono, int sampleRate);
bool Write16(std::filesystem::path const& path, std::vector<float> const& mono, int sampleRate);

}  // namespace mevr::voice::wav
