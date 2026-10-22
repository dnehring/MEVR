#include "core/vr/ImageConvert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace mevr::vr::image {
namespace {

// sRGB byte -> linear, exact per value.
std::array<float, 256> const& DecodeTable() {
    static std::array<float, 256> const table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            t[static_cast<size_t>(i)] = SrgbToLinear(static_cast<float>(i) / 255.0f);
        }
        return t;
    }();
    return table;
}

std::uint8_t ToByte(float value01) {
    return static_cast<std::uint8_t>(std::clamp(value01, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Straight alpha from premultiplied, in linear light, then encoded again.
std::uint8_t Unpremultiply(std::uint8_t encoded, float alpha) {
    if (alpha <= 0.0f) {
        return 0;
    }
    return LinearToSrgb8(DecodeTable()[encoded] / alpha);
}

std::uint32_t FromEncoded(float r, float g, float b, float a, Options const& options) {
    std::uint8_t const alpha = options.opaque ? std::uint8_t{255} : ToByte(a);
    std::uint8_t rr = ToByte(r), gg = ToByte(g), bb = ToByte(b);
    if (options.premultiplied && !options.opaque) {
        float const af = std::clamp(a, 0.0f, 1.0f);
        rr = Unpremultiply(rr, af);
        gg = Unpremultiply(gg, af);
        bb = Unpremultiply(bb, af);
    }
    return Pack(rr, gg, bb, alpha);
}

} // namespace

bool Supported(int dxgiFormat) {
    return BytesPerPixel(dxgiFormat) != 0;
}

int BytesPerPixel(int dxgiFormat) {
    switch (dxgiFormat) {
    case format::kR8G8B8A8Typeless:
    case format::kR8G8B8A8Unorm:
    case format::kR8G8B8A8UnormSrgb:
    case format::kB8G8R8A8Unorm:
    case format::kB8G8R8A8Typeless:
    case format::kB8G8R8A8UnormSrgb:
    case format::kR10G10B10A2Typeless:
    case format::kR10G10B10A2Unorm:
        return 4;
    case format::kR16G16B16A16Typeless:
    case format::kR16G16B16A16Float:
        return 8;
    default:
        return 0;
    }
}

bool IsFloat(int dxgiFormat) {
    return dxgiFormat == format::kR16G16B16A16Float || dxgiFormat == format::kR16G16B16A16Typeless;
}

float HalfToFloat(std::uint16_t half) {
    std::uint32_t const sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    std::uint32_t exponent = (half >> 10) & 0x1Fu;
    std::uint32_t mantissa = half & 0x3FFu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;  // +-0
        } else {
            // Subnormal: normalize it.
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3FFu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1F) {
        bits = sign | 0x7F800000u | (mantissa << 13);  // inf / NaN
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

float SrgbToLinear(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float linear) {
    if (!(linear > 0.0f)) {
        return 0.0f;  // also NaN
    }
    if (linear >= 1.0f) {
        return 1.0f;
    }
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

std::uint8_t LinearToSrgb8(float linear) {
    return ToByte(LinearToSrgb(linear));
}

bool ConvertRow(int dxgiFormat, void const* source, int width, Options const& options, std::uint32_t* out) {
    if (source == nullptr || out == nullptr || width < 0) {
        return false;
    }
    auto const* bytes = static_cast<std::uint8_t const*>(source);
    switch (dxgiFormat) {
    case format::kR8G8B8A8Typeless:
    case format::kR8G8B8A8Unorm:
    case format::kR8G8B8A8UnormSrgb:
    case format::kB8G8R8A8Unorm:
    case format::kB8G8R8A8Typeless:
    case format::kB8G8R8A8UnormSrgb: {
        bool const bgra = dxgiFormat == format::kB8G8R8A8Unorm || dxgiFormat == format::kB8G8R8A8Typeless ||
            dxgiFormat == format::kB8G8R8A8UnormSrgb;
        for (int x = 0; x < width; ++x) {
            std::uint8_t const* p = bytes + static_cast<size_t>(x) * 4;
            std::uint8_t const r = bgra ? p[2] : p[0];
            std::uint8_t const g = p[1];
            std::uint8_t const b = bgra ? p[0] : p[2];
            std::uint8_t const a = options.opaque ? std::uint8_t{255} : p[3];
            if (options.premultiplied && !options.opaque) {
                float const af = static_cast<float>(a) / 255.0f;
                out[x] = Pack(Unpremultiply(r, af), Unpremultiply(g, af), Unpremultiply(b, af), a);
            } else {
                out[x] = Pack(r, g, b, a);
            }
        }
        return true;
    }
    case format::kR10G10B10A2Typeless:
    case format::kR10G10B10A2Unorm:
        for (int x = 0; x < width; ++x) {
            std::uint32_t v = 0;
            std::memcpy(&v, bytes + static_cast<size_t>(x) * 4, 4);
            out[x] = FromEncoded(static_cast<float>(v & 0x3FFu) / 1023.0f, static_cast<float>((v >> 10) & 0x3FFu) / 1023.0f,
                static_cast<float>((v >> 20) & 0x3FFu) / 1023.0f, static_cast<float>(v >> 30) / 3.0f, options);
        }
        return true;
    case format::kR16G16B16A16Typeless:
    case format::kR16G16B16A16Float:
        for (int x = 0; x < width; ++x) {
            std::uint16_t h[4];
            std::memcpy(h, bytes + static_cast<size_t>(x) * 8, 8);
            float const a = options.opaque ? 1.0f : std::clamp(HalfToFloat(h[3]), 0.0f, 1.0f);
            float r = HalfToFloat(h[0]) * options.whiteScale;
            float g = HalfToFloat(h[1]) * options.whiteScale;
            float b = HalfToFloat(h[2]) * options.whiteScale;
            if (options.premultiplied && !options.opaque) {
                if (a > 0.0f) {
                    r /= a;
                    g /= a;
                    b /= a;
                } else {
                    r = g = b = 0.0f;
                }
            }
            out[x] = Pack(LinearToSrgb8(r), LinearToSrgb8(g), LinearToSrgb8(b), ToByte(a));
        }
        return true;
    default:
        return false;
    }
}

std::uint32_t Over(std::uint32_t destination, std::uint32_t source) {
    std::uint32_t const sa = source >> 24;
    if (sa == 255) {
        return source;
    }
    if (sa == 0) {
        return destination;
    }
    auto const& decode = DecodeTable();
    float const a = static_cast<float>(sa) / 255.0f;
    float const da = static_cast<float>(destination >> 24) / 255.0f;
    float const outA = a + da * (1.0f - a);
    std::uint8_t channels[3];
    for (int c = 0; c < 3; ++c) {
        int const shift = 8 * c;
        float const s = decode[(source >> shift) & 0xFFu];
        float const d = decode[(destination >> shift) & 0xFFu];
        float const premultiplied = s * a + d * da * (1.0f - a);
        channels[c] = outA > 0.0f ? LinearToSrgb8(premultiplied / outA) : std::uint8_t{0};
    }
    return Pack(channels[0], channels[1], channels[2], ToByte(outA));
}

} // namespace mevr::vr::image
