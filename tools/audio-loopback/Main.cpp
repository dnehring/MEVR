// SPDX-License-Identifier: MIT
// What the PC plays, recorded: the default playback device's mix (WASAPI loopback) for a number of seconds, written as a
// float WAV. For measuring what the game's audio does without ears (research/69 "The voices, placed"): where a voice sits
// between the left and right channel and how loud it is, per stretch of time, read by tools/audio-balance.py.
//
//   mevr-audio-loopback <out.wav> [seconds]
//
// Nothing is played and nothing leaves the machine. Silence is recorded as silence (loopback delivers no packets while
// nothing plays; the gaps are filled with zeros by the clock).
#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

template <typename T>
struct Com {
    T* p = nullptr;
    ~Com() {
        if (p != nullptr) {
            p->Release();
        }
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

int Fail(char const* what, HRESULT hr) {
    std::fprintf(stderr, "audio loopback: %s failed (0x%08lX)\n", what, static_cast<unsigned long>(hr));
    return 1;
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void Put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: mevr-audio-loopback <out.wav> [seconds]\n");
        return 2;
    }
    double const seconds = argc > 2 ? _wtof(argv[2]) : 10.0;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        return Fail("CoInitializeEx", hr);
    }
    Com<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        return Fail("the device enumerator", hr);
    }
    Com<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        return Fail("the default playback device", hr);
    }
    Com<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
    if (FAILED(hr)) {
        return Fail("IAudioClient", hr);
    }
    WAVEFORMATEX* mix = nullptr;
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || mix == nullptr) {
        return Fail("GetMixFormat", hr);
    }
    bool isFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto const* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE const*>(mix);
        isFloat = ext->SubFormat.Data1 == 3;  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
    }
    if (!isFloat || mix->wBitsPerSample != 32) {
        std::fprintf(stderr, "audio loopback: the mix is not 32-bit float (tag %u, %u bits)\n", mix->wFormatTag, mix->wBitsPerSample);
        return 1;
    }
    std::uint32_t const channels = mix->nChannels;
    std::uint32_t const rate = mix->nSamplesPerSec;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10'000'000, 0, mix, nullptr);
    if (FAILED(hr)) {
        return Fail("Initialize (loopback)", hr);
    }
    Com<IAudioCaptureClient> capture;
    hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
    if (FAILED(hr)) {
        return Fail("IAudioCaptureClient", hr);
    }
    hr = client->Start();
    if (FAILED(hr)) {
        return Fail("Start", hr);
    }
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(seconds * rate * channels) + rate);
    auto const begin = std::chrono::steady_clock::now();
    for (;;) {
        double const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        if (elapsed >= seconds) {
            break;
        }
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet != 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            std::size_t const count = static_cast<std::size_t>(frames) * channels;
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
                samples.insert(samples.end(), count, 0.0f);
            } else {
                auto const* f = reinterpret_cast<float const*>(data);
                samples.insert(samples.end(), f, f + count);
            }
            capture->ReleaseBuffer(frames);
        }
        // Nothing played: keep the recording on the clock, so its time is the wall's.
        std::size_t const wanted = static_cast<std::size_t>(elapsed * rate) * channels;
        if (samples.size() + static_cast<std::size_t>(rate / 5) * channels < wanted) {
            samples.resize(wanted, 0.0f);
        }
        Sleep(5);
    }
    client->Stop();
    CoTaskMemFree(mix);

    std::vector<std::uint8_t> wav;
    std::uint32_t const bytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    Put32(wav, 36 + bytes);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(wav, 16);
    Put16(wav, 3);  // IEEE float
    Put16(wav, static_cast<std::uint16_t>(channels));
    Put32(wav, rate);
    Put32(wav, rate * channels * 4);
    Put16(wav, static_cast<std::uint16_t>(channels * 4));
    Put16(wav, 32);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    Put32(wav, bytes);
    FILE* file = nullptr;
    if (_wfopen_s(&file, argv[1], L"wb") != 0 || file == nullptr) {
        std::fprintf(stderr, "audio loopback: cannot write the file\n");
        return 1;
    }
    std::fwrite(wav.data(), 1, wav.size(), file);
    std::fwrite(samples.data(), sizeof(float), samples.size(), file);
    std::fclose(file);
    std::printf("audio loopback: %.1f s, %u channels at %u Hz\n", static_cast<double>(samples.size() / channels) / rate, channels, rate);
    return 0;
}
