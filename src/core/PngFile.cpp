// SPDX-License-Identifier: MIT
#include "core/PngFile.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>

namespace mevr::png {
namespace {

using Microsoft::WRL::ComPtr;

// RGBA <-> BGRA: swaps the R and B bytes (its own inverse).
std::uint32_t SwapRB(std::uint32_t p) {
    return (p & 0xFF00FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu);
}

bool Fail(std::string* error, char const* what, HRESULT hr) {
    if (error != nullptr) {
        char buffer[96];
        std::snprintf(buffer, sizeof buffer, "%s (HRESULT 0x%08X)", what, static_cast<unsigned>(hr));
        *error = buffer;
    }
    return false;
}

} // namespace

bool Write(std::filesystem::path const& path, int width, int height, std::uint32_t const* pixels, std::string* error) {
    if (width <= 0 || height <= 0 || pixels == nullptr) {
        return Fail(error, "empty image", E_INVALIDARG);
    }
    size_t const count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<std::uint32_t> bgra(count);  // WIC's PNG encoder takes BGRA on every Windows version
    for (size_t i = 0; i < count; ++i) {
        bgra[i] = SwapRB(pixels[i]);
    }
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return Fail(error, "WIC factory", hr);
    }
    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    }
    if (FAILED(hr)) {
        return Fail(error, "can't create the file", hr);
    }
    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA) {
        hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (SUCCEEDED(hr)) {
        UINT const stride = static_cast<UINT>(width) * 4u;
        hr = frame->WritePixels(static_cast<UINT>(height), stride, stride * static_cast<UINT>(height),
            reinterpret_cast<BYTE*>(bgra.data()));
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }
    return SUCCEEDED(hr) || Fail(error, "PNG encoding", hr);
}

bool Read(std::filesystem::path const& path, int& width, int& height, std::vector<std::uint32_t>& pixels, std::string* error) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return Fail(error, "WIC factory", hr);
    }
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr)) {
        hr = decoder->GetFrame(0, &frame);
    }
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom);
    }
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) {
        hr = converter->GetSize(&w, &h);
    }
    if (SUCCEEDED(hr)) {
        pixels.assign(static_cast<size_t>(w) * h, 0u);
        hr = converter->CopyPixels(nullptr, w * 4u, w * h * 4u, reinterpret_cast<BYTE*>(pixels.data()));
    }
    if (FAILED(hr)) {
        return Fail(error, "PNG decoding", hr);
    }
    for (std::uint32_t& p : pixels) {
        p = SwapRB(p);
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

} // namespace mevr::png
