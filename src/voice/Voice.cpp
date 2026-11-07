// SPDX-License-Identifier: MIT
// MEVR-Voice.dll (research/54): the microphone and the recognizer, behind src/voice/VoiceApi.h. Moonshine Voice's streaming
// models (MIT) on ONNX Runtime (MIT), loaded from the model folder the host names; WASAPI shared-mode capture of the headset's
// microphone on a thread of its own (MMCSS "Audio"), fed into a Moonshine stream while a listen is on and for `tailMs` after
// it ends, a worker that asks for a partial transcript every `intervalMs` and the final one after the stop; the game's own
// audio session ducked while listening (ISimpleAudioVolume on this process's session). Nothing is written to disk.
#include "voice/VoiceApi.h"

#include "core/voice/Wav.hpp"
#include "moonshine-c-api.h"

#define INITGUID  // the device property keys (PKEY_Device_FriendlyName) defined here, not pulled from a library
#include <Windows.h>
#include <initguid.h>
#include <propidl.h>
#include <propkeydef.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace voice = mevr::vr::voice;

struct Config {
    std::string modelDir;
    int arch = MOONSHINE_MODEL_ARCH_TINY_STREAMING;
    std::string device;
    int tailMs = 200;
    float duckDb = 12.0f;
    int threads = 1;
    bool wordTimestamps = true;
    int intervalMs = 150;
    std::string keyterms;
    void (*log)(int, char const*) = nullptr;
};

Config gConfig;
std::atomic<bool> gRunning{false};
std::atomic<voice::Status> gStatus{voice::Status::Off};
std::atomic<std::int32_t> gTranscriber{-1};
std::int32_t gStream = -1;
std::atomic<int> gWanted{-1};            // MevrVoiceListen's request for the worker: 1 on, 0 off, -1 none
std::atomic<std::uint64_t> gWantedAtMs{0};
std::mutex gMoonshine;  // every moonshine_* call on the transcriber (the capture thread adds audio under it too)

// The listen: 0 idle, 1 listening, 2 tail (the gesture ended, audio still kept), 3 draining (stopped, the final transcript
// due). The worker returns 3 -> 0 after publishing the final transcript and end marker.
std::atomic<int> gPhase{0};
std::atomic<std::uint64_t> gTailUntilMs{0};
std::atomic<std::uint64_t> gListenStartMs{0};
std::atomic<bool> gFromFile{false};
std::atomic<std::uint32_t> gListens{0};
std::atomic<std::uint32_t> gFinalLines{0};
std::atomic<std::uint32_t> gTranscribeMs{0};
unsigned gPartials = 0;  // the worker's own: this listen's partial calls, their costliest and their sum (ms)
std::uint32_t gPartialMaxMs = 0, gPartialSumMs = 0;
std::atomic<std::uint32_t> gLoadMs{0};
std::atomic<float> gLevel{0.0f};
std::atomic<bool> gKeyHeldUnused{false};
std::mutex gText;
std::string gDeviceName;
std::string gModelName;
std::string gError;

std::mutex gLinesMutex;
std::deque<voice::Line> gLines;  // updates for the host, oldest first
std::vector<std::string> gLastTexts;  // the last text per line index, to flag text changes

std::thread gLoader, gCapture, gWorker, gFeeder;
HANDLE gWake = nullptr;  // the worker's timer/event

void Log(int level, std::string const& message) {
    if (gConfig.log != nullptr) gConfig.log(level, message.c_str());
}

std::string Narrow(std::wstring const& w) {
    if (w.empty()) return {};
    int const n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(n - 1, 0)), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---- Ducking ---------------------------------------------------------------------------------------------------------

struct Duck {
    ISimpleAudioVolume* volume = nullptr;
    float before = -1.0f;
} gDuck;

// This process's session on the default render device.
ISimpleAudioVolume* FindOurSession() {
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) return nullptr;
    IMMDevice* device = nullptr;
    ISimpleAudioVolume* found = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        IAudioSessionManager2* manager = nullptr;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&manager)))) {
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
                int count = 0;
                sessions->GetCount(&count);
                for (int i = 0; i < count && found == nullptr; ++i) {
                    IAudioSessionControl* control = nullptr;
                    if (FAILED(sessions->GetSession(i, &control))) continue;
                    IAudioSessionControl2* control2 = nullptr;
                    if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&control2)))) {
                        DWORD pid = 0;
                        if (SUCCEEDED(control2->GetProcessId(&pid)) && pid == GetCurrentProcessId()) {
                            control2->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&found));
                        }
                        control2->Release();
                    }
                    control->Release();
                }
                sessions->Release();
            }
            manager->Release();
        }
        device->Release();
    }
    enumerator->Release();
    return found;
}

void DuckOn() {
    if (gConfig.duckDb <= 0.0f || gDuck.volume != nullptr) return;
    gDuck.volume = FindOurSession();
    if (gDuck.volume == nullptr) return;
    float level = 1.0f;
    if (FAILED(gDuck.volume->GetMasterVolume(&level))) level = 1.0f;
    gDuck.before = level;
    float const factor = std::pow(10.0f, -gConfig.duckDb / 20.0f);
    gDuck.volume->SetMasterVolume(std::clamp(level * factor, 0.0f, 1.0f), nullptr);
}

void DuckOff() {
    if (gDuck.volume == nullptr) return;
    if (gDuck.before >= 0.0f) gDuck.volume->SetMasterVolume(gDuck.before, nullptr);
    gDuck.volume->Release();
    gDuck.volume = nullptr;
    gDuck.before = -1.0f;
}

// ---- The lines -------------------------------------------------------------------------------------------------------

void Copy(char* out, std::size_t cap, char const* text) {
    if (text == nullptr) {
        out[0] = '\0';
        return;
    }
    std::size_t n = std::strlen(text);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;  // don't cut a UTF-8 sequence
    }
    std::memcpy(out, text, n);
    out[n] = '\0';
}

// A transcript's lines into the queue: every new, updated or (on the final call) every line, complete flags as given.
void Publish(transcript_t const* transcript, bool finalCall) {
    if (transcript == nullptr) return;
    std::lock_guard<std::mutex> lock(gLinesMutex);
    if (gLastTexts.size() < transcript->line_count) gLastTexts.resize(transcript->line_count);
    for (std::uint64_t i = 0; i < transcript->line_count; ++i) {
        transcript_line_t const& l = transcript->lines[i];
        std::string const text = l.text != nullptr ? l.text : "";
        bool const changed = text != gLastTexts[i];
        if (!finalCall && !changed && !l.is_new && !l.is_updated) continue;
        gLastTexts[i] = text;
        voice::Line line;
        line.id = l.id != 0 ? l.id : i + 1;
        Copy(line.text, sizeof line.text, l.text);
        line.startS = l.start_time;
        line.durationS = l.duration;
        line.complete = finalCall || l.is_complete != 0 ? 1 : 0;
        line.updated = changed ? 1 : 0;
        line.fromFile = gFromFile.load() ? 1 : 0;
        line.latencyMs = l.last_transcription_latency_ms;
        line.listenStartMs = gListenStartMs.load();
        line.wordCount = 0;
        if (l.words != nullptr) {
            for (std::uint64_t w = 0; w < l.word_count && line.wordCount < voice::kMaxWords; ++w) {
                voice::Word& word = line.words[line.wordCount++];
                Copy(word.text, sizeof word.text, l.words[w].text);
                word.startS = l.words[w].start;
                word.endS = l.words[w].end;
                word.confidence = l.words[w].confidence;
            }
        }
        if (line.complete) ++gFinalLines;
        gLines.push_back(line);
        if (gLines.size() > 64) gLines.pop_front();
    }
}

// ---- The stream ------------------------------------------------------------------------------------------------------

bool EnsureStream() {
    std::int32_t const th = gTranscriber.load();
    if (th < 0) return false;
    if (gStream < 0) {
        gStream = moonshine_create_stream(th, 0);
        if (gStream < 0) {
            Log(1, std::format("voice: can't create a stream: {}", moonshine_error_to_string(gStream)));
            gStream = -1;
            return false;
        }
    }
    return true;
}

// Phase 0 -> 1 (under gMoonshine).
bool StartListen(bool fromFile) {
    if (!EnsureStream()) return false;
    std::int32_t const err = moonshine_start_stream(gTranscriber.load(), gStream);
    if (err != MOONSHINE_ERROR_NONE) {
        Log(1, std::format("voice: can't start the stream: {}", moonshine_error_to_string(err)));
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(gLinesMutex);
        gLastTexts.clear();
    }
    gFromFile.store(fromFile);
    gListenStartMs.store(GetTickCount64());
    gPhase.store(1);
    ++gListens;
    if (!fromFile) DuckOn();
    SetEvent(gWake);
    return true;
}

// Phase 2 -> 3 (under gMoonshine): the stream stopped; the worker drains it.
void StopStream() {
    if (gStream >= 0) moonshine_stop_stream(gTranscriber.load(), gStream);
    gPhase.store(3);
    DuckOff();
    SetEvent(gWake);
}

// ---- The worker: partial transcripts while listening, the final one after the stop ---------------------------------------

int ApplyListen(int on, std::uint64_t askedMs);  // below, with MevrVoiceListen

void WorkerMain() {
    // Latest desired request wins. 0 idle -> 1 listen -> 2 tail -> 3 drain -> 0; re-squeeze in 2 resumes 1.
    // Phase 3 is runnable without a wake. Retain a pending start through 3 (coalesced auto-reset signals are harmless),
    // drain the old transcript first, then retry until StartListen succeeds. New off requests cancel a pending start.
    int pending = -1;
    std::uint64_t pendingAtMs = 0;
    bool pendingLogged = false;  // one refusal line per pending request, not one per retry
    while (gRunning.load()) {
        int const phase = gPhase.load();
        DWORD const wait = phase == 1 || phase == 2 || pending >= 0 ? static_cast<DWORD>(std::max(gConfig.intervalMs, 30)) : INFINITE;
        if (phase != 3 && gWanted.load() < 0) WaitForSingleObject(gWake, wait);
        if (!gRunning.load()) break;
        if (int const wanted = gWanted.exchange(-1); wanted >= 0) {
            pending = wanted;
            pendingAtMs = gWantedAtMs.load();
            pendingLogged = false;
        }
        if (pending >= 0) {
            std::lock_guard<std::mutex> lock(gMoonshine);
            if (gPhase.load() != 3) {
                int const err = ApplyListen(pending, pendingAtMs);
                if (err == 0) { pending = -1; continue; }
                if (!pendingLogged) {
                    pendingLogged = true;
                    Log(1, std::format("voice: listen {} refused ({}); pending", pending != 0 ? "on" : "off", err));
                }
            }
        }
        int now = gPhase.load();
        // The tail also ends here, not only in the capture loop: with no microphone, or one unplugged mid-listen, no
        // packet ever arrived to end it, and a released listen stayed in its tail (and the game ducked) for good.
        if (now == 2 && GetTickCount64() >= gTailUntilMs.load()) {
            std::lock_guard<std::mutex> lock(gMoonshine);
            if (gPhase.load() == 2) StopStream();
            now = gPhase.load();
        }
        if (now != 1 && now != 2 && now != 3) continue;
        bool const finalCall = now == 3;
        transcript_t* transcript = nullptr;
        auto const t0 = std::chrono::steady_clock::now();
        std::int32_t err = MOONSHINE_ERROR_NONE;
        {
            std::lock_guard<std::mutex> lock(gMoonshine);
            if (gStream < 0) continue;
            err = moonshine_transcribe_stream(gTranscriber.load(), gStream, finalCall ? MOONSHINE_FLAG_FORCE_UPDATE : 0, &transcript);
            if (err == MOONSHINE_ERROR_NONE) Publish(transcript, finalCall);
            if (finalCall) {
                // The end marker: the mod joins the listen's lines (the library's VAD splits "Garrus." from the order) and acts.
                std::lock_guard<std::mutex> lines(gLinesMutex);
                voice::Line end;
                end.complete = 1;
                end.endOfListen = 1;
                end.fromFile = gFromFile.load() ? 1 : 0;
                end.listenStartMs = gListenStartMs.load();
                gLines.push_back(end);
            }
        }
        std::uint32_t const ms = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        gTranscribeMs.store(ms);
        if (finalCall) {
            Log(0, std::format("voice: the listen's final transcript took {} ms after the stop; {} partials, the costliest {} ms, {} ms together", ms, gPartials, gPartialMaxMs,
                gPartialSumMs));
            gPartials = 0;
            gPartialMaxMs = 0;
            gPartialSumMs = 0;
        } else {
            ++gPartials;
            gPartialMaxMs = std::max(gPartialMaxMs, ms);
            gPartialSumMs += ms;
        }
        if (err != MOONSHINE_ERROR_NONE && err != MOONSHINE_ERROR_BUSY) {
            Log(1, std::format("voice: transcribe_stream failed: {}", moonshine_error_to_string(err)));
        }
        if (finalCall) {
            gPhase.store(0);
            if (pending >= 0) SetEvent(gWake); // run the deferred request immediately after the drain
        }
    }
}

// ---- Capture ---------------------------------------------------------------------------------------------------------

// The capture endpoint: the one whose friendly name holds config.device (case-insensitive), else the default.
IMMDevice* PickDevice(IMMDeviceEnumerator* enumerator, std::string& nameOut) {
    IMMDevice* chosen = nullptr;
    std::string const want = Lower(gConfig.device);
    if (!want.empty()) {
        IMMDeviceCollection* devices = nullptr;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices))) {
            UINT count = 0;
            devices->GetCount(&count);
            for (UINT i = 0; i < count && chosen == nullptr; ++i) {
                IMMDevice* device = nullptr;
                if (FAILED(devices->Item(i, &device))) continue;
                IPropertyStore* props = nullptr;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
                    PROPVARIANT name;
                    PropVariantInit(&name);
                    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
                        std::string const friendly = Narrow(name.pwszVal);
                        if (Lower(friendly).find(want) != std::string::npos) {
                            chosen = device;
                            nameOut = friendly;
                        }
                    }
                    PropVariantClear(&name);
                    props->Release();
                }
                if (chosen != device) device->Release();
            }
            devices->Release();
        }
        if (chosen == nullptr) Log(1, std::format("voice: no capture device named like \"{}\": the default device is used", gConfig.device));
    }
    if (chosen == nullptr && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &chosen))) {
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(chosen->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) nameOut = Narrow(name.pwszVal);
            PropVariantClear(&name);
            props->Release();
        }
    }
    return chosen;
}

// Interleaved device samples to mono float.
void ToMono(BYTE const* data, UINT32 frames, WAVEFORMATEX const* format, bool isFloat, std::vector<float>& out) {
    out.resize(frames);
    int const channels = std::max<int>(format->nChannels, 1);
    int const bytes = format->wBitsPerSample / 8;
    for (UINT32 f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c) {
            BYTE const* p = data + (static_cast<std::size_t>(f) * channels + c) * bytes;
            double v = 0.0;
            if (isFloat && bytes == 4) {
                float s = 0.0f;
                std::memcpy(&s, p, 4);
                v = s;
            } else if (bytes == 2) {
                std::int16_t s = 0;
                std::memcpy(&s, p, 2);
                v = s / 32768.0;
            } else if (bytes == 4) {
                std::int32_t s = 0;
                std::memcpy(&s, p, 4);
                v = s / 2147483648.0;
            } else if (bytes == 3) {
                std::int32_t s = (p[0] << 8) | (p[1] << 16) | (static_cast<std::int32_t>(p[2]) << 24);
                v = (s >> 8) / 8388608.0;
            }
            sum += v;
        }
        out[f] = static_cast<float>(std::clamp(sum / channels, -1.0, 1.0));
    }
}

void CaptureMain() {
    HRESULT const init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE const task = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::string name;
    bool ok = false;
    do {
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
            Log(1, "voice: no audio device enumerator");
            break;
        }
        device = PickDevice(enumerator, name);
        if (device == nullptr) {
            Log(1, "voice: no capture device: is a microphone connected?");
            break;
        }
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client)))) {
            Log(1, "voice: the capture device can't be activated");
            break;
        }
        if (FAILED(client->GetMixFormat(&format)) || format == nullptr) {
            Log(1, "voice: the capture device has no mix format");
            break;
        }
        REFERENCE_TIME const period = 200000;  // 20 ms buffers
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, 0, format, nullptr);
        if (FAILED(hr)) {
            Log(1, std::format("voice: the capture client won't initialize (0x{:08X})", static_cast<unsigned>(hr)));
            break;
        }
        if (FAILED(client->SetEventHandle(event)) || FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture)))) {
            Log(1, "voice: no capture service");
            break;
        }
        if (FAILED(client->Start())) {
            Log(1, "voice: the capture won't start");
            break;
        }
        ok = true;
    } while (false);
    bool isFloat = false;
    if (ok) {
        isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE const*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        {
            std::lock_guard<std::mutex> lock(gText);
            gDeviceName = name;
        }
        Log(0, std::format("voice: capturing \"{}\" at {} Hz, {} channels, {}-bit {}", name, format->nSamplesPerSec, format->nChannels, format->wBitsPerSample,
            isFloat ? "float" : "PCM"));
    } else {
        std::lock_guard<std::mutex> lock(gText);
        if (gError.empty()) gError = "no microphone";
    }
    std::vector<float> mono;
    while (ok && gRunning.load()) {
        if (WaitForSingleObject(event, 200) != WAIT_OBJECT_0) continue;
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                mono.assign(frames, 0.0f);
            } else {
                ToMono(data, frames, format, isFloat, mono);
            }
            capture->ReleaseBuffer(frames);
            double energy = 0.0;
            for (float const s : mono) energy += static_cast<double>(s) * s;
            gLevel.store(mono.empty() ? 0.0f : static_cast<float>(std::sqrt(energy / static_cast<double>(mono.size()))));
            int const phase = gPhase.load();
            if ((phase == 1 || phase == 2) && !gFromFile.load()) {
                // Added without gMoonshine: the library's calls are thread-safe, and the worker's transcribe (100 ms and more)
                // must not stall the capture (the first build held the lock here: a file fed in real time arrived 1-2 s late).
                std::int32_t const stream = gStream;
                if (stream >= 0) {
                    moonshine_transcribe_add_audio_to_stream(gTranscriber.load(), stream, mono.data(), mono.size(), static_cast<std::int32_t>(format->nSamplesPerSec), 0);
                }
                if (gPhase.load() == 2 && GetTickCount64() >= gTailUntilMs.load()) {
                    std::lock_guard<std::mutex> lock(gMoonshine);
                    if (gPhase.load() == 2) StopStream();
                }
            }
        }
    }
    if (client != nullptr) client->Stop();
    if (capture != nullptr) capture->Release();
    if (format != nullptr) CoTaskMemFree(format);
    if (client != nullptr) client->Release();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (event != nullptr) CloseHandle(event);
    if (task != nullptr) AvRevertMmThreadCharacteristics(task);
    if (SUCCEEDED(init)) CoUninitialize();
}

// ---- Loading ---------------------------------------------------------------------------------------------------------

void LoaderMain() {
    auto const t0 = std::chrono::steady_clock::now();
    // Moonshine passes no thread count to ONNX Runtime, whose default is a pool of every core that spins between ops:
    // a partial transcript then took every core for 200-300 ms (the game lost 2 s of frames per listen, 2026-11-06).
    // Its own switch (ort-utils' ort_maybe_force_single_thread, read with the CRT's getenv, which our static link shares)
    // puts the sessions on one thread, the worker's own. Set before any session is made, both copies of the environment.
    bool const single = gConfig.threads <= 1;
    _putenv_s("MOONSHINE_ORT_SINGLE_THREAD", single ? "1" : "");
    SetEnvironmentVariableA("MOONSHINE_ORT_SINGLE_THREAD", single ? "1" : nullptr);
    Log(0, single ? "voice: the recognizer runs on one thread (MOONSHINE_ORT_SINGLE_THREAD)" : "voice: the recognizer runs on ONNX Runtime's default threads (every core)");
    std::vector<moonshine_option_t> options;
    std::string const timestamps = gConfig.wordTimestamps ? "true" : "false";
    options.push_back({"word_timestamps", timestamps.c_str()});
    if (!gConfig.keyterms.empty()) options.push_back({"keyterms", gConfig.keyterms.c_str()});
    std::int32_t const handle = moonshine_load_transcriber_from_files(gConfig.modelDir.c_str(), static_cast<std::uint32_t>(gConfig.arch), options.data(), options.size(),
        MOONSHINE_HEADER_VERSION);
    std::uint32_t const ms = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    gLoadMs.store(ms);
    if (handle < 0) {
        std::lock_guard<std::mutex> lock(gText);
        gError = std::format("the models didn't load from {}: {}", gConfig.modelDir, moonshine_error_to_string(handle));
        gStatus.store(voice::Status::Failed);
        Log(1, "voice: " + gError);
        return;
    }
    gTranscriber.store(handle);
    gStatus.store(voice::Status::Ready);
    Log(0, std::format("voice: the {} models loaded from {} in {} ms (library version {}){}", gModelName, gConfig.modelDir, ms, moonshine_get_version(),
        gConfig.keyterms.empty() ? "" : "; key terms: " + gConfig.keyterms));
}

char const* ArchName(int arch) {
    switch (arch) {
    case MOONSHINE_MODEL_ARCH_TINY_STREAMING: return "tiny streaming";
    case MOONSHINE_MODEL_ARCH_BASE_STREAMING: return "base streaming";
    case MOONSHINE_MODEL_ARCH_SMALL_STREAMING: return "small streaming";
    case MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING: return "medium streaming";
    case MOONSHINE_MODEL_ARCH_TINY: return "tiny";
    case MOONSHINE_MODEL_ARCH_BASE: return "base";
    default: return "?";
    }
}

}  // namespace

extern "C" {

int MevrVoiceApiVersion() {
    return MEVR_VOICE_API_VERSION;
}

int MevrVoiceStartup(MevrVoiceConfig const* config) {
    if (config == nullptr || config->version != MEVR_VOICE_API_VERSION) return -3;
    if (gRunning.load()) return 0;
    gConfig = {};
    gConfig.modelDir = config->modelDir != nullptr ? config->modelDir : "";
    gConfig.arch = config->arch;
    gConfig.device = config->device != nullptr ? config->device : "";
    gConfig.tailMs = std::clamp(config->tailMs, 0, 2000);
    gConfig.duckDb = config->duckDb;
    gConfig.threads = config->threads;
    gConfig.wordTimestamps = config->wordTimestamps != 0;
    gConfig.intervalMs = std::clamp(config->intervalMs, 30, 1000);
    gConfig.keyterms = config->keyterms != nullptr ? config->keyterms : "";
    gConfig.log = config->log;
    {
        std::lock_guard<std::mutex> lock(gText);  // a restart (the menu's new microphone or model): nothing of the last run shown
        gModelName = ArchName(gConfig.arch);
        gDeviceName.clear();
        gError.clear();
    }
    gLevel.store(0.0f);
    gWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gRunning.store(true);
    gStatus.store(voice::Status::Loading);
    gLoader = std::thread(LoaderMain);
    gCapture = std::thread(CaptureMain);
    gWorker = std::thread(WorkerMain);
    return 0;
}

void MevrVoiceShutdown() {
    if (!gRunning.load()) return;
    gRunning.store(false);
    if (gWake != nullptr) SetEvent(gWake);
    if (gLoader.joinable()) gLoader.join();
    if (gCapture.joinable()) gCapture.join();
    if (gWorker.joinable()) gWorker.join();
    if (gFeeder.joinable()) gFeeder.join();
    {
        std::lock_guard<std::mutex> lock(gMoonshine);
        DuckOff();
        std::int32_t const th = gTranscriber.exchange(-1);
        if (th >= 0) {
            if (gStream >= 0) moonshine_free_stream(th, gStream);
            moonshine_free_transcriber(th);
        }
        gStream = -1;
    }
    if (gWake != nullptr) CloseHandle(gWake);
    gWake = nullptr;
    gPhase.store(0);
    gWanted.store(-1);
    gStatus.store(voice::Status::Off);
    std::lock_guard<std::mutex> lock(gLinesMutex);
    gLines.clear();
}

// The listen request is applied by the worker (ApplyListen below): the caller is the host's render thread, and the
// lock it took here waited out a partial transcript in flight (up to 300 ms a release, 2026-11-06).
int MevrVoiceListen(int on) {
    if (gStatus.load() != voice::Status::Ready) return -1;
    gWantedAtMs.store(GetTickCount64());
    gWanted.store(on != 0 ? 1 : 0);
    if (gWake != nullptr) SetEvent(gWake);
    return 0;
}

}  // extern "C"

namespace {

// Under gMoonshine, on the worker.
int ApplyListen(int on, std::uint64_t askedMs) {
    int const phase = gPhase.load();
    if (on != 0) {
        if (phase == 1) return 0;
        if (phase == 2) {  // squeezed again inside the tail: keep listening
            gPhase.store(1);
            return 0;
        }
        if (phase != 0) return MOONSHINE_ERROR_BUSY;  // the last final is still on its way
        return StartListen(false) ? 0 : -1;
    }
    if (phase != 1) return 0;
    if (gConfig.tailMs <= 0 || gFromFile.load()) {
        StopStream();
    } else {
        gTailUntilMs.store(askedMs + static_cast<std::uint64_t>(gConfig.tailMs));  // from the release, not from this call
        gPhase.store(2);
    }
    return 0;
}

}  // namespace

extern "C" {

int MevrVoiceTake(voice::Line* out, std::uint32_t size) {
    if (out == nullptr || size < sizeof(voice::Line)) return 0;
    std::lock_guard<std::mutex> lock(gLinesMutex);
    if (gLines.empty()) return 0;
    *out = gLines.front();
    gLines.pop_front();
    return 1;
}

void MevrVoiceState(voice::State* out, std::uint32_t size) {
    if (out == nullptr || size < sizeof(voice::State)) return;
    voice::State s;
    s.status = static_cast<std::uint8_t>(gStatus.load());
    int const phase = gPhase.load();
    s.listening = phase == 1 || phase == 2 ? 1 : 0;
    s.draining = phase == 3 ? 1 : 0;
    s.level = gLevel.load();
    s.transcribeMs = gTranscribeMs.load();
    s.loadMs = gLoadMs.load();
    s.listenStartMs = gListenStartMs.load();
    s.listens = gListens.load();
    s.lines = gFinalLines.load();
    {
        std::lock_guard<std::mutex> lock(gText);
        Copy(s.device, sizeof s.device, gDeviceName.c_str());
        Copy(s.model, sizeof s.model, gModelName.c_str());
        Copy(s.error, sizeof s.error, gError.c_str());
    }
    *out = s;
}

int MevrVoiceFeedFile(wchar_t const* path) {
    if (path == nullptr || gStatus.load() != voice::Status::Ready) return -1;
    std::string why;
    std::optional<mevr::voice::wav::Audio> audio = mevr::voice::wav::Read(path, &why);
    if (!audio) {
        Log(1, std::format("voice: can't read {}: {}", Narrow(path), why));
        return -3;
    }
    {
        std::lock_guard<std::mutex> lock(gMoonshine);
        if (gPhase.load() != 0) return MOONSHINE_ERROR_BUSY;
        if (!StartListen(true)) return -1;
    }
    if (gFeeder.joinable()) gFeeder.join();
    Log(0, std::format("voice: feeding {} ({:.2f} s at {} Hz) as one listen", Narrow(path), audio->Seconds(), audio->sampleRate));
    // Fed in real time, 100 ms at a time, so the partials come as they would from a microphone; then the stop and the tail.
    gFeeder = std::thread([a = std::move(*audio)]() {
        std::size_t const chunk = static_cast<std::size_t>(a.sampleRate / 10);
        for (std::size_t at = 0; at < a.mono.size() && gRunning.load(); at += chunk) {
            std::size_t const n = std::min(chunk, a.mono.size() - at);
            std::int32_t const stream = gStream;  // no lock (see the capture thread)
            if (stream >= 0 && gPhase.load() == 1) {
                moonshine_transcribe_add_audio_to_stream(gTranscriber.load(), stream, a.mono.data() + at, n, a.sampleRate, 0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::lock_guard<std::mutex> lock(gMoonshine);
        if (gPhase.load() == 1) StopStream();
    });
    return 0;
}

int MevrVoiceDevices(char* names, std::uint32_t size, int* defaultIndex) {
    if (defaultIndex != nullptr) *defaultIndex = -1;
    if (names == nullptr || size == 0) return -1;
    names[0] = '\0';
    HRESULT const init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // RPC_E_CHANGED_MODE: the thread's own apartment works too
    int count = -2;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        std::wstring defaultId;
        IMMDevice* fallback = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &fallback))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(fallback->GetId(&id)) && id != nullptr) {
                defaultId = id;
                CoTaskMemFree(id);
            }
            fallback->Release();
        }
        IMMDeviceCollection* devices = nullptr;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices))) {
            count = 0;
            std::size_t used = 0;
            UINT n = 0;
            devices->GetCount(&n);
            for (UINT i = 0; i < n; ++i) {
                IMMDevice* device = nullptr;
                if (FAILED(devices->Item(i, &device))) continue;
                std::string name;
                IPropertyStore* props = nullptr;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
                    PROPVARIANT value;
                    PropVariantInit(&value);
                    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) name = Narrow(value.pwszVal);
                    PropVariantClear(&value);
                    props->Release();
                }
                bool isDefault = false;
                LPWSTR id = nullptr;
                if (SUCCEEDED(device->GetId(&id)) && id != nullptr) {
                    isDefault = !defaultId.empty() && defaultId == id;
                    CoTaskMemFree(id);
                }
                device->Release();
                name.erase(std::remove(name.begin(), name.end(), '\n'), name.end());
                if (name.empty() || used + name.size() + 1 >= size) continue;
                std::memcpy(names + used, name.data(), name.size());
                used += name.size();
                names[used++] = '\n';
                names[used] = '\0';
                if (isDefault && defaultIndex != nullptr) *defaultIndex = count;
                ++count;
            }
            devices->Release();
        }
        enumerator->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return count;
}

int MevrVoiceSetKeyterms(char const* commaSeparated) {
    std::int32_t const th = gTranscriber.load();
    if (th < 0) return -1;
    std::lock_guard<std::mutex> lock(gMoonshine);
    gConfig.keyterms = commaSeparated != nullptr ? commaSeparated : "";
    return moonshine_transcriber_set_keyterms(th, gConfig.keyterms.c_str());
}

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        // The host calls MevrVoiceShutdown first; nothing is joined here (the loader lock).
    }
    return TRUE;
}
