// SPDX-License-Identifier: MIT
#include "core/voice/Wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace mevr::voice::wav {

namespace {

std::uint32_t U32(std::uint8_t const* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
std::uint16_t U16(std::uint8_t const* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void Put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

}  // namespace

std::optional<Audio> Parse(std::vector<std::uint8_t> const& bytes, std::string* why) {
    auto fail = [&](char const* what) {
        if (why != nullptr) *why = what;
        return std::optional<Audio>{};
    };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return fail("not a RIFF WAVE file");
    }
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    bool haveFormat = false;
    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        std::uint8_t const* const chunk = bytes.data() + pos;
        std::uint32_t const size = U32(chunk + 4);
        std::size_t const body = pos + 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16 || body + 16 > bytes.size()) return fail("a short fmt chunk");
            format = U16(bytes.data() + body);
            channels = U16(bytes.data() + body + 2);
            rate = U32(bytes.data() + body + 4);
            bits = U16(bytes.data() + body + 14);
            if (format == 0xFFFE && size >= 40 && body + 40 <= bytes.size()) format = U16(bytes.data() + body + 24);  // WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!haveFormat) return fail("data before fmt");
            if (channels == 0 || rate == 0 || bits == 0) return fail("a fmt chunk with zero channels, rate or bits");
            bool const isFloat = format == 3;
            if (!(format == 1 || isFloat)) return fail("not PCM or IEEE float");
            if (isFloat && bits != 32) return fail("float samples that aren't 32-bit");
            if (!isFloat && !(bits == 8 || bits == 16 || bits == 24 || bits == 32)) return fail("PCM bits not 8, 16, 24 or 32");
            std::size_t const available = std::min<std::size_t>(size, bytes.size() - body);
            std::size_t const bytesPerSample = bits / 8;
            std::size_t const frames = available / (bytesPerSample * channels);
            Audio audio;
            audio.sampleRate = static_cast<int>(rate);
            audio.channels = channels;
            audio.mono.resize(frames);
            std::uint8_t const* p = bytes.data() + body;
            for (std::size_t f = 0; f < frames; ++f) {
                double sum = 0.0;
                for (std::uint16_t c = 0; c < channels; ++c) {
                    double v = 0.0;
                    if (isFloat) {
                        float s = 0.0f;
                        std::memcpy(&s, p, 4);
                        v = s;
                    } else if (bits == 8) {
                        v = (static_cast<int>(p[0]) - 128) / 128.0;
                    } else if (bits == 16) {
                        v = static_cast<std::int16_t>(U16(p)) / 32768.0;
                    } else if (bits == 24) {
                        std::int32_t s = (p[0] << 8) | (p[1] << 16) | (static_cast<std::int32_t>(p[2]) << 24);
                        v = (s >> 8) / 8388608.0;
                    } else {
                        v = static_cast<std::int32_t>(U32(p)) / 2147483648.0;
                    }
                    sum += v;
                    p += bytesPerSample;
                }
                audio.mono[f] = static_cast<float>(std::clamp(sum / channels, -1.0, 1.0));
            }
            return audio;
        }
        pos = body + size + (size & 1u);
    }
    return fail(haveFormat ? "no data chunk" : "no fmt chunk");
}

std::optional<Audio> Read(std::filesystem::path const& path, std::string* why) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (why != nullptr) *why = "can't open the file";
        return std::nullopt;
    }
    std::vector<std::uint8_t> const bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Parse(bytes, why);
}

std::vector<std::uint8_t> Encode16(std::vector<float> const& mono, int sampleRate) {
    std::vector<std::uint8_t> out;
    std::uint32_t const dataBytes = static_cast<std::uint32_t>(mono.size() * 2);
    out.reserve(44 + dataBytes);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    Put32(out, 36 + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(out, 16);
    Put16(out, 1);  // PCM
    Put16(out, 1);  // mono
    Put32(out, static_cast<std::uint32_t>(sampleRate));
    Put32(out, static_cast<std::uint32_t>(sampleRate) * 2);
    Put16(out, 2);
    Put16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    Put32(out, dataBytes);
    for (float const s : mono) {
        float const c = std::clamp(s, -1.0f, 1.0f);
        Put16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(c * 32767.0f))));
    }
    return out;
}

bool Write16(std::filesystem::path const& path, std::vector<float> const& mono, int sampleRate) {
    std::vector<std::uint8_t> const bytes = Encode16(mono, sampleRate);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

}  // namespace mevr::voice::wav
