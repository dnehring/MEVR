// The headset test loop: eye snapshots, pose recording and replay (plugin/TestLoop.hpp). Host side,
// LE1 with Halcyon's renderer. Its one seam in Halcyon's code is MevrBridge::WrapSessionFunctions
// (src/halcyon/le1/mevr_bridge.h), called from xr_session.cpp's ResolveSessionFunctions.

#include "plugin/TestLoop.hpp"

#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_menu.h"

#include "core/Hotkey.hpp"
#include "core/Log.hpp"
#include "core/PngFile.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/ImageConvert.hpp"
#include "core/vr/LayerComposite.hpp"
#include "core/vr/PoseRecording.hpp"
#include "core/vr/TestRequest.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <objbase.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mevr::host::testloop {
namespace {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace rec = vr::recording;
namespace req = vr::test_request;

// ---- Paths, time, text ------------------------------------------------------------------------------

fs::path BaseDir() {
    static fs::path const dir = [] {
        wchar_t buffer[MAX_PATH]{};
        DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        std::error_code ec;
        fs::path const base = (n > 0 && n < MAX_PATH) ? fs::path(buffer) : fs::temp_directory_path(ec);
        return base / L"MEVR";
    }();
    return dir;
}
fs::path SnapshotDir() { return BaseDir() / L"snapshots"; }
fs::path RecordingDir() { return BaseDir() / L"recordings"; }

std::string Utf8(fs::path const& path) {
    std::u8string const u = path.u8string();
    return std::string(u.begin(), u.end());
}

std::wstring Widen(std::string const& text) {
    if (text.empty()) {
        return {};
    }
    int const n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(n, 0)), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    }
    return out;
}

std::string Narrow(std::wstring const& text) {
    if (text.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    }
    return out;
}

std::int64_t UnixMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER t{};
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>((t.QuadPart - 116444736000000000ull) / 10000ull);
}

// Local time, sortable and safe in file names: 2026-10-21_14-03-07-123.
std::string Stamp() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}-{:03}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
        t.wSecond, t.wMilliseconds);
}

std::string SafeTag(std::string const& tag) {
    std::string out;
    for (char const c : tag) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') {
            out += c;
        }
        if (out.size() >= 32) {
            break;
        }
    }
    return out;
}

std::string J(double value) { return req::JsonNumber(value); }
std::string J(bool value) { return value ? "true" : "false"; }
std::string JVec(XrVector3f const& v) { return std::format("[{}, {}, {}]", J(v.x), J(v.y), J(v.z)); }
std::string JQuat(XrQuaternionf const& q) { return std::format("[{}, {}, {}, {}]", J(q.x), J(q.y), J(q.z), J(q.w)); }
std::string JPose(XrPosef const& p) {
    return std::format("{{\"orientation\": {}, \"position\": {}}}", JQuat(p.orientation), JVec(p.position));
}
std::string JFov(XrFovf const& f) {
    constexpr double kDeg = 57.29577951308232;
    return std::format("{{\"leftDeg\": {}, \"rightDeg\": {}, \"upDeg\": {}, \"downDeg\": {}}}", J(f.angleLeft * kDeg),
        J(f.angleRight * kDeg), J(f.angleUp * kDeg), J(f.angleDown * kDeg));
}
std::string JHand(vr::controllers::Hand const& h) {
    return std::format("{{\"gripValid\": {}, \"grip\": {}, \"aimValid\": {}, \"aim\": {}, \"trigger\": {}, \"squeeze\": {}, "
                       "\"stick\": [{}, {}], \"stickClick\": {}, \"primary\": {}, \"secondary\": {}, \"menu\": {}}}",
        J(h.gripValid), JPose(h.grip), J(h.aimValid), JPose(h.aim), J(h.trigger), J(h.squeeze), J(h.stickX), J(h.stickY),
        J(h.stickClick), J(h.primary), J(h.secondary), J(h.menu));
}

// Writes `text` next to `path` and moves it into place, so a reader never sees half a file.
bool WriteFileAtomically(fs::path const& path, std::string const& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path const temp = fs::path(path).concat(L".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return false;
        }
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// ---- Swapchains seen this session -------------------------------------------------------------------

struct SwapchainInfo {
    std::vector<ID3D11Texture2D*> images;  // owned by the runtime
    bool enumerated = false;
    std::uint32_t acquired = UINT32_MAX;
    std::string name;
    bool isStatic = false;
    std::uint32_t staticWidth = 0;
    std::uint32_t staticHeight = 0;
    std::vector<std::uint32_t> staticPixels;
};
std::mutex gSwapchainMutex;
std::unordered_map<XrSwapchain, SwapchainInfo> gSwapchains;

// ---- The wrapped session functions (Halcyon's g_fn) -------------------------------------------------

PFN_xrAcquireSwapchainImage gRealAcquire = nullptr;
PFN_xrReleaseSwapchainImage gRealRelease = nullptr;
PFN_xrEndFrame gRealEndFrame = nullptr;
PFN_xrLocateViews gRealLocateViews = nullptr;

// ---- Control (any thread, under gControlMutex) ------------------------------------------------------

struct Replay {
    std::shared_ptr<rec::Recording const> recording;
    fs::path path;
    bool loop = false;
    bool inputs = false;
    bool started = false;
    XrTime start = 0;
};

std::mutex gControlMutex;
int gSnapshotsPending = 0;  // snapshots still to take
double gSnapshotInterval = 0.0;
std::uint64_t gSnapshotDueMs = 0;
std::string gSnapshotTag;
std::string gSnapshotTrigger = "key";
int gRecordCommand = 2;  // -1 toggle, 0 stop, 1 start, 2 none
double gRecordSeconds = 0.0;
std::shared_ptr<Replay> gReplayPending;
bool gReplayStopRequested = false;
std::string gLastRecording;
std::string gLastSnapshot;
std::string gReplayPathText;
std::string gModStatus;
std::uint64_t gModStatusMs = 0;

std::atomic<int> gPendingCount{0};  // mirrors gSnapshotsPending, plus a capture in flight
std::atomic<bool> gRecordingActive{false};
std::atomic<std::uint64_t> gRecordStartMs{0};
std::atomic<bool> gReplayActive{false};
std::atomic<bool> gReplayInputs{false};
std::atomic<bool> gReplayLoop{false};
std::atomic<std::int64_t> gReplayPositionNs{0};
std::atomic<std::int64_t> gReplayDurationNs{0};
std::atomic<bool> gGripChord{false};
std::atomic<std::uint64_t> gLastFrameMs{0};
std::atomic<std::uint64_t> gWantStatusUntilMs{0};
std::atomic<DWORD> gPresentThread{0};

// ---- Present thread state ---------------------------------------------------------------------------

XrSession gSession = XR_NULL_HANDLE;
XrSpace gAppSpace = XR_NULL_HANDLE;
XrTime gFrameTime = 0;
std::uint64_t gFrame = 0;
bool gFocused = false;
vr::controllers::State gFrameState;  // as published this frame
bool gHeadValid = false;
XrView gHead[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};  // located this frame in the app space

struct MenuQuadNote {
    std::uint64_t frame = UINT64_MAX;
    XrVector3f position{};   // as submitted, in its space
    bool headLocked = true;  // submitted in VIEW space (else in the app space: world-locked panels)
    float width = 0.0f;
    float height = 0.0f;
};
MenuQuadNote gMenuQuad;

struct Recorder {
    rec::Writer writer;
    fs::path path;
    XrTime start = 0;
    bool started = false;
    bool pending = false;
    rec::Frame frame;
    double maxSeconds = 0.0;
};
// Never destroyed: at process exit a thread killed inside a write could hold the stream (frames are
// flushed every 90 instead, so an exit loses at most about a second).
Recorder& gRecorder = *new Recorder;
std::shared_ptr<Replay> gReplay;

bool OnPresentThread() {
    return GetCurrentThreadId() == gPresentThread.load(std::memory_order_relaxed);
}

// ---- Snapshot jobs (worker thread) ------------------------------------------------------------------

struct LayerShot {
    bool projection = false;
    std::string name;
    std::string file;  // planned PNG (empty: not written on its own)
    int eye = -1;      // projection views: 0 left, 1 right
    XrSwapchain swapchain = XR_NULL_HANDLE;
    XrRect2Di rect{};
    std::uint32_t arrayIndex = 0;
    XrFlags64 flags = 0;
    XrSpace space = XR_NULL_HANDLE;
    XrPosef pose{};       // as submitted, in `space`
    XrFovf fov{};
    XrExtent2Df size{};
    XrEyeVisibility visibility = XR_EYE_VISIBILITY_BOTH;
    bool located = false;
    XrPosef poseInReference{};  // quads: in the eyes' space
};

struct Source {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int format = 0;
    int width = 0;
    int height = 0;
    std::uint8_t const* data = nullptr;  // mapped staging memory (until `converted`)
    size_t rowPitch = 0;
    std::vector<std::uint32_t> pixels;  // static images (premultiplied RGBA8 sRGB)
};

struct Job {
    std::string stamp;
    std::vector<LayerShot> layers;
    std::vector<Source> sources;
    bool eyesValid = false;
    rec::View eyes[2];
    std::string meta;  // JSON members
    std::atomic<bool> converted{false};
};

class Worker {
public:
    void Push(std::shared_ptr<Job> job) {
        {
            std::lock_guard lock(mutex_);
            jobs_.push_back(std::move(job));
            if (!started_) {
                started_ = true;
                std::thread([this] { Run(); }).detach();  // never joined: it may be cut off at exit
            }
        }
        cv_.notify_one();
    }

private:
    void Run() {
        // WIC needs COM on this thread; never uninitialized (the thread runs until the process ends).
        (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return !jobs_.empty(); });
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            Process(*job);
        }
    }
    static void Process(Job& job);

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Job>> jobs_;
    bool started_ = false;
};

Worker& TheWorker() {
    static Worker* const worker = new Worker;  // never destroyed: its thread may outlive static destruction
    return *worker;
}

Source const* FindSource(Job const& job, XrSwapchain swapchain) {
    for (Source const& s : job.sources) {
        if (s.swapchain == swapchain) {
            return &s;
        }
    }
    return nullptr;
}

bool ConvertLayer(Job const& job, LayerShot const& layer, vr::composite::Image& out) {
    Source const* const source = FindSource(job, layer.swapchain);
    if (source == nullptr || source->width <= 0 || source->height <= 0) {
        return false;
    }
    int const x0 = std::clamp(layer.rect.offset.x, 0, source->width - 1);
    int const y0 = std::clamp(layer.rect.offset.y, 0, source->height - 1);
    int const w = std::clamp(layer.rect.extent.width > 0 ? layer.rect.extent.width : source->width, 1, source->width - x0);
    int const h = std::clamp(layer.rect.extent.height > 0 ? layer.rect.extent.height : source->height, 1, source->height - y0);
    bool const blend = (layer.flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
    vr::image::Options options;
    options.opaque = !blend;
    options.premultiplied = blend && (layer.flags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) == 0;
    out.width = w;
    out.height = h;
    out.pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0u);
    if (!source->pixels.empty()) {
        for (int y = 0; y < h; ++y) {
            std::uint32_t const* row = source->pixels.data() + static_cast<size_t>(y0 + y) * static_cast<size_t>(source->width) + x0;
            if (!vr::image::ConvertRow(vr::image::format::kR8G8B8A8UnormSrgb, row, w, options,
                    out.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w))) {
                return false;
            }
        }
        return true;
    }
    if (source->data == nullptr) {
        return false;
    }
    int const bpp = vr::image::BytesPerPixel(source->format);
    if (bpp == 0) {
        return false;
    }
    options.whiteScale = 1.0f;
    for (int y = 0; y < h; ++y) {
        std::uint8_t const* row = source->data + static_cast<size_t>(y0 + y) * source->rowPitch + static_cast<size_t>(x0) * bpp;
        if (!vr::image::ConvertRow(source->format, row, w, options, out.pixels.data() + static_cast<size_t>(y) * w)) {
            return false;
        }
    }
    return true;
}

void Worker::Process(Job& job) {
    auto const begin = std::chrono::steady_clock::now();
    std::vector<vr::composite::Image> images(job.layers.size());
    std::vector<bool> converted(job.layers.size(), false);
    std::vector<std::string> errors;
    for (size_t i = 0; i < job.layers.size(); ++i) {
        converted[i] = ConvertLayer(job, job.layers[i], images[i]);
        if (!converted[i]) {
            Source const* const s = FindSource(job, job.layers[i].swapchain);
            errors.push_back(std::format("layer {} ({}): {}", i, job.layers[i].name,
                s == nullptr ? "its image wasn't released during the capture (not copied)"
                             : std::format("format {} not converted", s->format)));
        }
    }
    job.converted.store(true, std::memory_order_release);  // the Present thread may unmap now

    fs::path const dir = SnapshotDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<std::string> written;
    auto save = [&](std::string const& file, vr::composite::Image const& image) {
        std::string error;
        if (png::Write(dir / Widen(file), image.width, image.height, image.pixels.data(), &error)) {
            written.push_back(file);
        } else {
            errors.push_back(file + ": " + error);
        }
    };

    std::string eyesJson;
    static char const* const kEyeNames[2] = {"left", "right"};
    for (int e = 0; e < 2 && job.eyesValid; ++e) {
        // The eye's world: the projection layers for it, in order (the first sets the size).
        vr::composite::Image eye;
        bool haveWorld = false;
        for (size_t i = 0; i < job.layers.size(); ++i) {
            LayerShot const& layer = job.layers[i];
            if (!layer.projection || layer.eye != e || !converted[i]) {
                continue;
            }
            if (!haveWorld) {
                eye = images[i];
                haveWorld = true;
            } else if (images[i].width == eye.width && images[i].height == eye.height) {
                bool const blend = (layer.flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
                for (size_t p = 0; p < eye.pixels.size(); ++p) {
                    eye.pixels[p] = blend ? vr::image::Over(eye.pixels[p], images[i].pixels[p]) : images[i].pixels[p];
                }
            }
        }
        if (!haveWorld) {
            eye.width = 1024;  // no world layer this frame (a flat screen or a menu): black behind the quads
            eye.height = 1024;
            eye.pixels.assign(static_cast<size_t>(eye.width) * eye.height, vr::image::Pack(0, 0, 0, 255));
        }
        vr::composite::Image const world = eye;
        vr::composite::Eye const view{job.eyes[e].pose, job.eyes[e].fov};
        std::string quadsJson;
        size_t covered = 0;
        for (size_t i = 0; i < job.layers.size(); ++i) {
            LayerShot const& layer = job.layers[i];
            if (layer.projection || !converted[i] || !layer.located) {
                continue;
            }
            if (layer.visibility == (e == 0 ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT)) {
                continue;
            }
            vr::composite::Quad quad;
            quad.pose = layer.poseInReference;
            quad.width = layer.size.width;
            quad.height = layer.size.height;
            quad.image = &images[i];
            quad.blend = (layer.flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
            size_t const n = vr::composite::Draw(eye, view, quad);
            covered += n;
            quadsJson += std::format("{}{}: {}", quadsJson.empty() ? "" : ", ", req::JsonString(layer.name), n);
        }
        std::string const file = std::format("{}-{}.png", job.stamp, kEyeNames[e]);
        save(file, eye);
        std::string worldFile;
        if (covered > 0) {
            worldFile = std::format("{}-{}-world.png", job.stamp, kEyeNames[e]);
            save(worldFile, world);
        }
        eyesJson += std::format("{}{{\"eye\": {}, \"file\": {}, \"worldFile\": {}, \"size\": [{}, {}], \"hasWorld\": {}, "
                                "\"pose\": {}, \"fov\": {}, \"quadPixels\": {{{}}}}}",
            e == 0 ? "" : ", ", req::JsonString(kEyeNames[e]), req::JsonString(file),
            worldFile.empty() ? "null" : req::JsonString(worldFile), eye.width, eye.height, J(haveWorld),
            JPose(job.eyes[e].pose), JFov(job.eyes[e].fov), quadsJson);
    }
    for (size_t i = 0; i < job.layers.size(); ++i) {
        if (!job.layers[i].projection && converted[i] && !job.layers[i].file.empty()) {
            save(job.layers[i].file, images[i]);
        }
    }

    double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::string filesJson, errorsJson;
    for (std::string const& f : written) {
        filesJson += (filesJson.empty() ? "" : ", ") + req::JsonString(f);
    }
    for (std::string const& e : errors) {
        errorsJson += (errorsJson.empty() ? "" : ", ") + req::JsonString(e);
    }
    std::string const json = std::format("{{\n  \"stamp\": {},\n  \"directory\": {},\n  \"files\": [{}],\n  \"errors\": [{}],\n"
                                         "  \"eyes\": [{}],\n  \"workerMs\": {},\n{}\n}}\n",
        req::JsonString(job.stamp), req::JsonString(Utf8(dir)), filesJson, errorsJson, eyesJson, J(ms), job.meta);
    fs::path const jsonPath = dir / Widen(job.stamp + ".json");
    if (!WriteFileAtomically(jsonPath, json)) {
        log::Error("test loop: snapshot {}: writing {} failed", job.stamp, Utf8(jsonPath));
    }
    {
        std::lock_guard lock(gControlMutex);
        gLastSnapshot = Utf8(jsonPath);
    }
    std::string list;
    for (std::string const& f : written) {
        list += (list.empty() ? "" : ", ") + f;
    }
    log::Info("test loop: snapshot saved in {} ({:.0f} ms): {}, {}.json{}", Utf8(dir), ms, list.empty() ? "no images" : list,
        job.stamp, errors.empty() ? "" : std::format(" ({} problems, see the JSON)", errors.size()));
}

// ---- Snapshot capture (Present thread) --------------------------------------------------------------

enum class Phase { Idle, Armed, Staged, Mapped };

struct Staged {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc{};
    bool written = false;
    bool mapped = false;
    D3D11_MAPPED_SUBRESOURCE map{};
};

struct Capture {
    Phase phase = Phase::Idle;
    int armedFrames = 0;
    int stagedFrames = 0;
    std::vector<Staged> staged;
    ComPtr<ID3D11DeviceContext> context;
    std::shared_ptr<Job> job;
    std::string stamp;
    std::string trigger;
    std::uint64_t armedMs = 0;
};
Capture& gCapture = *new Capture;  // never destroyed: no D3D releases during process exit
std::atomic<bool> gArmed{false};
constexpr int kMaxArmedFrames = 6;  // waiting for every layer's image to be released once (alternate-eye modes)
constexpr int kMapAfterFrames = 2;
constexpr int kBlockingMapAfterFrames = 30;
bool gToldStagingFailed = false;

void StageImage(XrSwapchain swapchain, ID3D11Texture2D* image) {
    if (image == nullptr) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    image->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1) {
        return;  // multisampled: not copyable to staging (Halcyon's aren't)
    }
    Staged* entry = nullptr;
    for (Staged& s : gCapture.staged) {
        if (s.swapchain == swapchain) {
            entry = &s;
        }
    }
    if (entry == nullptr) {
        gCapture.staged.push_back({});
        entry = &gCapture.staged.back();
        entry->swapchain = swapchain;
    }
    ComPtr<ID3D11Device> device;
    image->GetDevice(&device);
    if (entry->texture == nullptr || entry->desc.Width != desc.Width || entry->desc.Height != desc.Height ||
        entry->desc.Format != desc.Format || entry->desc.ArraySize != desc.ArraySize || entry->desc.MipLevels != desc.MipLevels) {
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        entry->texture.Reset();
        if (FAILED(device->CreateTexture2D(&sd, nullptr, &entry->texture))) {
            if (!gToldStagingFailed) {
                gToldStagingFailed = true;
                log::Error("test loop: staging texture {}x{} format {} failed; that layer is left out of snapshots",
                    desc.Width, desc.Height, static_cast<int>(desc.Format));
            }
            entry->texture.Reset();
            return;
        }
        entry->desc = desc;
    }
    if (gCapture.context == nullptr) {
        device->GetImmediateContext(&gCapture.context);
    }
    gCapture.context->CopyResource(entry->texture.Get(), image);
    entry->written = true;
}

// A swapchain image Halcyon is about to release: its index from the wrapped acquire, its texture
// from the runtime's list.
void StageReleased(XrSwapchain swapchain) {
    ID3D11Texture2D* image = nullptr;
    {
        std::lock_guard lock(gSwapchainMutex);
        SwapchainInfo& info = gSwapchains[swapchain];
        if (!info.enumerated) {
            info.enumerated = true;
            std::uint32_t count = 0;
            if (XR_SUCCEEDED(xrEnumerateSwapchainImages(swapchain, 0, &count, nullptr)) && count > 0) {
                std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
                if (XR_SUCCEEDED(xrEnumerateSwapchainImages(swapchain, count, &count,
                        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) {
                    for (auto const& i : images) {
                        info.images.push_back(i.texture);
                    }
                }
            }
        }
        if (info.acquired < info.images.size()) {
            image = info.images[info.acquired];
        }
    }
    StageImage(swapchain, image);
}

std::string NameFor(XrSwapchain swapchain) {
    std::lock_guard lock(gSwapchainMutex);
    auto const it = gSwapchains.find(swapchain);
    return it != gSwapchains.end() ? it->second.name : std::string();
}

bool IsStatic(XrSwapchain swapchain) {
    std::lock_guard lock(gSwapchainMutex);
    auto const it = gSwapchains.find(swapchain);
    return it != gSwapchains.end() && it->second.isStatic;
}

XrPosef Compose(XrPosef const& a, XrPosef const& b) {  // b given in a's frame -> a's parent
    XrQuaternionf const& q = a.orientation;
    XrQuaternionf const& r = b.orientation;
    XrQuaternionf const o{q.w * r.x + q.x * r.w + q.y * r.z - q.z * r.y, q.w * r.y - q.x * r.z + q.y * r.w + q.z * r.x,
        q.w * r.z + q.x * r.y - q.y * r.x + q.z * r.w, q.w * r.w - q.x * r.x - q.y * r.y - q.z * r.z};
    // Rotate b's position by q.
    XrVector3f const v = b.position;
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const t{2.0f * (u.y * v.z - u.z * v.y), 2.0f * (u.z * v.x - u.x * v.z), 2.0f * (u.x * v.y - u.y * v.x)};
    XrVector3f const rotated{v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
        v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
    return {o, {a.position.x + rotated.x, a.position.y + rotated.y, a.position.z + rotated.z}};
}

std::vector<LayerShot> ParseLayers(XrFrameEndInfo const& info) {
    std::vector<LayerShot> layers;
    for (std::uint32_t i = 0; i < info.layerCount && info.layers != nullptr; ++i) {
        XrCompositionLayerBaseHeader const* base = info.layers[i];
        if (base == nullptr) {
            continue;
        }
        if (base->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            auto const* p = reinterpret_cast<XrCompositionLayerProjection const*>(base);
            for (std::uint32_t v = 0; v < p->viewCount && v < 2 && p->views != nullptr; ++v) {
                XrCompositionLayerProjectionView const& view = p->views[v];
                LayerShot shot;
                shot.projection = true;
                shot.eye = static_cast<int>(v);
                shot.name = v == 0 ? "left" : "right";
                shot.swapchain = view.subImage.swapchain;
                shot.rect = view.subImage.imageRect;
                shot.arrayIndex = view.subImage.imageArrayIndex;
                shot.flags = p->layerFlags;
                shot.space = p->space;
                shot.pose = view.pose;
                shot.fov = view.fov;
                shot.located = true;
                shot.poseInReference = view.pose;
                layers.push_back(shot);
            }
        } else if (base->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            auto const* q = reinterpret_cast<XrCompositionLayerQuad const*>(base);
            LayerShot shot;
            shot.swapchain = q->subImage.swapchain;
            shot.rect = q->subImage.imageRect;
            shot.arrayIndex = q->subImage.imageArrayIndex;
            shot.flags = q->layerFlags;
            shot.space = q->space;
            shot.pose = q->pose;
            shot.size = q->size;
            shot.visibility = q->eyeVisibility;
            layers.push_back(shot);
        }
    }
    return layers;
}

std::string LayersJson(std::vector<LayerShot> const& layers) {
    std::string out;
    for (LayerShot const& l : layers) {
        bool const blend = (l.flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
        out += std::format("{}\n    {{\"type\": {}, \"name\": {}, \"file\": {}, \"rect\": [{}, {}, {}, {}], \"arrayIndex\": {}, "
                           "\"blend\": {}, \"premultiplied\": {}, \"pose\": {}",
            out.empty() ? "" : ",", req::JsonString(l.projection ? "projection" : "quad"), req::JsonString(l.name),
            l.file.empty() ? "null" : req::JsonString(l.file), l.rect.offset.x, l.rect.offset.y, l.rect.extent.width,
            l.rect.extent.height, l.arrayIndex, J(blend),
            J(blend && (l.flags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) == 0), JPose(l.pose));
        if (l.projection) {
            out += std::format(", \"fov\": {}}}", JFov(l.fov));
        } else {
            out += std::format(", \"size\": [{}, {}], \"located\": {}, \"poseInEyeSpace\": {}, \"eyeVisibility\": {}}}",
                J(l.size.width), J(l.size.height), J(l.located), JPose(l.poseInReference), static_cast<int>(l.visibility));
        }
    }
    return out;
}

std::string MetaJson(std::vector<LayerShot> const& layers, XrTime displayTime, std::string const& trigger) {
    vr::controllers::State const& c = gFrameState;
    vr::aim_ray::Ray const ray = vr::aim_ray::Latest();
    vr::host_view::State const view = vr::host_view::Latest();
    std::string mod = "null";
    std::string tag;
    {
        std::lock_guard lock(gControlMutex);
        if (!gModStatus.empty() && GetTickCount64() - gModStatusMs < 1000) {
            mod = gModStatus;
        }
        tag = gSnapshotTag;
    }
    std::string head = std::format("{{\"valid\": {}", J(gHeadValid));
    if (gHeadValid) {
        XrVector3f const center{0.5f * (gHead[0].pose.position.x + gHead[1].pose.position.x),
            0.5f * (gHead[0].pose.position.y + gHead[1].pose.position.y),
            0.5f * (gHead[0].pose.position.z + gHead[1].pose.position.z)};
        head += std::format(", \"orientation\": {}, \"center\": {}, \"leftEye\": {}, \"rightEye\": {}, \"leftFov\": {}, "
                            "\"rightFov\": {}",
            JQuat(gHead[0].pose.orientation), JVec(center), JPose(gHead[0].pose), JPose(gHead[1].pose), JFov(gHead[0].fov),
            JFov(gHead[1].fov));
    }
    head += "}";
    std::string replay = "null";
    if (gReplay != nullptr) {
        replay = std::format("{{\"path\": {}, \"timeS\": {}, \"durationS\": {}, \"loop\": {}, \"inputs\": {}}}",
            req::JsonString(Utf8(gReplay->path)), J(static_cast<double>(gReplayPositionNs.load()) * 1e-9),
            J(static_cast<double>(gReplayDurationNs.load()) * 1e-9), J(gReplay->loop), J(gReplay->inputs));
    }
    return std::format(
        "  \"trigger\": {},\n  \"tag\": {},\n  \"frame\": {},\n  \"displayTimeNs\": {},\n  \"unixMs\": {},\n"
        "  \"sessionFocused\": {},\n  \"halcyonMenuOpen\": {},\n  \"recording\": {},\n  \"replay\": {},\n"
        "  \"head\": {},\n  \"controllers\": {{\"active\": {}, \"left\": {}, \"right\": {}}},\n"
        "  \"aimRay\": {{\"valid\": {}, \"origin\": {}, \"direction\": {}, \"ageMs\": {}}},\n"
        "  \"hostView\": {{\"valid\": {}, \"headLook\": {}, \"headAim\": {}, \"lookYawUU\": {}, \"lookPitchUU\": {}, "
        "\"offsetUU\": [{}, {}, {}], \"unitsPerMeter\": {}}},\n  \"mod\": {},\n  \"layers\": [{}\n  ]",
        req::JsonString(trigger), req::JsonString(tag), gFrame, static_cast<long long>(displayTime), UnixMs(), J(gFocused),
        J(MELEVR::Menu::IsOpen()), J(gRecordingActive.load()), replay, head, J(c.active), JHand(c.left), JHand(c.right),
        J(ray.valid), JVec(ray.origin), JVec(ray.direction), ray.timeMs != 0 ? GetTickCount64() - ray.timeMs : 0,
        J(view.valid), J(view.headLook), J(view.headAim), view.lookYaw, view.lookPitch, J(view.offsetForward),
        J(view.offsetRight), J(view.offsetUp), J(view.unitsPerMeter), mod, LayersJson(layers));
}

// The capture's last armed frame: what was submitted, where the quads are, the metadata.
void FinishArming(XrFrameEndInfo const& info, std::vector<LayerShot> layers) {
    // The eyes' space: the projection layer's (Halcyon's app space); the quads are located in it.
    XrSpace reference = gAppSpace;
    for (LayerShot const& l : layers) {
        if (l.projection) {
            reference = l.space;
        }
    }
    auto job = std::make_shared<Job>();
    job->stamp = gCapture.stamp;
    for (LayerShot const& l : layers) {
        if (l.projection && (l.eye == 0 || l.eye == 1)) {
            job->eyes[l.eye] = {l.pose, l.fov};
            job->eyesValid = true;
        }
    }
    if (!job->eyesValid && gHeadValid && !layers.empty()) {  // no world layer: the quads seen from the head
        job->eyes[0] = {gHead[0].pose, gHead[0].fov};
        job->eyes[1] = {gHead[1].pose, gHead[1].fov};
        job->eyesValid = true;
    }
    int quadIndex = 0;
    std::vector<std::string> usedNames;
    for (LayerShot& l : layers) {
        if (l.projection) {
            continue;
        }
        l.name = NameFor(l.swapchain);
        if (l.name.empty() && gMenuQuad.frame == gFrame && std::abs(l.size.width - gMenuQuad.width) < 1e-3f &&
            std::abs(l.size.height - gMenuQuad.height) < 1e-3f && std::abs(l.pose.position.x - gMenuQuad.position.x) < 1e-3f &&
            std::abs(l.pose.position.y - gMenuQuad.position.y) < 1e-3f &&
            std::abs(l.pose.position.z - gMenuQuad.position.z) < 1e-3f) {
            l.name = "menu";
        }
        if (l.name.empty()) {
            l.name = std::format("quad{}", quadIndex);
        }
        ++quadIndex;
        if (std::find(usedNames.begin(), usedNames.end(), l.name) != usedNames.end()) {
            l.name += std::format("-{}", quadIndex);
        }
        usedNames.push_back(l.name);
        if (l.name != "reticle") {  // the reticle never changes: it's in the composites only
            l.file = std::format("{}-{}.png", gCapture.stamp, l.name);
        }
        if (l.space == reference) {
            l.located = true;
            l.poseInReference = l.pose;
        } else if (l.name == "menu" && gMenuQuad.headLocked && gHeadValid) {
            // Head-locked (VIEW space): placed from the head Halcyon rendered with, which is the replayed
            // one during a replay (the runtime's VIEW space would follow the real headset).
            XrPosef head = gHead[0].pose;
            head.position = {0.5f * (gHead[0].pose.position.x + gHead[1].pose.position.x),
                0.5f * (gHead[0].pose.position.y + gHead[1].pose.position.y),
                0.5f * (gHead[0].pose.position.z + gHead[1].pose.position.z)};
            l.located = true;
            l.poseInReference = Compose(head, l.pose);
        } else {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            l.located = XR_SUCCEEDED(xrLocateSpace(l.space, reference, info.displayTime, &location)) &&
                (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0 &&
                (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
            if (l.located) {
                l.poseInReference = Compose(location.pose, l.pose);
            }
        }
    }
    job->meta = MetaJson(layers, info.displayTime, gCapture.trigger);
    job->layers = std::move(layers);
    gCapture.job = std::move(job);
    gArmed.store(false, std::memory_order_relaxed);
    gCapture.phase = Phase::Staged;
    gCapture.stagedFrames = 0;
}

void OnEndFrameArmed(XrFrameEndInfo const& info) {
    std::vector<LayerShot> layers = ParseLayers(info);
    ++gCapture.armedFrames;
    bool complete = !layers.empty();
    for (LayerShot const& l : layers) {
        if (IsStatic(l.swapchain)) {
            continue;
        }
        bool written = false;
        for (Staged const& s : gCapture.staged) {
            written = written || (s.swapchain == l.swapchain && s.written);
        }
        complete = complete && written;
    }
    if (complete || gCapture.armedFrames >= kMaxArmedFrames) {
        FinishArming(info, std::move(layers));
    }
}

void StartCapture(std::string const& trigger) {
    gCapture.phase = Phase::Armed;
    gCapture.armedFrames = 0;
    gCapture.stamp = Stamp();
    gCapture.trigger = trigger;
    gCapture.armedMs = GetTickCount64();
    for (Staged& s : gCapture.staged) {
        s.written = false;
    }
    gArmed.store(true, std::memory_order_relaxed);
}

void ReleaseStaging() {
    for (Staged& s : gCapture.staged) {
        if (s.mapped && gCapture.context != nullptr) {
            gCapture.context->Unmap(s.texture.Get(), 0);
        }
        s.mapped = false;
    }
}

void TickCapture() {
    std::uint64_t const now = GetTickCount64();
    switch (gCapture.phase) {
    case Phase::Idle: {
        std::string trigger;
        {
            std::lock_guard lock(gControlMutex);
            if (gSnapshotsPending <= 0 || now < gSnapshotDueMs) {
                if (gSnapshotsPending <= 0 && !gCapture.staged.empty()) {
                    gCapture.staged.clear();  // nothing more to take: give the staging memory back
                    gCapture.context.Reset();
                }
                return;
            }
            --gSnapshotsPending;
            gSnapshotDueMs = now + static_cast<std::uint64_t>(std::max(gSnapshotInterval, 0.0) * 1000.0);
            trigger = gSnapshotTrigger;
        }
        StartCapture(trigger);
        break;
    }
    case Phase::Armed:
        if (now - gCapture.armedMs > 5000) {  // no frame was submitted: try again later
            log::Warn("test loop: snapshot {}: no layers were submitted for 5 s; given up", gCapture.stamp);
            gArmed.store(false, std::memory_order_relaxed);
            gCapture.phase = Phase::Idle;
            gPendingCount.fetch_sub(1, std::memory_order_relaxed);
        }
        break;
    case Phase::Staged: {
        if (++gCapture.stagedFrames < kMapAfterFrames) {
            break;
        }
        bool all = true;
        for (Staged& s : gCapture.staged) {
            if (!s.written || s.mapped || s.texture == nullptr) {
                continue;
            }
            UINT const flags = gCapture.stagedFrames < kBlockingMapAfterFrames ? D3D11_MAP_FLAG_DO_NOT_WAIT : 0u;
            HRESULT const hr = gCapture.context->Map(s.texture.Get(), 0, D3D11_MAP_READ, flags, &s.map);
            if (SUCCEEDED(hr)) {
                s.mapped = true;
            } else {
                all = false;
                if (hr != DXGI_ERROR_WAS_STILL_DRAWING) {
                    log::Error("test loop: snapshot {}: mapping a staging copy failed (0x{:08X})", gCapture.stamp,
                        static_cast<unsigned>(hr));
                    s.written = false;  // leave it out
                    all = true;
                }
            }
        }
        if (!all) {
            break;
        }
        Job& job = *gCapture.job;
        for (Staged const& s : gCapture.staged) {
            if (s.mapped) {
                Source source;
                source.swapchain = s.swapchain;
                source.format = static_cast<int>(s.desc.Format);
                source.width = static_cast<int>(s.desc.Width);
                source.height = static_cast<int>(s.desc.Height);
                source.data = static_cast<std::uint8_t const*>(s.map.pData);
                source.rowPitch = s.map.RowPitch;
                job.sources.push_back(std::move(source));
            }
        }
        {
            std::lock_guard lock(gSwapchainMutex);
            for (LayerShot const& l : job.layers) {
                auto const it = gSwapchains.find(l.swapchain);
                if (it != gSwapchains.end() && it->second.isStatic && FindSource(job, l.swapchain) == nullptr) {
                    Source source;
                    source.swapchain = l.swapchain;
                    source.format = vr::image::format::kR8G8B8A8UnormSrgb;
                    source.width = static_cast<int>(it->second.staticWidth);
                    source.height = static_cast<int>(it->second.staticHeight);
                    source.pixels = it->second.staticPixels;
                    job.sources.push_back(std::move(source));
                }
            }
        }
        TheWorker().Push(gCapture.job);
        gCapture.phase = Phase::Mapped;
        break;
    }
    case Phase::Mapped:
        if (gCapture.job == nullptr || gCapture.job->converted.load(std::memory_order_acquire)) {
            ReleaseStaging();
            gCapture.job.reset();
            gCapture.phase = Phase::Idle;
            gPendingCount.fetch_sub(1, std::memory_order_relaxed);
        }
        break;
    }
}

// ---- Recording and replay (Present thread) -----------------------------------------------------------

void StopRecordingNow(char const* why) {
    if (!gRecorder.writer.IsOpen()) {
        return;
    }
    if (gRecorder.pending) {
        gRecorder.writer.Append(gRecorder.frame);
        gRecorder.pending = false;
    }
    std::size_t const frames = gRecorder.writer.Frames();
    gRecorder.writer.Close();
    gRecordingActive.store(false, std::memory_order_relaxed);
    double const seconds = static_cast<double>(gRecorder.frame.timeNs) * 1e-9;
    log::Info("test loop: recording stopped ({}): {} frames, {:.1f} s, {}", why, frames, seconds, Utf8(gRecorder.path));
    std::lock_guard lock(gControlMutex);
    gLastRecording = Utf8(gRecorder.path);
}

void StartRecordingNow(double maxSeconds) {
    StopRecordingNow("restarted");
    fs::path const path = RecordingDir() / Widen(Stamp() + ".mevrrec");
    if (!gRecorder.writer.Open(path, UnixMs())) {
        log::Error("test loop: can't create {}; not recording", Utf8(path));
        return;
    }
    gRecorder.path = path;
    gRecorder.started = false;
    gRecorder.pending = false;
    gRecorder.frame = {};
    gRecorder.maxSeconds = maxSeconds;
    gRecordStartMs.store(GetTickCount64(), std::memory_order_relaxed);
    gRecordingActive.store(true, std::memory_order_relaxed);
    log::Info("test loop: recording the head and controllers to {}{}", Utf8(path),
        maxSeconds > 0.0 ? std::format(" for {:.0f} s", maxSeconds) : std::string(" (Home, both grips + right stick click, or "
                                                                                    "record.request stops it)"));
}

void TickRecording(XrTime displayTime, vr::controllers::State const& live) {
    int command = 2;
    double seconds = 0.0;
    {
        std::lock_guard lock(gControlMutex);
        std::swap(command, gRecordCommand);
        seconds = gRecordSeconds;
    }
    bool const active = gRecorder.writer.IsOpen();
    if (command == 1 || (command == -1 && !active)) {
        if (gReplay != nullptr) {
            log::Warn("test loop: not recording while a replay runs (stop it first)");
        } else {
            StartRecordingNow(seconds);
        }
    } else if (command == 0 || (command == -1 && active)) {
        StopRecordingNow("asked");
    }
    if (!gRecorder.writer.IsOpen()) {
        return;
    }
    if (gRecorder.pending && !gRecorder.writer.Append(gRecorder.frame)) {
        log::Error("test loop: writing {} failed; recording stopped", Utf8(gRecorder.path));
        gRecorder.pending = false;
        StopRecordingNow("write failed");
        return;
    }
    if (!gRecorder.started) {
        gRecorder.started = true;
        gRecorder.start = displayTime;
    }
    rec::Frame& f = gRecorder.frame;
    f = {};
    f.timeNs = static_cast<std::int64_t>(displayTime - gRecorder.start);
    f.controllersActive = live.active;
    f.hands[0] = live.left;
    f.hands[1] = live.right;
    gRecorder.pending = true;  // the head is added when Halcyon locates it this frame
    if (gRecorder.maxSeconds > 0.0 && static_cast<double>(f.timeNs) * 1e-9 >= gRecorder.maxSeconds) {
        StopRecordingNow("time is up");
    }
}

void EndReplay(char const* why) {
    if (gReplay == nullptr) {
        return;
    }
    log::Info("test loop: replay of {} {} after {:.1f} s; the headset and controllers are live again", Utf8(gReplay->path),
        why, static_cast<double>(gReplayPositionNs.load()) * 1e-9);
    gReplay.reset();
    gReplayActive.store(false, std::memory_order_relaxed);
    gReplayInputs.store(false, std::memory_order_relaxed);
}

void ApplyReplay(XrTime displayTime, vr::controllers::State& state) {
    std::shared_ptr<Replay> pending;
    bool stop = false;
    {
        std::lock_guard lock(gControlMutex);
        pending = std::move(gReplayPending);
        stop = std::exchange(gReplayStopRequested, false);
    }
    if (stop) {
        EndReplay("stopped");
    }
    if (pending != nullptr) {
        EndReplay("replaced");
        StopRecordingNow("a replay started");
        gReplay = std::move(pending);
        gReplayActive.store(true, std::memory_order_relaxed);
        gReplayInputs.store(gReplay->inputs, std::memory_order_relaxed);
        gReplayLoop.store(gReplay->loop, std::memory_order_relaxed);
        gReplayDurationNs.store(gReplay->recording->DurationNs(), std::memory_order_relaxed);
        log::Info("test loop: replaying {} ({} frames, {:.1f} s{}{}): the recorded head and controller poses{} stand in "
                  "for the live ones",
            Utf8(gReplay->path), gReplay->recording->frames.size(), static_cast<double>(gReplay->recording->DurationNs()) * 1e-9,
            gReplay->loop ? ", looping" : "", gReplay->inputs ? ", with buttons and sticks" : "",
            gReplay->inputs ? " and inputs" : " (live buttons and sticks)");
    }
    if (gReplay == nullptr) {
        return;
    }
    if (!gReplay->started) {
        gReplay->started = true;
        gReplay->start = displayTime;
    }
    std::int64_t const t = static_cast<std::int64_t>(displayTime - gReplay->start);
    gReplayPositionNs.store(t, std::memory_order_relaxed);
    rec::Frame f;
    if (!rec::Sample(*gReplay->recording, t, gReplay->loop, f)) {
        EndReplay("finished");
        return;
    }
    vr::controllers::Hand* const hands[2] = {&state.left, &state.right};
    for (int h = 0; h < 2; ++h) {
        vr::controllers::Hand& out = *hands[h];
        rec::Frame const& src = f;
        if (gReplay->inputs) {
            out = src.hands[h];
        } else {
            out.gripValid = src.hands[h].gripValid;
            out.grip = src.hands[h].grip;
            out.aimValid = src.hands[h].aimValid;
            out.aim = src.hands[h].aim;
        }
    }
    state.active = true;
}

void SubstituteHead(XrViewLocateInfo const& info, XrResult& result, XrViewState& viewState, std::uint32_t& count,
    XrView* views) {
    rec::Frame f;
    rec::Sample(*gReplay->recording, static_cast<std::int64_t>(info.displayTime - gReplay->start), gReplay->loop, f);
    if (!f.headValid) {
        return;
    }
    bool const live = XR_SUCCEEDED(result) && count >= 2 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    for (int e = 0; e < 2; ++e) {
        views[e].pose = f.views[e].pose;
        if (!live) {
            views[e].fov = f.views[e].fov;  // the runtime gave none: the recorded ones
        }
    }
    count = 2;
    viewState.viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
        XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    result = XR_SUCCESS;
}

// ---- Wrappers ------------------------------------------------------------------------------------------

XrResult XRAPI_CALL WrappedAcquire(XrSwapchain swapchain, XrSwapchainImageAcquireInfo const* info, std::uint32_t* index) {
    XrResult const result = gRealAcquire(swapchain, info, index);
    if (XR_SUCCEEDED(result) && index != nullptr) {
        std::lock_guard lock(gSwapchainMutex);
        gSwapchains[swapchain].acquired = *index;
    }
    return result;
}

XrResult XRAPI_CALL WrappedRelease(XrSwapchain swapchain, XrSwapchainImageReleaseInfo const* info) {
    if (gArmed.load(std::memory_order_relaxed) && OnPresentThread()) {
        StageReleased(swapchain);
    }
    return gRealRelease(swapchain, info);
}

XrResult XRAPI_CALL WrappedEndFrame(XrSession session, XrFrameEndInfo const* info) {
    if (gArmed.load(std::memory_order_relaxed) && info != nullptr && OnPresentThread()) {
        OnEndFrameArmed(*info);
    }
    return gRealEndFrame(session, info);
}

XrResult XRAPI_CALL WrappedLocateViews(XrSession session, XrViewLocateInfo const* info, XrViewState* viewState,
    std::uint32_t capacity, std::uint32_t* count, XrView* views) {
    XrResult result = gRealLocateViews(session, info, viewState, capacity, count, views);
    if (info == nullptr || viewState == nullptr || count == nullptr || views == nullptr || capacity < 2 ||
        !OnPresentThread()) {
        return result;
    }
    if (gReplay != nullptr && gReplay->started) {
        SubstituteHead(*info, result, *viewState, *count, views);
    }
    if (info->space == gAppSpace && info->displayTime == gFrameTime && XR_SUCCEEDED(result) && *count >= 2 &&
        (viewState->viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0) {
        gHead[0] = views[0];
        gHead[1] = views[1];
        gHeadValid = true;
        if (gRecorder.pending && !gRecorder.frame.headValid) {
            gRecorder.frame.headValid = true;
            for (int e = 0; e < 2; ++e) {
                gRecorder.frame.views[e].pose = views[e].pose;
                gRecorder.frame.views[e].fov = views[e].fov;
            }
        }
    }
    return result;
}

// ---- Request files and keys (game thread) ---------------------------------------------------------------

// A request file's text, then deletes it. Skipped while it's younger than 200 ms (still being written).
bool TakeRequest(fs::path const& path, std::string& text) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a{}, b{};
    a.LowPart = data.ftLastWriteTime.dwLowDateTime;
    a.HighPart = data.ftLastWriteTime.dwHighDateTime;
    b.LowPart = now.dwLowDateTime;
    b.HighPart = now.dwHighDateTime;
    if (b.QuadPart > a.QuadPart && b.QuadPart - a.QuadPart < 2'000'000ull) {
        return false;  // 200 ms in 100 ns units
    }
    std::string bytes;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (!DeleteFileW(path.c_str())) {
        fs::path const taken = fs::path(path).concat(L".taken");
        if (!MoveFileExW(path.c_str(), taken.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            static bool told = false;
            if (!told) {
                told = true;
                log::Error("test loop: can't delete or rename {} (error {}); request ignored", Utf8(path), GetLastError());
            }
            return false;
        }
    }
    // UTF-16 LE with a byte order mark: Windows PowerShell 5's Out-File default.
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring wide((bytes.size() - 2) / 2, L'\0');
        std::memcpy(wide.data(), bytes.data() + 2, wide.size() * 2);
        text = Narrow(wide);
    } else {
        text = std::move(bytes);
    }
    return true;
}

fs::path LatestRecording() {
    std::error_code ec;
    fs::path best;
    fs::file_time_type bestTime{};
    for (auto const& entry : fs::directory_iterator(RecordingDir(), ec)) {
        if (entry.path().extension() == L".mevrrec") {
            auto const t = entry.last_write_time(ec);
            if (best.empty() || t > bestTime) {
                best = entry.path();
                bestTime = t;
            }
        }
    }
    return best;
}

void PollRequests() {
    std::string text;
    if (TakeRequest(BaseDir() / L"snapshot.request", text)) {
        req::Request const r = req::Parse(text);
        SnapshotRequest s;
        s.count = static_cast<int>(std::clamp(r.Number("count", 1.0), 1.0, 600.0));
        s.intervalSeconds = std::clamp(r.Number("interval", 1.0), 0.0, 3600.0);
        s.delaySeconds = std::clamp(r.Number("delay", 0.0), 0.0, 3600.0);
        s.tag = r.Get("tag");
        s.trigger = "file";
        log::Info("test loop: snapshot.request: {} snapshot{}{}{}", s.count, s.count == 1 ? "" : "s",
            s.count > 1 ? std::format(", {:.2f} s apart", s.intervalSeconds) : std::string(),
            s.delaySeconds > 0.0 ? std::format(", starting in {:.1f} s", s.delaySeconds) : std::string());
        RequestSnapshot(s);
    }
    if (TakeRequest(BaseDir() / L"record.request", text)) {
        req::Request const r = req::Parse(text);
        std::string const what = r.argument.empty() ? "toggle" : r.argument;
        int const command = what == "start" ? 1 : what == "stop" ? 0 : -1;
        log::Info("test loop: record.request: {}", what);
        RequestRecording(command, r.Number("seconds", 0.0));
    }
    if (TakeRequest(BaseDir() / L"replay.request", text)) {
        req::Request const r = req::Parse(text);
        if (r.argument == "stop") {
            log::Info("test loop: replay.request: stop");
            StopReplay();
        } else {
            std::string error;
            std::wstring const path = (r.argument.empty() || r.argument == "latest") ? std::wstring() : Widen(r.argument);
            if (!StartReplay(path, r.Flag("loop", false), r.Flag("inputs", false), &error)) {
                log::Error("test loop: replay.request {}: {}", r.argument.empty() ? "latest" : r.argument, error);
            }
        }
    }
}

std::string StatusJson(Status const& s) {
    return std::format(
        "{{\n  \"recording\": {},\n  \"recordingSeconds\": {},\n  \"replaying\": {},\n  \"replayPath\": {},\n"
        "  \"replaySeconds\": {},\n  \"replayDuration\": {},\n  \"replayLoop\": {},\n  \"replayInputs\": {},\n"
        "  \"lastRecording\": {},\n  \"lastSnapshot\": {},\n  \"snapshotsPending\": {},\n  \"framesRunning\": {}\n}}\n",
        J(s.recording), J(s.recordingSeconds), J(s.replaying), req::JsonString(s.replayPath), J(s.replaySeconds),
        J(s.replayDuration), J(s.replayLoop), J(s.replayInputs), req::JsonString(s.lastRecording),
        req::JsonString(s.lastSnapshot), s.snapshotsPending, J(s.framesRunning));
}

Hotkey gSnapshotKey{VK_END};
Hotkey gRecordKey{VK_HOME};
double gPollTimer = 0.0;
std::string gLastStatusJson;

} // namespace

// ---- Public -------------------------------------------------------------------------------------------

void OnSessionCreated(XrSession session) {
    std::lock_guard lock(gSwapchainMutex);
    // Swapchains die with their session; ours (named, static) are re-registered when re-created.
    gSwapchains.clear();
    (void)session;
}

void OnFrameBegun(XrSession session, XrSpace appSpace, XrTime displayTime, bool focused, vr::controllers::State& state) {
    gPresentThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    gSession = session;
    gAppSpace = appSpace;
    gFrameTime = displayTime;
    gFocused = focused;
    ++gFrame;
    gHeadValid = false;
    gLastFrameMs.store(GetTickCount64(), std::memory_order_relaxed);

    vr::controllers::State const live = state;
    TickRecording(displayTime, live);
    ApplyReplay(displayTime, state);

    // Chords on the live controllers: both grips squeezed, then a stick click.
    static bool s_leftClick = false;
    static bool s_rightClick = false;
    bool const grips = live.active && live.left.squeeze > 0.75f && live.right.squeeze > 0.75f;
    gGripChord.store(grips, std::memory_order_relaxed);
    if (grips && live.left.stickClick && !s_leftClick) {
        log::Info("test loop: both grips + left stick: snapshot");
        SnapshotRequest r;
        r.trigger = "chord";
        RequestSnapshot(r);
    }
    if (grips && live.right.stickClick && !s_rightClick) {
        log::Info("test loop: both grips + right stick: recording {}", gRecordingActive.load() ? "off" : "on");
        RequestRecording(-1);
    }
    s_leftClick = live.left.stickClick;
    s_rightClick = live.right.stickClick;

    gFrameState = state;
    TickCapture();
}

void OnImageWritten(XrSwapchain swapchain, ID3D11Texture2D* image) {
    if (gArmed.load(std::memory_order_relaxed) && OnPresentThread()) {
        StageImage(swapchain, image);
    }
}

void SetStaticImage(XrSwapchain swapchain, std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t> pixels) {
    std::lock_guard lock(gSwapchainMutex);
    SwapchainInfo& info = gSwapchains[swapchain];
    info.isStatic = pixels.size() == static_cast<size_t>(width) * height;
    info.staticWidth = width;
    info.staticHeight = height;
    info.staticPixels = std::move(pixels);
}

void NameSwapchain(XrSwapchain swapchain, char const* name) {
    std::lock_guard lock(gSwapchainMutex);
    gSwapchains[swapchain].name = name != nullptr ? name : "";
}

void ForgetSwapchain(XrSwapchain swapchain) {
    std::lock_guard lock(gSwapchainMutex);
    gSwapchains.erase(swapchain);
}

void NoteMenuQuad(float const position[3], bool headLocked, float width, float height) {
    gMenuQuad.frame = gFrame;
    gMenuQuad.position = {position[0], position[1], position[2]};
    gMenuQuad.headLocked = headLocked;
    gMenuQuad.width = width;
    gMenuQuad.height = height;
}

bool GripChordHeld() {
    return gGripChord.load(std::memory_order_relaxed);
}

bool Replaying() {
    return gReplayActive.load(std::memory_order_relaxed);
}

bool ReplayDrivesInputs() {
    return gReplayActive.load(std::memory_order_relaxed) && gReplayInputs.load(std::memory_order_relaxed);
}

bool Recording() {
    return gRecordingActive.load(std::memory_order_relaxed);
}

void RequestSnapshot(SnapshotRequest const& request) {
    std::uint64_t const now = GetTickCount64();
    {
        std::lock_guard lock(gControlMutex);
        int const count = std::max(request.count, 1);
        gSnapshotsPending += count;
        gPendingCount.fetch_add(count, std::memory_order_relaxed);
        gSnapshotInterval = std::max(request.intervalSeconds, 0.0);
        // At least 100 ms: the mod's next tick publishes its state for the JSON.
        gSnapshotDueMs = now + static_cast<std::uint64_t>(std::max(request.delaySeconds, 0.1) * 1000.0);
        gSnapshotTag = SafeTag(request.tag);
        gSnapshotTrigger = request.trigger != nullptr ? request.trigger : "?";
    }
    gWantStatusUntilMs.store(now + 1000 + static_cast<std::uint64_t>(std::max(request.delaySeconds, 0.0) * 1000.0),
        std::memory_order_relaxed);
    if (now - gLastFrameMs.load(std::memory_order_relaxed) > 1000) {
        log::Info("test loop: snapshot requested; waiting for the headset's frames (the VR session isn't running)");
    }
}

void RequestRecording(int command, double seconds) {
    std::lock_guard lock(gControlMutex);
    gRecordCommand = command;
    gRecordSeconds = std::max(seconds, 0.0);
}

bool StartReplay(std::wstring const& path, bool loop, bool inputs, std::string* error) {
    fs::path const file = path.empty() ? LatestRecording() : fs::path(path);
    if (file.empty()) {
        if (error != nullptr) {
            *error = "no recordings in " + Utf8(RecordingDir());
        }
        return false;
    }
    auto recording = std::make_shared<rec::Recording>();
    std::string why;
    if (!rec::Load(file, *recording, &why)) {
        if (error != nullptr) {
            *error = Utf8(file) + ": " + why;
        }
        return false;
    }
    auto replay = std::make_shared<Replay>();
    replay->recording = std::move(recording);
    replay->path = file;
    replay->loop = loop;
    replay->inputs = inputs;
    std::lock_guard lock(gControlMutex);
    gReplayPending = std::move(replay);
    gReplayPathText = Utf8(file);
    gRecordCommand = 2;
    return true;
}

void StopReplay() {
    std::lock_guard lock(gControlMutex);
    gReplayPending.reset();
    gReplayStopRequested = true;
}

Status GetStatus() {
    Status s;
    std::uint64_t const now = GetTickCount64();
    s.recording = gRecordingActive.load(std::memory_order_relaxed);
    if (s.recording) {
        s.recordingSeconds = static_cast<double>(now - gRecordStartMs.load(std::memory_order_relaxed)) * 1e-3;
    }
    s.replaying = gReplayActive.load(std::memory_order_relaxed);
    s.replayLoop = gReplayLoop.load(std::memory_order_relaxed);
    s.replayInputs = gReplayInputs.load(std::memory_order_relaxed);
    s.replaySeconds = static_cast<double>(gReplayPositionNs.load(std::memory_order_relaxed)) * 1e-9;
    s.replayDuration = static_cast<double>(gReplayDurationNs.load(std::memory_order_relaxed)) * 1e-9;
    s.snapshotsPending = std::max(gPendingCount.load(std::memory_order_relaxed), 0);
    s.framesRunning = now - gLastFrameMs.load(std::memory_order_relaxed) < 1000;
    std::lock_guard lock(gControlMutex);
    s.replayPath = s.replaying ? gReplayPathText : std::string();
    s.lastRecording = gLastRecording;
    s.lastSnapshot = gLastSnapshot;
    return s;
}

void Tick(float deltaSeconds) {
    if (gSnapshotKey.Pressed()) {
        log::Info("test loop: End: snapshot");
        RequestSnapshot({});
    }
    if (gRecordKey.Pressed()) {
        log::Info("test loop: Home: recording {}", gRecordingActive.load() ? "off" : "on");
        RequestRecording(-1);
    }
    gPollTimer += deltaSeconds;
    if (gPollTimer < 1.0) {
        return;
    }
    gPollTimer = 0.0;
    PollRequests();
    std::string const status = StatusJson(GetStatus());
    if (status != gLastStatusJson) {
        if (WriteFileAtomically(BaseDir() / L"testloop.status", status)) {
            gLastStatusJson = status;
        }
    }
}

bool ModStatusWanted() {
    return gPendingCount.load(std::memory_order_relaxed) > 0 ||
        GetTickCount64() < gWantStatusUntilMs.load(std::memory_order_relaxed);
}

void SetModStatus(char const* json) {
    std::lock_guard lock(gControlMutex);
    gModStatus = json != nullptr ? json : "";
    gModStatusMs = GetTickCount64();
}

} // namespace mevr::host::testloop

namespace MELEVR::MevrBridge {

void WrapSessionFunctions(void** acquireSwapchainImage, void** releaseSwapchainImage, void** endFrame,
    void** locateViews) noexcept {
    using namespace mevr::host::testloop;
    // Re-resolving hands us the real functions again; never wrap a wrapper.
    auto wrap = [](void** slot, auto& real, auto wrapper) {
        if (slot == nullptr || *slot == nullptr || *slot == reinterpret_cast<void*>(wrapper)) {
            return;
        }
        real = reinterpret_cast<std::remove_reference_t<decltype(real)>>(*slot);
        *slot = reinterpret_cast<void*>(wrapper);
    };
    wrap(acquireSwapchainImage, gRealAcquire, &WrappedAcquire);
    wrap(releaseSwapchainImage, gRealRelease, &WrappedRelease);
    wrap(endFrame, gRealEndFrame, &WrappedEndFrame);
    wrap(locateViews, gRealLocateViews, &WrappedLocateViews);
    mevr::log::Info("test loop: wrapped Halcyon's xrAcquireSwapchainImage, xrReleaseSwapchainImage, xrEndFrame and "
                    "xrLocateViews (snapshots, recording, replay)");
}

} // namespace MELEVR::MevrBridge
