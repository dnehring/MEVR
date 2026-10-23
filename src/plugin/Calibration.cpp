// The guided calibration in the headset (plugin/Calibration.hpp, research/25).

#include "plugin/Calibration.hpp"

#include "core/Log.hpp"
#include "core/vr/Floor.hpp"
#include "core/vr/MenuPointer.hpp"
#include "core/vr/TestRequest.hpp"
#include "halcyon/le1/vr_menu.h"
#include "plugin/CalibrationDraw.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/TestLoop.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mevr::host::calibration {
namespace {

using Microsoft::WRL::ComPtr;
namespace cal = vr::calibration;
namespace draw = calibration_draw;
using draw::Screen;

constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // the bytes are sRGB already
constexpr float kHoldSeconds = 1.0f;
constexpr std::uint64_t kMessageMs = 4500;
constexpr std::uint64_t kFlashMs = 450;
constexpr std::uint64_t kSavedMs = 3500;
constexpr float kPromptAfterSeconds = 8.0f;   // world frames on foot before the first-run prompt shows
constexpr float kPromptTimeoutSeconds = 45.0f;
constexpr float kTickSeconds = 0.035f;        // the capture's haptic tick
constexpr float kTickAmplitude = 0.7f;
constexpr float kTriggerOn = 0.6f;
constexpr float kTriggerOff = 0.4f;

// ---- Across threads ----------------------------------------------------------------------------------------
std::atomic<bool> gRunning{false};
std::atomic<bool> gPaused{false};
std::atomic<bool> gPromptPointed{false};
std::mutex gStateMutex;  // gHost, gSummary
cal::HostState gHost;
Summary gSummary;

// ---- Present thread ---------------------------------------------------------------------------------------
std::string gSystem;
std::string gProfile;
std::string gKey;
std::string gLabel;
bool gKeyChecked = false;     // the ini was read for gKey: calibrated / prompted
bool gKeyCalibrated = false;
bool gKeyPrompted = false;
vr::controllers::State gControllers;  // the latest live (or replayed) sample

enum class Step { Stand, TPose, Forward, Crouch, PistolGrab, PistolAim, RifleGrab, RifleAim, Check, Saved };

Screen ScreenOf(Step step) {
    switch (step) {
    case Step::Stand: return Screen::Stand;
    case Step::TPose: return Screen::TPose;
    case Step::Forward: return Screen::Forward;
    case Step::Crouch: return Screen::Crouch;
    case Step::PistolGrab: return Screen::PistolGrab;
    case Step::PistolAim: return Screen::PistolAim;
    case Step::RifleGrab: return Screen::RifleGrab;
    case Step::RifleAim: return Screen::RifleAim;
    case Step::Check: return Screen::Check;
    case Step::Saved: return Screen::Saved;
    }
    return Screen::Stand;
}

char const* StepName(Step step) {
    switch (step) {
    case Step::Stand: return "stand";
    case Step::TPose: return "t-pose";
    case Step::Forward: return "arms-forward";
    case Step::Crouch: return "crouch";
    case Step::PistolGrab: return "pistol-grab";
    case Step::PistolAim: return "pistol-aim";
    case Step::RifleGrab: return "rifle-grab";
    case Step::RifleAim: return "rifle-aim";
    case Step::Check: return "check";
    case Step::Saved: return "saved";
    }
    return "?";
}

struct Run {
    Step step = Step::Stand;
    bool returnToCheck = false;   // a redo from the check step goes back there
    bool anchored = false;        // the layout has an anchor (set at the start, refined at step 1)
    bool reanchor = false;        // recentered: place it again in front of the head
    cal::Anchor anchor;
    cal::Anchor stepAnchor;       // where the eyes were when the step began (the orbs)
    cal::Stillness still;
    float held = 0.0f;
    std::string status;
    // Body.
    bool haveStand = false;
    std::optional<float> standEye;  // from the floor
    float standHeadY = 0.0f;        // app space
    bool haveT = false;
    XrVector3f tHead{}, tLeft{}, tRight{};
    float tSpan = 0.0f;
    std::optional<float> shoulderWidth;
    std::optional<float> armLength;
    std::optional<float> crouchEye;
    std::optional<float> crouchDrop;
    // Aims.
    std::vector<cal::AimSample> pistolSamples;
    std::vector<cal::AimSample> rifleSamples;
    int target = 0;
    cal::Stance pistol;
    cal::Stance rifle;
    int eye = -1;  // -1: measured; 0 / 1: chosen
    bool rightHeld = false;
    bool leftHeld = false;
    // The panel.
    std::string message;
    bool warn = false;
    std::uint64_t messageUntil = 0;
    std::uint64_t flashAt = 0;
    std::uint64_t savedAt = 0;
    std::uint64_t startedAt = 0;
};
Run gRun;

// The first-run prompt.
struct Prompt {
    bool shown = false;
    bool decided = false;   // this session
    float worldSeconds = 0.0f;
    float shownSeconds = 0.0f;
    cal::Anchor anchor;
};
Prompt gPrompt;

// The pointer on the panel (either hand's aim ray) and the triggers' latches.
struct Pointer {
    bool on = false;
    int hand = 1;
    float x = 0.0f;
    float y = 0.0f;
    int hovered = draw::kNoButton;
};
bool gTrigger[2] = {false, false};

// ---- Settings file ---------------------------------------------------------------------------------------

std::wstring Wide(std::string const& text) {
    return std::wstring(text.begin(), text.end());  // ASCII keys and numbers only
}

std::string ReadIni(std::string const& section, char const* key, char const* fallback = "") {
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (path.empty()) {
        return fallback;
    }
    wchar_t text[512]{};
    GetPrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), Wide(fallback).c_str(), text, 512, path.c_str());
    std::string out;
    for (wchar_t const* c = text; *c != L'\0'; ++c) {
        out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    return out;
}

void WriteIni(std::string const& section, char const* key, std::string const& value) {
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (!path.empty()) {
        WritePrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), Wide(value).c_str(), path.c_str());
    }
}

std::string Section(std::string const& key) {
    return "Calibration." + key;
}

std::string Fixed(float value, int decimals = 3) {
    return std::format("{:.{}f}", value, decimals);
}

void CheckKey() {
    if (gKeyChecked || gKey.empty()) {
        return;
    }
    gKeyChecked = true;
    gKeyCalibrated = !ReadIni(Section(gKey), "time").empty();
    gKeyPrompted = cal::ListHas(ReadIni("Calibration", "prompted"), gKey);
    std::string const when = ReadIni(Section(gKey), "time");
    log::Info("calibration: controllers {} (section [{}]): {}{}", gLabel, Section(gKey),
        gKeyCalibrated ? "calibrated " + when : std::string("not calibrated"), gKeyPrompted ? ", prompted before" : "");
}

void UpdateKey() {
    std::string const key = gProfile.empty() ? std::string() : cal::ProfileKey(gProfile, gSystem);
    if (key != gKey) {
        gKey = key;
        gLabel = gProfile.empty() ? std::string() : cal::ProfileLabel(gProfile, gSystem);
        gKeyChecked = false;
        std::lock_guard lock(gStateMutex);
        std::snprintf(gHost.key, sizeof gHost.key, "%s", gKey.c_str());
    }
    CheckKey();
}

void RecordPrompted() {
    if (gKey.empty() || gKeyPrompted) {
        return;
    }
    std::string list = ReadIni("Calibration", "prompted");
    if (!cal::ListHas(list, gKey)) {
        list += (list.empty() ? "" : ", ") + gKey;
        WriteIni("Calibration", "prompted", list);
    }
    gKeyPrompted = true;
}

// ---- GPU: our ImGui context, two canvases, five swapchains ----------------------------------------------------

struct Canvas {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    unsigned width = 0;
    unsigned height = 0;
};

struct Gpu {
    ComPtr<ID3D11Device> device;
    ImGuiContext* imgui = nullptr;
    ImFont* font = nullptr;
    Canvas panel;
    Canvas board;
    bool failed = false;
};
Gpu gGpu;

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // the runtime's
    unsigned width = 0;
    unsigned height = 0;
    bool failed = false;
    bool filled = false;  // a static picture, already in every image
};
enum SwapchainId { kPanelChain = 0, kBoardChain, kOrbActiveChain, kOrbHeldChain, kDotChain, kChains };
std::array<Swapchain, kChains> gChains;
XrSession gSession = XR_NULL_HANDLE;
std::array<XrCompositionLayerQuad, 6> gQuads{};

bool MakeCanvas(ID3D11Device* device, unsigned width, unsigned height, Canvas& out) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // ImGui's sRGB-encoded bytes, copied raw into the SRGB swapchain
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, out.texture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(out.texture.Get(), nullptr, out.rtv.GetAddressOf()))) {
        return false;
    }
    out.width = width;
    out.height = height;
    return true;
}

bool EnsureGpu(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (gGpu.failed) {
        return false;
    }
    if (gGpu.imgui != nullptr) {
        return gGpu.device.Get() == device;
    }
    if (!MakeCanvas(device, draw::kPanelWidth, draw::kPanelHeight, gGpu.panel) ||
        !MakeCanvas(device, draw::kBoardSize, draw::kBoardSize, gGpu.board)) {
        log::Error("calibration: canvas textures failed; no calibration in the headset");
        gGpu.failed = true;
        return false;
    }
    gGpu.device = device;
    ImGuiContext* const previous = ImGui::GetCurrentContext();
    gGpu.imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(gGpu.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(draw::kPanelWidth), static_cast<float>(draw::kPanelHeight));
    std::string chosen;
    gGpu.font = draw::LoadFont(io, chosen);
    bool const ok = ImGui_ImplDX11_Init(device, context);
    ImGui::SetCurrentContext(previous);  // Halcyon's menu keeps its own
    if (!ok) {
        log::Error("calibration: ImGui's D3D11 backend didn't start; no calibration in the headset");
        gGpu.failed = true;
        return false;
    }
    log::Info("calibration: ImGui context of its own, panel {}x{}, board {}x{}, font {}", draw::kPanelWidth, draw::kPanelHeight,
        draw::kBoardSize, draw::kBoardSize, chosen);
    return true;
}

// One ImGui frame drawn by `paint` into `canvas` (the game's pipeline state put back).
template <typename Paint>
void Render(ID3D11DeviceContext* context, Canvas const& canvas, float dt, Paint const& paint) {
    ImGuiContext* const previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(gGpu.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(canvas.width), static_cast<float>(canvas.height));
    io.DeltaTime = std::clamp(dt, 1.0f / 500.0f, 0.25f);
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    paint(ImGui::GetBackgroundDrawList());
    ImGui::Render();

    ID3D11RenderTargetView* savedRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* savedDsv = nullptr;
    D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRtvs, &savedDsv);
    context->RSGetViewports(&savedViewportCount, savedViewports);
    ID3D11RenderTargetView* target = canvas.rtv.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->ClearRenderTargetView(target, clear);
    D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(canvas.width), static_cast<float>(canvas.height), 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());  // backs up and restores the rest of the pipeline
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRtvs, savedDsv);
    context->RSSetViewports(savedViewportCount, savedViewports);
    for (auto*& r : savedRtvs) {
        if (r != nullptr) {
            r->Release();
        }
    }
    if (savedDsv != nullptr) {
        savedDsv->Release();
    }
    ImGui::SetCurrentContext(previous);
}

char const* ChainName(int id) {
    switch (id) {
    case kPanelChain: return "calibration";
    case kBoardChain: return "calibration-board";
    case kOrbActiveChain: return "calibration-orb";
    case kOrbHeldChain: return "calibration-orb-held";
    case kDotChain: return "calibration-dot";
    default: return "calibration-?";
    }
}

bool EnsureChain(int id, unsigned width, unsigned height) {
    Swapchain& chain = gChains[id];
    if (chain.handle != XR_NULL_HANDLE) {
        return true;
    }
    if (chain.failed) {
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;  // as our other quads
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = xrCreateSwapchain(gSession, &info, &chain.handle);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) {
        r = xrEnumerateSwapchainImages(chain.handle, 0, &count, nullptr);
    }
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(chain.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        log::Error("calibration: {} swapchain {}x{} failed (XrResult {})", ChainName(id), width, height, static_cast<int>(r));
        if (chain.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(chain.handle);
        }
        chain = {};
        chain.failed = true;
        return false;
    }
    for (auto const& image : images) {
        chain.images.push_back(image.texture);
    }
    chain.width = width;
    chain.height = height;
    testloop::NameSwapchain(chain.handle, ChainName(id));
    log::Info("calibration: {} swapchain {}x{}, {} images", ChainName(id), width, height, count);
    return true;
}

bool Acquire(Swapchain& chain, std::uint32_t& index) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(chain.handle, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    bool const ok = XR_SUCCEEDED(xrWaitSwapchainImage(chain.handle, &wait)) && index < chain.images.size();
    if (!ok) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(chain.handle, &release);
    }
    return ok;
}

// A canvas into its swapchain (each frame it's shown).
bool Submit(ID3D11DeviceContext* context, int id, Canvas const& canvas) {
    if (!EnsureChain(id, canvas.width, canvas.height)) {
        return false;
    }
    Swapchain& chain = gChains[id];
    std::uint32_t index = 0;
    if (!Acquire(chain, index)) {
        return false;
    }
    context->CopyResource(chain.images[index], canvas.texture.Get());
    testloop::OnImageWritten(chain.handle, chain.images[index]);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(chain.handle, &release);
    return true;
}

// A picture that never changes: into every image once (the runtime keeps showing the last released).
bool EnsurePicture(ID3D11DeviceContext* context, ID3D11Device* device, int id, unsigned size, std::vector<std::uint32_t> (*make)()) {
    if (!EnsureChain(id, size, size)) {
        return false;
    }
    Swapchain& chain = gChains[id];
    if (chain.filled) {
        return true;
    }
    std::vector<std::uint32_t> const pixels = make();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = size;
    td.Height = size;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), size * 4, 0};
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(device->CreateTexture2D(&td, &data, source.GetAddressOf()))) {
        log::Error("calibration: {} picture failed", ChainName(id));
        chain.failed = true;
        return false;
    }
    for (std::size_t i = 0; i < chain.images.size(); ++i) {
        std::uint32_t index = 0;
        if (!Acquire(chain, index)) {
            chain.failed = true;
            return false;
        }
        context->CopyResource(chain.images[index], source.Get());
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(chain.handle, &release);
    }
    testloop::SetStaticImage(chain.handle, size, size, pixels);
    chain.filled = true;
    return true;
}

std::vector<std::uint32_t> OrbActive() {
    return draw::OrbPicture(true);
}
std::vector<std::uint32_t> OrbHeld() {
    return draw::OrbPicture(false);
}

XrCompositionLayerQuad Quad(XrSpace space, int id, XrPosef const& pose, float width, float height) {
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    quad.space = space;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = gChains[id].handle;
    quad.subImage.imageRect = {{0, 0}, {static_cast<std::int32_t>(gChains[id].width), static_cast<std::int32_t>(gChains[id].height)}};
    quad.pose = pose;
    quad.size = {width, height};
    return quad;
}

// ---- The sequence -----------------------------------------------------------------------------------------

std::uint64_t Now() {
    return GetTickCount64();
}

void Tick(int hand) {
    halcyon::RequestHaptic(hand, kTickSeconds, kTickAmplitude);
}

void Say(std::string text, bool warn = false) {
    gRun.message = std::move(text);
    gRun.warn = warn;
    gRun.messageUntil = Now() + kMessageMs;
}

void Captured(std::initializer_list<int> hands) {
    for (int hand : hands) {
        Tick(hand);
    }
    gRun.flashAt = Now();
}

void Enter(Step step, XrPosef const& head) {
    gRun.step = step;
    gRun.still.Reset();
    gRun.held = 0.0f;
    gRun.status.clear();
    gRun.stepAnchor = {head.position, gRun.anchor.yaw};
    if (step == Step::PistolAim || step == Step::RifleAim) {
        gRun.target = 0;
    }
    if (step == Step::PistolAim) {
        gRun.pistolSamples.clear();
    }
    if (step == Step::RifleAim) {
        gRun.rifleSamples.clear();
    }
    if (step == Step::PistolGrab || step == Step::RifleGrab) {
        gRun.rightHeld = gRun.leftHeld = false;
    }
    log::Info("calibration: step {}", StepName(step));
}

// After a part is done: on to the next, or back to the check after a redo.
void Next(Step step, XrPosef const& head) {
    Enter(gRun.returnToCheck && step != Step::TPose && step != Step::Forward && step != Step::Crouch &&
                  step != Step::PistolAim && step != Step::RifleAim
              ? Step::Check
              : step,
        head);
}

bool Squeezed(vr::controllers::Hand const& hand, bool held) {
    return held ? hand.squeeze > cal::kSqueezeOff : hand.squeeze >= cal::kSqueezeOn;
}

float Distance(XrVector3f const& a, XrVector3f const& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// One frame of holding still for the step; true once held long enough.
bool Hold(XrPosef const& head, bool right, bool left, cal::StillSettings const& settings, float dt) {
    XrPosef const poses[cal::Stillness::kMaxPoses] = {head, gControllers.right.grip, gControllers.left.grip};
    bool const used[cal::Stillness::kMaxPoses] = {true, right, left};
    gRun.held = gRun.still.Update(poses, used, dt, settings);
    gRun.status = gRun.held > 0.05f ? "Hold still" : "Hold still...";
    return gRun.held >= settings.seconds;
}

void NotNow(std::string status) {
    gRun.still.Reset();
    gRun.held = 0.0f;
    gRun.status = std::move(status);
}

std::string Degrees(float value) {
    return std::format("{:.1f} degrees", value);
}

void SolveAgain() {
    if (gRun.pistolSamples.size() == static_cast<std::size_t>(cal::kTargets)) {
        gRun.pistol = cal::Solve(gRun.pistolSamples, gRun.eye);
    }
    if (gRun.rifleSamples.size() == static_cast<std::size_t>(cal::kTargets)) {
        gRun.rifle = cal::Solve(gRun.rifleSamples, gRun.eye);
    }
}

void LogStance(char const* name, cal::Stance const& s) {
    log::Info("calibration: {}: {} eye ({}), spread {:.2f} deg, trim ({}) in the right grip, sight line {} m from the grip (grip "
              "frame), eye relief {:.2f} m{}",
        name, s.eye == 0 ? "left" : "right", s.eyeMeasured ? "measured" : "chosen", s.spreadDeg, cal::FormatRotation(s.trim),
        cal::FormatVector(s.line), s.eyeRelief,
        s.hasLeft ? std::format("; foregrip {} m (gun frame), hands line {} (spread {:.2f} deg), left trim ({}), stock {} m",
                        cal::FormatVector(s.foregrip), cal::FormatVector(s.handsLine), s.handsSpreadDeg, cal::FormatRotation(s.leftTrim),
                        cal::FormatVector(s.stock))
                  : std::string());
}

// Three aims taken: solved, or once more if they disagree.
void FinishAims(bool rifle, XrPosef const& head) {
    std::vector<cal::AimSample>& samples = rifle ? gRun.rifleSamples : gRun.pistolSamples;
    cal::Stance const s = cal::Solve(samples, gRun.eye);
    char const* name = rifle ? "rifle" : "pistol";
    if (!s.valid) {
        log::Warn("calibration: the {} aims didn't solve: again", name);
        Say("Those aims couldn't be measured: once more, please.", true);
        Enter(rifle ? Step::RifleAim : Step::PistolAim, head);
        return;
    }
    LogStance(name, s);
    if (s.spreadDeg > cal::kMaxSpreadDeg) {
        log::Info("calibration: the {} aims were {:.1f} degrees apart (more than {:.0f}): again", name, s.spreadDeg, cal::kMaxSpreadDeg);
        Say(std::format("Those three aims were {} apart: once more, please, the same way each time.", Degrees(s.spreadDeg)), true);
        Enter(rifle ? Step::RifleAim : Step::PistolAim, head);
        return;
    }
    (rifle ? gRun.rifle : gRun.pistol) = s;
    Say(std::format("{}: three aims within {}.", rifle ? "Rifle" : "Pistol", Degrees(s.spreadDeg)));
    Next(rifle ? Step::Check : Step::RifleGrab, head);
}

cal::AimSample Sample(XrPosef const& head, XrVector3f const eyes[2], bool left) {
    cal::AimSample s;
    s.head = gRun.still.Average(0);
    s.right = gRun.still.Average(1);
    s.hasLeft = left;
    if (left) {
        s.left = gRun.still.Average(2);
    }
    // The eyes as they were this frame, moved with the head's average.
    XrVector3f const shift{s.head.position.x - head.position.x, s.head.position.y - head.position.y, s.head.position.z - head.position.z};
    for (int e = 0; e < 2; ++e) {
        s.eyes[e] = {eyes[e].x + shift.x, eyes[e].y + shift.y, eyes[e].z + shift.z};
    }
    s.target = cal::TargetPosition(gRun.anchor, gRun.target);
    return s;
}

void StepAim(bool rifle, XrPosef const& head, XrVector3f const eyes[2], float dt) {
    vr::controllers::Hand const& right = gControllers.right;
    vr::controllers::Hand const& left = gControllers.left;
    gRun.rightHeld = gControllers.active && Squeezed(right, gRun.rightHeld);
    if (rifle) {
        gRun.leftHeld = gControllers.active && Squeezed(left, gRun.leftHeld);
    }
    if (!gRun.rightHeld || (rifle && !gRun.leftHeld)) {
        Say(rifle ? "Keep both grips squeezed: take the rifle again." : "Keep the grip squeezed: take the pistol again.", true);
        Enter(rifle ? Step::RifleGrab : Step::PistolGrab, head);
        return;
    }
    XrVector3f const target = cal::TargetPosition(gRun.anchor, gRun.target);
    std::string const which = std::format("Target {} of {}", gRun.target + 1, cal::kTargets);
    bool const tracked = right.gripValid && (!rifle || left.gripValid);
    bool aimed = false;
    if (tracked) {
        aimed = rifle ? cal::OnTarget(head, left.grip.position, target, 20.0f, 0.25f, 0.25f) &&
                    Distance(right.grip.position, head.position) <= 0.45f
                      : cal::OnTarget(head, right.grip.position, target);
    }
    if (!aimed) {
        NotNow(which + (tracked ? ": aim at the orange target" : ": the controllers aren't tracked"));
        return;
    }
    cal::StillSettings settings;
    settings.seconds = kHoldSeconds;
    if (!Hold(head, true, rifle, settings, dt)) {
        gRun.status = which + ": hold still";
        return;
    }
    std::vector<cal::AimSample>& samples = rifle ? gRun.rifleSamples : gRun.pistolSamples;
    samples.push_back(Sample(head, eyes, rifle));
    if (rifle) {
        Captured({0, 1});
    } else {
        Captured({1});
    }
    log::Info("calibration: {} aim {} taken", rifle ? "rifle" : "pistol", samples.size());
    gRun.still.Reset();
    gRun.held = 0.0f;
    if (static_cast<int>(samples.size()) >= cal::kTargets) {
        FinishAims(rifle, head);
    } else {
        ++gRun.target;
    }
}

void StepGrab(bool rifle, XrPosef const& head) {
    vr::controllers::Hand const& right = gControllers.right;
    vr::controllers::Hand const& left = gControllers.left;
    XrVector3f const rear = cal::OrbRest(gRun.stepAnchor, rifle ? cal::Orb::RifleRear : cal::Orb::Pistol);
    XrVector3f const front = cal::OrbRest(gRun.stepAnchor, cal::Orb::RifleFront);
    // A grip takes its orb when squeezed within reach of it; it keeps it while squeezed.
    if (gRun.rightHeld) {
        gRun.rightHeld = gControllers.active && Squeezed(right, true);
    } else if (gControllers.active && right.gripValid && Distance(right.grip.position, rear) <= cal::kGrabReach &&
               Squeezed(right, false)) {
        gRun.rightHeld = true;
        Tick(1);
    }
    if (rifle) {
        if (gRun.leftHeld) {
            gRun.leftHeld = gControllers.active && Squeezed(left, true);
        } else if (gControllers.active && left.gripValid && Distance(left.grip.position, front) <= cal::kGrabReach &&
                   Squeezed(left, false)) {
            gRun.leftHeld = true;
            Tick(0);
        }
    }
    gRun.held = 0.0f;
    if (gRun.rightHeld && (!rifle || gRun.leftHeld)) {
        Captured({});
        Enter(rifle ? Step::RifleAim : Step::PistolAim, head);
        return;
    }
    gRun.status = rifle ? (gRun.rightHeld ? "Now the far orb with your left grip"
                                          : gRun.leftHeld ? "Now the near orb with your right grip"
                                                          : "Squeeze each grip at its orb")
                        : "Squeeze the right grip at the orb";
}

void StepBody(XrPosef const& head, float dt) {
    vr::floor::State floor;
    halcyon::Floor(&floor);
    vr::controllers::Hand const& right = gControllers.right;
    vr::controllers::Hand const& left = gControllers.left;
    bool const hands = gControllers.active && right.gripValid && left.gripValid;
    cal::StillSettings settings;
    settings.seconds = kHoldSeconds;
    switch (gRun.step) {
    case Step::Stand: {
        if (!cal::LookingAhead(head.orientation)) {
            NotNow("Look straight ahead");
            return;
        }
        if (!Hold(head, false, false, settings, dt)) {
            return;
        }
        XrPosef const avg = gRun.still.Average(0);
        gRun.anchor = {avg.position, cal::HeadingOf(avg.orientation)};
        gRun.anchored = true;
        gRun.haveStand = true;
        gRun.standHeadY = avg.position.y;
        gRun.standEye = floor.valid ? std::optional<float>(floor.eyeHeight) : std::nullopt;
        Captured({0, 1});
        log::Info("calibration: standing: eyes at ({:.2f}, {:.2f}, {:.2f}), heading {:.1f} deg, {}", avg.position.x, avg.position.y,
            avg.position.z, gRun.anchor.yaw * 57.29578f,
            gRun.standEye ? std::format("{:.3f} m above the {} floor", *gRun.standEye, vr::floor::Name(floor.source))
                          : std::string("no floor height (the runtime offers no floor space)"));
        Say(gRun.standEye ? std::format("Standing eye height {:.2f} m.", *gRun.standEye)
                          : std::string("No floor from the runtime: the crouch will use how far your head drops."));
        Next(Step::TPose, head);
        return;
    }
    case Step::TPose: {
        if (!hands || !cal::TPose(head.position, left.grip.position, right.grip.position)) {
            NotNow(hands ? "Both arms straight out, level with your shoulders" : "Both controllers must be tracked");
            return;
        }
        if (!Hold(head, true, true, settings, dt)) {
            return;
        }
        gRun.tHead = gRun.still.Average(0).position;
        gRun.tRight = gRun.still.Average(1).position;
        gRun.tLeft = gRun.still.Average(2).position;
        gRun.tSpan = Distance(gRun.tLeft, gRun.tRight);
        gRun.haveT = true;
        Captured({0, 1});
        log::Info("calibration: T pose: grips {:.3f} m apart", gRun.tSpan);
        Say(std::format("Arms {:.2f} m from hand to hand.", gRun.tSpan));
        Next(Step::Forward, head);
        return;
    }
    case Step::Forward: {
        XrVector3f const forward = cal::AnchorForward(gRun.anchor);
        if (!hands || !cal::ArmsForward(head.position, left.grip.position, right.grip.position, forward)) {
            NotNow(hands ? "Both arms straight ahead, level with your shoulders" : "Both controllers must be tracked");
            return;
        }
        if (!Hold(head, true, true, settings, dt)) {
            return;
        }
        float const width = cal::ShoulderWidth(gRun.still.Average(2).position, gRun.still.Average(1).position, forward);
        gRun.shoulderWidth = width;
        gRun.armLength = gRun.haveT ? cal::ArmLength(gRun.tSpan, width) : std::nullopt;
        Captured({0, 1});
        log::Info("calibration: arms forward: shoulders {:.3f} m; arm length {}", width,
            gRun.armLength ? std::format("{:.3f} m", *gRun.armLength) : std::string("not measured (no T pose, or implausible)"));
        Say(gRun.armLength ? std::format("Arms {:.2f} m, shoulders {:.2f} m.", *gRun.armLength, width)
                           : std::format("Shoulders {:.2f} m.", width));
        Next(Step::Crouch, head);
        return;
    }
    case Step::Crouch: {
        bool const useFloor = floor.valid && gRun.standEye.has_value();
        bool const down = useFloor ? cal::Crouched(*gRun.standEye, floor.eyeHeight)
                                   : gRun.haveStand && cal::Crouched(gRun.standHeadY, head.position.y);
        if (!gRun.haveStand) {
            NotNow("Needs step 1 (stand tall): skip, or redo the body");
            return;
        }
        if (!down) {
            NotNow("Lower");
            return;
        }
        cal::StillSettings crouch = settings;
        crouch.position = 0.04f;
        crouch.angleDeg = 6.0f;
        if (!Hold(head, false, false, crouch, dt)) {
            return;
        }
        float const drop = gRun.standHeadY - gRun.still.Average(0).position.y;
        gRun.crouchDrop = drop;
        gRun.crouchEye = useFloor ? std::optional<float>(floor.eyeHeight)
                                  : gRun.standEye ? std::optional<float>(*gRun.standEye - drop) : std::nullopt;
        Captured({0, 1});
        log::Info("calibration: crouched {:.3f} m below standing{}", drop,
            gRun.crouchEye ? std::format(", eyes {:.3f} m above the floor", *gRun.crouchEye) : std::string());
        Say(gRun.crouchEye ? std::format("Crouched eye height {:.2f} m.", *gRun.crouchEye) : std::format("Crouched {:.2f} m down.", drop));
        Next(Step::PistolGrab, head);
        return;
    }
    default:
        return;
    }
}

// ---- Saving ------------------------------------------------------------------------------------------------

void WriteStance(std::string const& section, char const* prefix, cal::Stance const& s) {
    std::string const p = prefix;
    WriteIni(section, (p + "Trim").c_str(), cal::FormatRotation(s.trim));
    WriteIni(section, (p + "Line").c_str(), cal::FormatVector(s.line));
    WriteIni(section, (p + "SpreadDeg").c_str(), Fixed(s.spreadDeg, 2));
    WriteIni(section, (p + "EyeReliefM").c_str(), Fixed(s.eyeRelief));
    if (s.hasLeft) {
        WriteIni(section, (p + "Foregrip").c_str(), cal::FormatVector(s.foregrip));
        WriteIni(section, (p + "LeftTrim").c_str(), cal::FormatRotation(s.leftTrim));
        WriteIni(section, (p + "HandsLine").c_str(), cal::FormatVector(s.handsLine));
        WriteIni(section, (p + "HandsSpreadDeg").c_str(), Fixed(s.handsSpreadDeg, 2));
        WriteIni(section, (p + "Stock").c_str(), cal::FormatVector(s.stock));
    }
}

std::string ResultsLine() {
    std::string text;
    cal::Stance const& any = gRun.pistol.valid ? gRun.pistol : gRun.rifle;
    if (any.valid) {
        text += std::format("{} eye ({})", any.eye == 0 ? "left" : "right", any.eyeMeasured ? "measured" : "chosen");
    }
    if (gRun.pistol.valid) {
        text += std::format("{}pistol {:.1f} deg", text.empty() ? "" : ", ", gRun.pistol.spreadDeg);
    }
    if (gRun.rifle.valid) {
        text += std::format("{}rifle {:.1f} deg", text.empty() ? "" : ", ", gRun.rifle.spreadDeg);
    }
    if (gRun.standEye) {
        text += std::format("{}eyes {:.2f} m", text.empty() ? "" : ", ", *gRun.standEye);
    }
    if (gRun.crouchEye) {
        text += std::format(" / {:.2f} m crouched", *gRun.crouchEye);
    }
    if (gRun.armLength) {
        text += std::format("{}arms {:.2f} m", text.empty() ? "" : ", ", *gRun.armLength);
    }
    return text.empty() ? std::string("nothing measured") : text;
}

void Save() {
    if (gKey.empty()) {
        Say("The controllers' type isn't known yet: not saved.", true);
        return;
    }
    std::string const section = Section(gKey);
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (!path.empty()) {
        WritePrivateProfileStringW(Wide(section).c_str(), nullptr, nullptr, path.c_str());  // a calibration is a whole
    }
    SYSTEMTIME t{};
    GetLocalTime(&t);
    WriteIni(section, "profile", gProfile);
    WriteIni(section, "system", gSystem);
    WriteIni(section, "label", gLabel);
    if (gRun.haveStand) {
        WriteIni(section, "heading", Fixed(gRun.anchor.yaw * 57.29578f, 1));
    }
    if (gRun.standEye) {
        WriteIni(section, "standEyeM", Fixed(*gRun.standEye));
    }
    if (gRun.crouchEye) {
        WriteIni(section, "crouchEyeM", Fixed(*gRun.crouchEye));
    }
    if (gRun.crouchDrop) {
        WriteIni(section, "crouchDropM", Fixed(*gRun.crouchDrop));
    }
    if (gRun.haveT) {
        WriteIni(section, "tPoseSpanM", Fixed(gRun.tSpan));
    }
    if (gRun.shoulderWidth) {
        WriteIni(section, "shoulderWidthM", Fixed(*gRun.shoulderWidth));
    }
    if (gRun.armLength) {
        WriteIni(section, "armLengthM", Fixed(*gRun.armLength));
    }
    cal::Stance const& any = gRun.pistol.valid ? gRun.pistol : gRun.rifle;
    if (any.valid) {
        WriteIni(section, "eye", any.eye == 0 ? "left" : "right");
        WriteIni(section, "eyeMeasured", any.eyeMeasured ? "1" : "0");
    }
    if (gRun.pistol.valid) {
        WriteStance(section, "pistol", gRun.pistol);
    }
    if (gRun.rifle.valid) {
        WriteStance(section, "rifle", gRun.rifle);
    }
    // The time last: the mod reads a section with a time as a calibration.
    WriteIni(section, "time", std::format("{:04}-{:02}-{:02} {:02}:{:02}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute));
    WriteIni("Calibration", "last", gKey);
    if (ReadIni("Calibration", "use").empty()) {
        WriteIni("Calibration", "use", "1");
    }
    // The T pose as the VR hands tab's arm-length sample ([Arms] measure*): the mod measures armLengthM from it.
    if (gRun.haveT) {
        MELEVR::MevrBridge::TakeArmSample(gRun.tLeft, gRun.tRight, &gRun.tHead, "calibration");
    }
    RecordPrompted();
    gKeyCalibrated = true;
    {
        std::lock_guard lock(gStateMutex);
        ++gHost.saves;
    }
    log::Info("calibration: saved to [{}] ({}): {}", section, gLabel, ResultsLine());
}

// ---- The panel's buttons --------------------------------------------------------------------------------------

void Stop(char const* why) {
    if (gRunning.exchange(false)) {
        log::Info("calibration: stopped ({})", why);
    }
    gPaused.store(false);
}

void Press(int button, XrPosef const& head) {
    log::Info("calibration: {} pressed ({})", draw::ButtonLabel(button), StepName(gRun.step));
    switch (button) {
    case draw::kCancel:
        Stop("cancelled on the panel");
        return;
    case draw::kRestart:
        Enter(gRun.step, head);
        return;
    case draw::kSkip:
        switch (gRun.step) {
        case Step::Stand:
            gRun.anchored = true;  // keep the start's layout
            Next(Step::TPose, head);
            return;
        case Step::TPose: Next(Step::Forward, head); return;
        case Step::Forward: Next(Step::Crouch, head); return;
        case Step::Crouch: Enter(gRun.returnToCheck ? Step::Check : Step::PistolGrab, head); return;
        case Step::PistolGrab:
        case Step::PistolAim: Enter(gRun.returnToCheck ? Step::Check : Step::RifleGrab, head); return;
        case Step::RifleGrab:
        case Step::RifleAim: Enter(Step::Check, head); return;
        default: return;
        }
    case draw::kSave:
        Save();
        gRun.savedAt = Now();
        Enter(Step::Saved, head);
        return;
    case draw::kSwitchEye: {
        cal::Stance const& any = gRun.pistol.valid ? gRun.pistol : gRun.rifle;
        gRun.eye = any.valid ? 1 - any.eye : (gRun.eye == 0 ? 1 : 0);
        SolveAgain();
        Say(std::format("The sight lines now run from your {} eye.", gRun.eye == 0 ? "left" : "right"));
        log::Info("calibration: the {} eye chosen", gRun.eye == 0 ? "left" : "right");
        if (gRun.pistol.valid) {
            LogStance("pistol", gRun.pistol);
        }
        if (gRun.rifle.valid) {
            LogStance("rifle", gRun.rifle);
        }
        return;
    }
    case draw::kRedoBody:
        gRun.returnToCheck = true;
        Enter(Step::Stand, head);
        return;
    case draw::kRedoPistol:
        gRun.returnToCheck = true;
        Enter(Step::PistolGrab, head);
        return;
    case draw::kRedoRifle:
        gRun.returnToCheck = true;
        Enter(Step::RifleGrab, head);
        return;
    default:
        return;
    }
}

// Where either controller's aim ray meets the panel (right first), and a trigger click there.
Pointer Point(XrPosef const& panelPose, float width, float height, draw::PanelView const& view, int& click) {
    Pointer p;
    click = draw::kNoButton;
    vr::menu_pointer::Quad quad;
    quad.pose = panelPose;
    quad.width = width;
    quad.height = height;
    for (int hand : {1, 0}) {
        vr::controllers::Hand const& h = hand == 1 ? gControllers.right : gControllers.left;
        bool const pulled = gTrigger[hand] ? h.trigger > kTriggerOff : h.trigger >= kTriggerOn;
        bool const pressedNow = pulled && !gTrigger[hand];
        gTrigger[hand] = gControllers.active && pulled;
        if (p.on || !gControllers.active || !h.aimValid) {
            continue;
        }
        vr::menu_pointer::Hit hit;
        if (vr::menu_pointer::Intersect(quad, h.aim.position, vr::menu_pointer::AimDirection(h.aim), hit)) {
            p.on = true;
            p.hand = hand;
            p.x = hit.u * static_cast<float>(draw::kPanelWidth);
            p.y = hit.v * static_cast<float>(draw::kPanelHeight);
            p.hovered = draw::ButtonAt(view, p.x, p.y);
            if (pressedNow && p.hovered != draw::kNoButton) {
                click = p.hovered;
                halcyon::RequestHaptic(hand, 0.02f, 0.4f);
            }
        }
    }
    return p;
}

draw::PanelView PanelFor(Step step, std::uint64_t now) {
    draw::ScreenText const text = draw::TextFor(ScreenOf(step));
    draw::PanelView v;
    v.device = gLabel;
    v.step = text.pip;
    v.steps = draw::kSteps;
    v.title = text.title;
    v.body = text.body;
    v.status = gRun.status;
    v.progress = step == Step::Check || step == Step::Saved || step == Step::PistolGrab || step == Step::RifleGrab
        ? -1.0f
        : std::clamp(gRun.held / kHoldSeconds, 0.0f, 1.0f);
    if (now < gRun.messageUntil) {
        v.message = gRun.message;
        v.warn = gRun.warn;
    }
    v.time = static_cast<float>(now % 100000) / 1000.0f;
    v.flash = gRun.flashAt != 0 && now - gRun.flashAt < kFlashMs ? 1.0f - static_cast<float>(now - gRun.flashAt) / kFlashMs : 0.0f;
    if (step == Step::Check) {
        cal::Stance const& any = gRun.pistol.valid ? gRun.pistol : gRun.rifle;
        if (any.valid) {
            v.summary.push_back(std::format("Aiming eye: {} ({})", any.eye == 0 ? "left" : "right", any.eyeMeasured ? "measured" : "your choice"));
        }
        v.summary.push_back(gRun.pistol.valid ? std::format("Pistol: three aims within {}", Degrees(gRun.pistol.spreadDeg))
                                              : std::string("Pistol: skipped"));
        v.summary.push_back(gRun.rifle.valid ? std::format("Rifle: three aims within {}", Degrees(gRun.rifle.spreadDeg))
                                             : std::string("Rifle: skipped"));
        std::string body;
        if (gRun.standEye) {
            body += std::format("Eye height {:.2f} m standing", *gRun.standEye);
            if (gRun.crouchEye) {
                body += std::format(", {:.2f} m crouched", *gRun.crouchEye);
            }
        }
        if (gRun.armLength && gRun.shoulderWidth) {
            body += std::format("{}arms {:.2f} m, shoulders {:.2f} m", body.empty() ? "" : "; ", *gRun.armLength, *gRun.shoulderWidth);
        }
        if (!body.empty()) {
            v.summary.push_back(body);
        }
        v.status.clear();
        v.buttons = {draw::kRedoBody, draw::kRedoPistol, draw::kRedoRifle, draw::kRowBreak, draw::kSave};
        if (any.valid) {
            v.buttons.push_back(draw::kSwitchEye);
        }
        v.buttons.push_back(draw::kCancel);
    } else if (step == Step::Saved) {
        v.status.clear();
        v.message = "Saved for " + (gLabel.empty() ? std::string("these controllers") : gLabel) + ".";
        v.warn = false;
    } else {
        v.buttons = {draw::kSkip, draw::kRestart, draw::kCancel};
    }
    return v;
}

// ---- One frame ----------------------------------------------------------------------------------------------

std::uint64_t gLastMs = 0;

void ForgetSession(XrSession session) {
    for (Swapchain& chain : gChains) {
        chain = {};  // the old session's swapchains died with it
    }
    gSession = session;
}

unsigned PromptLayers(Frame const& frame, XrCompositionLayerQuad const** out, unsigned capacity, float dt) {
    gPromptPointed.store(false);
    bool const eligible = !gKey.empty() && gKeyChecked && !gKeyCalibrated && !gKeyPrompted && !gPrompt.decided;
    bool const visible = frame.worldFrame && frame.headValid && !frame.menuOpen && frame.gameMode == 0;
    if (!eligible) {
        gPrompt.shown = false;
        return 0;
    }
    if (!visible) {
        return 0;
    }
    gPrompt.worldSeconds += dt;
    if (gPrompt.worldSeconds < kPromptAfterSeconds || capacity == 0) {
        return 0;
    }
    if (!gPrompt.shown) {
        gPrompt.shown = true;
        gPrompt.anchor = {frame.head.position, cal::HeadingOf(frame.head.orientation)};
        log::Info("calibration: first-run prompt for {} ([{}] has no calibration)", gLabel, Section(gKey));
    }
    gPrompt.shownSeconds += dt;
    if (gPrompt.shownSeconds > kPromptTimeoutSeconds) {
        gPrompt.decided = true;
        RecordPrompted();
        log::Info("calibration: the prompt timed out (not shown again for these controllers; the VR hands tab has Calibrate)");
        return 0;
    }
    if (!EnsureGpu(frame.device, frame.context)) {
        return 0;
    }
    draw::ScreenText const text = draw::TextFor(Screen::Prompt);
    draw::PanelView view;
    view.prompt = true;
    view.device = gLabel;
    view.title = text.title;
    view.body = text.body;
    view.buttons = {draw::kCalibrateNow, draw::kDismiss};
    view.time = static_cast<float>(Now() % 100000) / 1000.0f;
    float const height = cal::kPanelWidth * static_cast<float>(draw::kPanelHeight) / static_cast<float>(draw::kPanelWidth);
    XrPosef const pose = cal::PanelPose(gPrompt.anchor, false);
    int click = draw::kNoButton;
    Pointer const p = Point(pose, cal::kPanelWidth, height, view, click);
    view.pointer = p.on;
    view.pointerX = p.x;
    view.pointerY = p.y;
    view.hovered = p.hovered;
    gPromptPointed.store(p.on);
    if (click == draw::kCalibrateNow) {
        gPrompt.decided = true;
        RecordPrompted();
        Start("the first-run prompt");
        return 0;
    }
    if (click == draw::kDismiss) {
        gPrompt.decided = true;
        RecordPrompted();
        log::Info("calibration: the prompt was dismissed (not shown again for these controllers)");
        return 0;
    }
    Render(frame.context, gGpu.panel, dt, [&](ImDrawList* dl) { draw::DrawPanel(dl, gGpu.font, view); });
    if (!Submit(frame.context, kPanelChain, gGpu.panel)) {
        return 0;
    }
    gQuads[0] = Quad(frame.appSpace, kPanelChain, pose, cal::kPanelWidth, height);
    out[0] = &gQuads[0];
    return 1;
}

} // namespace

// ---- Public ---------------------------------------------------------------------------------------------------

bool Active() {
    return gRunning.load(std::memory_order_relaxed) && !gPaused.load(std::memory_order_relaxed) && !MELEVR::Menu::IsOpen();
}

bool OwnsTriggers() {
    return gPromptPointed.load(std::memory_order_relaxed);
}

void HostStateOut(vr::calibration::HostState* out) {
    if (out == nullptr) {
        return;
    }
    std::lock_guard lock(gStateMutex);
    *out = gHost;
    out->size = sizeof(vr::calibration::HostState);
    out->flags = Active() ? vr::calibration::kCalibrating : 0u;
}

bool Running() {
    return gRunning.load();
}

void Start(char const* why) {
    gRun = {};
    gRun.startedAt = Now();
    // [Calibration] eye = left | right: the player's choice of aiming eye; auto (or nothing): measured.
    std::string const eye = ReadIni("Calibration", "eye", "auto");
    gRun.eye = eye == "left" ? 0 : eye == "right" ? 1 : -1;
    gPaused.store(false);
    gRunning.store(true);
    gPrompt.shown = false;
    log::Info("calibration: started ({}) for {} ([{}]); aiming eye {}", why, gLabel.empty() ? std::string("unknown controllers") : gLabel,
        gKey.empty() ? std::string("?") : Section(gKey), gRun.eye < 0 ? "measured" : eye);
}

void Cancel(char const* why) {
    Stop(why);
}

void SetSystem(std::string const& systemName) {
    if (systemName != gSystem) {
        gSystem = systemName;
        log::Info("calibration: headset system \"{}\"", systemName);
        UpdateKey();
    }
}

void SetProfile(std::string const& interactionProfile) {
    if (interactionProfile != gProfile) {
        gProfile = interactionProfile;
        log::Info("calibration: controllers' interaction profile {}", interactionProfile.empty() ? "(none yet)" : interactionProfile);
        UpdateKey();
    }
}

void NoteControllers(vr::controllers::State const& state) {
    gControllers = state;
}

vr::controllers::State Hidden(vr::controllers::State const& state) {
    vr::controllers::State hidden;
    hidden.sequence = state.sequence;
    hidden.timeMs = state.timeMs;
    hidden.active = false;
    return hidden;
}

void Recentered() {
    if (gRunning.load()) {
        gRun.reanchor = true;
    }
    gPrompt.shown = false;  // placed again in front of the head
}

unsigned Layers(Frame const& frame, XrCompositionLayerQuad const** out, unsigned capacity) {
    if (frame.session != gSession) {
        ForgetSession(frame.session);
    }
    std::uint64_t const now = Now();
    float const dt = gLastMs == 0 ? 1.0f / 72.0f : std::clamp(static_cast<float>(now - gLastMs) / 1000.0f, 0.0f, 0.25f);
    gLastMs = now;
    UpdateKey();
    if (frame.session == XR_NULL_HANDLE || frame.context == nullptr || frame.device == nullptr || out == nullptr) {
        return 0;
    }
    if (!gRunning.load()) {
        gPaused.store(false);
        return PromptLayers(frame, out, capacity, dt);
    }
    gPromptPointed.store(false);
    bool const paused = frame.menuOpen || !frame.worldFrame || !frame.headValid;
    if (paused != gPaused.load()) {
        log::Info("calibration: {}", paused ? "paused (Halcyon's menu, or no head-tracked world)" : "resumed");
    }
    gPaused.store(paused);
    if (paused) {
        return 0;
    }
    XrPosef const& head = frame.head;
    if (!gRun.anchored || gRun.reanchor) {
        // At the start: in front of the head (step 1 refines it). After a recenter: again in front of the head. The
        // aims taken so far stay good (each is consistent in itself); a body step starts again, and without a floor
        // the standing height (in the old space) is lost, so the body starts over at step 1.
        gRun.anchor = {head.position, cal::HeadingOf(head.orientation)};
        gRun.anchored = true;
        gRun.stepAnchor = {head.position, gRun.anchor.yaw};
        if (gRun.reanchor) {
            gRun.reanchor = false;
            bool const body = gRun.step == Step::Stand || gRun.step == Step::TPose || gRun.step == Step::Forward || gRun.step == Step::Crouch;
            if (body && gRun.haveStand && !gRun.standEye) {
                gRun.haveStand = false;
                log::Info("calibration: recentered: laid out again in front of the head; no floor, so the body starts over");
                Enter(Step::Stand, head);
            } else {
                log::Info("calibration: recentered: laid out again in front of the head");
                if (body) {
                    Enter(gRun.step, head);
                }
            }
        }
    }

    // The step.
    switch (gRun.step) {
    case Step::Stand:
    case Step::TPose:
    case Step::Forward:
    case Step::Crouch: StepBody(head, dt); break;
    case Step::PistolGrab: StepGrab(false, head); break;
    case Step::PistolAim: StepAim(false, head, frame.eyes, dt); break;
    case Step::RifleGrab: StepGrab(true, head); break;
    case Step::RifleAim: StepAim(true, head, frame.eyes, dt); break;
    case Step::Check: break;
    case Step::Saved:
        if (now - gRun.savedAt >= kSavedMs) {
            Stop("saved");
            return 0;
        }
        break;
    }
    if (!gRunning.load()) {
        return 0;
    }

    if (!EnsureGpu(frame.device, frame.context)) {
        return 0;
    }
    Step const step = gRun.step;
    bool const aiming = step == Step::PistolAim || step == Step::RifleAim || step == Step::Check;
    float const panelHeight = cal::kPanelWidth * static_cast<float>(draw::kPanelHeight) / static_cast<float>(draw::kPanelWidth);
    XrPosef const panelPose = cal::PanelPose(gRun.anchor, aiming);
    draw::PanelView view = PanelFor(step, now);
    int click = draw::kNoButton;
    Pointer const p = Point(panelPose, cal::kPanelWidth, panelHeight, view, click);
    view.pointer = p.on;
    view.pointerX = p.x;
    view.pointerY = p.y;
    view.hovered = p.hovered;
    if (click != draw::kNoButton) {
        Press(click, head);
        if (!gRunning.load()) {
            return 0;
        }
        view = PanelFor(gRun.step, now);  // the press may have changed the step
        view.pointer = p.on;
        view.pointerX = p.x;
        view.pointerY = p.y;
    }

    unsigned count = 0;
    auto const add = [&](XrCompositionLayerQuad const& quad) {
        if (count < capacity && count < gQuads.size()) {
            gQuads[count] = quad;
            out[count] = &gQuads[count];
            ++count;
        }
    };
    // The target board (aiming and checking), farthest first.
    if (gRun.step == Step::PistolAim || gRun.step == Step::RifleAim || gRun.step == Step::Check) {
        draw::BoardView board;
        board.current = gRun.step == Step::Check ? -1 : gRun.target;
        std::size_t const taken = gRun.step == Step::PistolAim ? gRun.pistolSamples.size()
            : gRun.step == Step::RifleAim                     ? gRun.rifleSamples.size()
                                                              : 0;
        for (std::size_t i = 0; i < taken && i < 3; ++i) {
            board.done[i] = true;
        }
        board.time = view.time;
        board.flash = view.flash;
        Render(frame.context, gGpu.board, dt, [&](ImDrawList* dl) { draw::DrawBoard(dl, gGpu.font, board); });
        if (Submit(frame.context, kBoardChain, gGpu.board)) {
            add(Quad(frame.appSpace, kBoardChain, cal::BoardPose(gRun.anchor), cal::kBoardSize, cal::kBoardSize));
        }
    }
    // The orbs: waiting at their rest (orange), or in the hand that holds them (pale).
    auto const orb = [&](bool held, XrVector3f const& at) {
        int const id = held ? kOrbHeldChain : kOrbActiveChain;
        if (EnsurePicture(frame.context, frame.device, id, draw::kOrbPixels, held ? &OrbHeld : &OrbActive)) {
            float const pulse = held ? 1.0f : 1.0f + 0.08f * std::sin(view.time * 5.0f);
            add(Quad(frame.appSpace, id, cal::Billboard(at, head.position), cal::kOrbSize * pulse, cal::kOrbSize * pulse));
        }
    };
    vr::controllers::Hand const& right = gControllers.right;
    vr::controllers::Hand const& left = gControllers.left;
    if (step == Step::PistolGrab || step == Step::PistolAim) {
        bool const inHand = gRun.rightHeld && right.gripValid;
        orb(inHand, inHand ? right.grip.position : cal::OrbRest(gRun.stepAnchor, cal::Orb::Pistol));
    } else if (step == Step::RifleGrab || step == Step::RifleAim) {
        bool const rightIn = gRun.rightHeld && right.gripValid;
        bool const leftIn = gRun.leftHeld && left.gripValid;
        orb(rightIn, rightIn ? right.grip.position : cal::OrbRest(gRun.stepAnchor, cal::Orb::RifleRear));
        orb(leftIn, leftIn ? left.grip.position : cal::OrbRest(gRun.stepAnchor, cal::Orb::RifleFront));
    }
    // The check: a red dot where the new sight line meets the board (rifle with both grips, pistol with the right).
    if (step == Step::Check && gControllers.active && right.gripValid) {
        bool const both = Squeezed(right, false) && Squeezed(left, false) && left.gripValid;
        cal::Stance const* stance = both && gRun.rifle.valid ? &gRun.rifle
            : Squeezed(right, false) && gRun.pistol.valid  ? &gRun.pistol
                                                           : nullptr;
        if (stance != nullptr && EnsurePicture(frame.context, frame.device, kDotChain, draw::kDotPixels, &draw::DotPicture)) {
            cal::Line const line = cal::SightLine(right.grip, *stance);
            XrPosef const board = cal::BoardPose(gRun.anchor);
            XrVector3f const at = cal::OnPlane(line, board.position, cal::AnchorForward(gRun.anchor), 0.5f, cal::kBoardDistance);
            add(Quad(frame.appSpace, kDotChain, cal::Billboard(at, head.position), cal::kDotSize, cal::kDotSize));
        }
    }
    // The panel, last: on top.
    Render(frame.context, gGpu.panel, dt, [&](ImDrawList* dl) { draw::DrawPanel(dl, gGpu.font, view); });
    if (Submit(frame.context, kPanelChain, gGpu.panel)) {
        add(Quad(frame.appSpace, kPanelChain, panelPose, cal::kPanelWidth, panelHeight));
    }

    // The menu tab's summary.
    {
        std::lock_guard lock(gStateMutex);
        gSummary.step = StepName(gRun.step);
    }
    return count;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    std::string const anchor = std::format("{{\"eyes\": [{}, {}, {}], \"yawDeg\": {}}}", json::JsonNumber(gRun.anchor.eyes.x),
        json::JsonNumber(gRun.anchor.eyes.y), json::JsonNumber(gRun.anchor.eyes.z), json::JsonNumber(gRun.anchor.yaw * 57.29578f));
    auto const stance = [](cal::Stance const& s) {
        if (!s.valid) {
            return std::string("null");
        }
        return std::format("{{\"eye\": {}, \"spreadDeg\": {}, \"trim\": [{}], \"line\": [{}], \"eyeReliefM\": {}, \"hasLeft\": {}}}",
            s.eye, json::JsonNumber(s.spreadDeg), cal::FormatRotation(s.trim), cal::FormatVector(s.line), json::JsonNumber(s.eyeRelief),
            s.hasLeft ? "true" : "false");
    };
    return std::format("{{\"running\": {}, \"active\": {}, \"step\": {}, \"target\": {}, \"held\": {}, \"anchor\": {}, \"key\": {}, "
                       "\"pistolSamples\": {}, \"rifleSamples\": {}, \"pistol\": {}, \"rifle\": {}, \"rightHeld\": {}, \"leftHeld\": {}, "
                       "\"prompt\": {}}}",
        gRunning.load() ? "true" : "false", Active() ? "true" : "false", json::JsonString(StepName(gRun.step)), gRun.target,
        json::JsonNumber(gRun.held), anchor, json::JsonString(gKey), gRun.pistolSamples.size(), gRun.rifleSamples.size(),
        stance(gRun.pistol), stance(gRun.rifle), gRun.rightHeld ? "true" : "false", gRun.leftHeld ? "true" : "false",
        gPrompt.shown ? "true" : "false");
}

Summary GetSummary() {
    Summary s;
    s.running = gRunning.load();
    {
        std::lock_guard lock(gStateMutex);
        s.step = gSummary.step;
    }
    s.device = gLabel;
    s.key = gKey;
    s.calibrated = gKeyCalibrated;
    if (!gKey.empty()) {
        s.when = ReadIni(Section(gKey), "time");
        if (s.calibrated) {
            std::string const eye = ReadIni(Section(gKey), "eye");
            std::string const pistol = ReadIni(Section(gKey), "pistolSpreadDeg");
            std::string const rifle = ReadIni(Section(gKey), "rifleSpreadDeg");
            std::string const stand = ReadIni(Section(gKey), "standEyeM");
            std::string const crouch = ReadIni(Section(gKey), "crouchEyeM");
            s.results = std::format("{} eye; pistol {}; rifle {}; eyes {} m, crouched {} m", eye.empty() ? "?" : eye,
                pistol.empty() ? "skipped" : pistol + " deg", rifle.empty() ? "skipped" : rifle + " deg", stand.empty() ? "?" : stand,
                crouch.empty() ? "?" : crouch);
        }
    }
    return s;
}

} // namespace mevr::host::calibration
