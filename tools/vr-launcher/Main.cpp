// SPDX-License-Identifier: MIT
// Experimental SteamVR overlay host for the SAME RmlUi/D3D11 renderer used by the in-game menu.
// It owns no scene session. Steam/EA still launch LE1, which owns its own OpenXR session.
#include "Handoff.hpp"
#include "core/PngFile.hpp"
#include "install/Detect.hpp"
#include "install/Launch.hpp"
#include "install/SteamVrSettings.hpp"
#include "plugin/menu/MenuUi.hpp"
#include <RmlUi/Core.h>
#include <openvr.h>
#include <Windows.h>
#include <TlHelp32.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace detect = mevr::install::detect;
namespace launch = mevr::install::launch;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
constexpr int kWidth = 1440, kHeight = 900;
std::ofstream gLog;
Clock::time_point gStart = Clock::now();
void Log(std::string const& text) {
    double const seconds = std::chrono::duration<double>(Clock::now() - gStart).count();
    std::printf("%8.3f %s\n", seconds, text.c_str());
    std::fflush(stdout);
    if (gLog) { gLog << seconds << " " << text << '\n'; gLog.flush(); }
}
void Require(bool value, char const* message) { if (!value) throw std::runtime_error(message); }
std::string Read(fs::path const& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
fs::path Local() {
    wchar_t value[32768]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", value, static_cast<DWORD>(std::size(value)));
    Require(n > 0 && n < std::size(value), "LOCALAPPDATA is unavailable.");
    return value;
}
struct Options {
    fs::path root, assets, fonts, steamvr, preview, capture, headsetCapture, log;
    std::string store, previewState = "ready";
    double seconds = 0;
    bool autoLaunch = false, allowRuntimeMismatch = false, exerciseInput = false;
};
Options Parse(int argc, wchar_t** argv) {
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    Options o;
    o.assets = fs::path(exe).parent_path() / L"vr-launcher/menu";
    o.fonts = fs::path(exe).parent_path() / L"vr-launcher/fonts";
    for (int i = 1; i < argc; ++i) {
        std::wstring const arg = argv[i];
        auto next = [&]() -> wchar_t const* { Require(i + 1 < argc, "Missing argument value."); return argv[++i]; };
        if (arg == L"--game-root") o.root = next();
        else if (arg == L"--store") o.store = detect::Utf8(next());
        else if (arg == L"--assets") o.assets = next();
        else if (arg == L"--fonts") o.fonts = next();
        else if (arg == L"--steamvr-root") o.steamvr = next();
        else if (arg == L"--preview") o.preview = next();
        else if (arg == L"--preview-state") o.previewState = detect::Utf8(next());
        else if (arg == L"--capture") o.capture = next();
        else if (arg == L"--capture-headset") o.headsetCapture = next();
        else if (arg == L"--log") o.log = next();
        else if (arg == L"--seconds") { o.seconds = std::stod(next()); Require(o.seconds > 0 && o.seconds <= 600, "--seconds must be between 0 and 600."); }
        else if (arg == L"--auto-launch") o.autoLaunch = true;
        else if (arg == L"--exercise-input") o.exerciseInput = true;
        else if (arg == L"--allow-runtime-mismatch") o.allowRuntimeMismatch = true; // private fake-headset QA only
        else throw std::runtime_error("Unknown option. See tools/vr-launcher/README.md.");
    }
    Require(o.store.empty() || o.store == "steam" || o.store == "ea", "--store must be steam or ea.");
    Require(!o.exerciseInput || !o.preview.empty(), "--exercise-input requires --preview; it never launches the game.");
    Require(o.previewState == "ready" || o.previewState == "focus", "--preview-state must be ready or focus.");
    if (o.log.empty()) o.log = Local() / L"MEVR/VRLauncher.log";
    return o;
}

struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    void Init(vr::IVRSystem* system) {
        ComPtr<IDXGIAdapter> adapter;
        if (system) {
            int32_t index = -1;
            system->GetDXGIOutputInfo(&index);
            ComPtr<IDXGIFactory1> factory;
            Require(index >= 0 && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
                    SUCCEEDED(factory->EnumAdapters(static_cast<UINT>(index), &adapter)), "SteamVR's graphics adapter is unavailable.");
        }
        Require(SUCCEEDED(D3D11CreateDevice(adapter.Get(), system ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_WARP,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "D3D11 device creation failed.");
        D3D11_TEXTURE2D_DESC d{};
        d.Width = kWidth; d.Height = kHeight; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Require(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &texture)) &&
                SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &target)), "Menu texture creation failed.");
    }
    void Capture(fs::path const& path) {
        CaptureTexture(texture.Get(), path);
    }
    void CaptureTexture(ID3D11Texture2D* source, fs::path const& path) {
        D3D11_TEXTURE2D_DESC d{}; source->GetDesc(&d);
        bool const bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        Require(bgra || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "Unsupported capture format.");
        d.BindFlags = 0; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        Require(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "Capture texture creation failed.");
        context->CopyResource(staging.Get(), source);
        D3D11_MAPPED_SUBRESOURCE m{};
        Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m)), "Capture map failed.");
        std::vector<std::uint32_t> pixels(static_cast<size_t>(d.Width) * d.Height);
        for (UINT y = 0; y < d.Height; ++y)
            memcpy(pixels.data() + y * d.Width, static_cast<char const*>(m.pData) + y * m.RowPitch, d.Width * 4);
        context->Unmap(staging.Get(), 0);
        if (bgra) for (auto& p : pixels) p = (p & 0xff00ff00) | ((p & 0xff) << 16) | ((p >> 16) & 0xff);
        if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
        std::string error;
        bool const written = mevr::png::Write(path, static_cast<int>(d.Width), static_cast<int>(d.Height), pixels.data(), &error);
        Require(written, error.c_str());
        Log("Texture saved to " + detect::Utf8(path.wstring()));
    }
};

struct SteamVr {
    HMODULE dll = nullptr;
    decltype(&vr::VR_ShutdownInternal) shutdown = nullptr;
    bool initialized = false;
    vr::IVRSystem* system = nullptr;
    vr::IVROverlay* overlays = nullptr;
    vr::IVRCompositor* compositor = nullptr;
    vr::VROverlayHandle_t panel = vr::k_ulOverlayHandleInvalid;
    ~SteamVr() {
        if (panel != vr::k_ulOverlayHandleInvalid && overlays) {
            overlays->HideOverlay(panel);
            overlays->ClearOverlayTexture(panel);
            overlays->DestroyOverlay(panel);
        }
        if (initialized && shutdown) shutdown();
        if (dll) FreeLibrary(dll);
    }
    void Check(vr::EVROverlayError e) {
        if (e != vr::VROverlayError_None) throw std::runtime_error(overlays->GetOverlayErrorNameFromEnum(e));
    }
    void Init(fs::path root) {
        if (root.empty()) {
            auto path = mevr::install::steamvr::RuntimePath(Read(Local() / L"openvr/openvrpaths.vrpath"));
            Require(path.has_value(), "SteamVR is not installed. Could not read openvrpaths.vrpath.");
            root = *path;
        }
        Require(root.is_absolute(), "SteamVR runtime path must be absolute.");
        if (!launch::runtime_start::SteamVrRunning()) {
            Log("Starting SteamVR before creating the menu.");
            auto plan = launch::runtime_start::PlanFor("SteamVR", detect::Utf8((root / L"steamxr_win64.json").wstring()));
            Require(launch::runtime_start::StartSteamVr(plan), "SteamVR could not start.");
            auto const until = Clock::now() + std::chrono::seconds(45);
            while (!launch::runtime_start::SteamVrRunning() && Clock::now() < until) Sleep(250);
        }
        dll = LoadLibraryExW((root / L"bin/win64/openvr_api.dll").c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        Require(dll != nullptr, "Could not load the installed SteamVR API.");
        // memcpy avoids C4191's unrelated-function-type cast warning, while preserving the exact exported signatures.
        auto Load = [&](auto& out, char const* name) { auto address = GetProcAddress(dll, name); static_assert(sizeof(out) == sizeof(address)); memcpy(&out, &address, sizeof(out)); Require(out != nullptr, name); };
        decltype(&vr::VR_InitInternal2) init = nullptr;
        decltype(&vr::VR_GetGenericInterface) get = nullptr;
        Load(init, "VR_InitInternal2"); Load(get, "VR_GetGenericInterface"); Load(shutdown, "VR_ShutdownInternal");
        vr::EVRInitError error = vr::VRInitError_None;
        init(&error, vr::VRApplication_Overlay, nullptr);
        if (error != vr::VRInitError_None) throw std::runtime_error("SteamVR overlay initialization failed: " + std::to_string(error));
        initialized = true;
        system = static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version, &error));
        Require(system && error == vr::VRInitError_None, "SteamVR system interface unavailable.");
        overlays = static_cast<vr::IVROverlay*>(get(vr::IVROverlay_Version, &error));
        Require(overlays && error == vr::VRInitError_None, "SteamVR overlay interface unavailable.");
        compositor = static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version, &error));
        Require(compositor && error == vr::VRInitError_None, "SteamVR compositor interface unavailable.");
        Check(overlays->CreateOverlay("mevr.launcher.prototype", "MEVR Launcher", &panel));
        Check(overlays->SetOverlayWidthInMeters(panel, 2.0f));
        Check(overlays->SetOverlayFlag(panel, vr::VROverlayFlags_IsPremultiplied, true));
        Check(overlays->SetOverlayFlag(panel, vr::VROverlayFlags_VisibleInDashboard, true));
        vr::HmdVector2_t const scale{{static_cast<float>(kWidth), static_cast<float>(kHeight)}};
        Check(overlays->SetOverlayMouseScale(panel, &scale));
        Interactive(true);
        auto const until = Clock::now() + std::chrono::seconds(10);
        while (!Place() && Clock::now() < until) Sleep(100);
        Require(Place(), "No tracked headset pose. Put on the headset and try again.");
    }
    bool Place() {
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
        system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, poses, vr::k_unMaxTrackedDeviceCount);
        if (!poses[0].bPoseIsValid) return false;
        auto const& h = poses[0].mDeviceToAbsoluteTracking;
        float fx = -h.m[0][2], fz = -h.m[2][2];
        float const length = std::sqrt(fx * fx + fz * fz);
        if (length > 1e-3f) { fx /= length; fz /= length; } else { fx = 0; fz = -1; }
        // Same level, yaw-only placement as plugin/MainMenu::Place: 10 cm below eye height, fixed in the room.
        vr::HmdMatrix34_t pose{{{-fz, 0, -fx, h.m[0][3] + fx * 2.0f},
                              {0, 1, 0, h.m[1][3] - 0.10f},
                              {fx, 0, -fz, h.m[2][3] + fz * 2.0f}}};
        Check(overlays->SetOverlayTransformAbsolute(panel, vr::TrackingUniverseStanding, &pose));
        return true;
    }
    void Interactive(bool value) {
        Check(overlays->SetOverlayFlag(panel, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, value));
        Check(overlays->SetOverlayInputMethod(panel, value ? vr::VROverlayInputMethod_Mouse : vr::VROverlayInputMethod_None));
    }
    void Submit(ID3D11Texture2D* image) {
        vr::Texture_t texture{image, vr::TextureType_DirectX, vr::ColorSpace_Gamma};
        Check(overlays->SetOverlayTexture(panel, &texture));
    }
    void CaptureHeadset(Graphics& g, fs::path const& path) {
        ID3D11ShaderResourceView* view = nullptr;
        auto const result = compositor->GetMirrorTextureD3D11(vr::Eye_Left, g.device.Get(), reinterpret_cast<void**>(&view));
        Require(result == vr::VRCompositorError_None && view, "Compositor mirror unavailable.");
        struct Release { vr::IVRCompositor* c; ID3D11ShaderResourceView* v; ~Release() { c->ReleaseMirrorTextureD3D11(v); } } release{compositor, view};
        ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
        ComPtr<ID3D11Texture2D> image;
        Require(SUCCEEDED(resource.As(&image)), "Compositor mirror is not a D3D11 texture.");
        g.CaptureTexture(image.Get(), path);
    }
};

DWORD GamePid(fs::path const& root) {
    HANDLE const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    DWORD found = 0;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    for (BOOL next = Process32FirstW(snapshot, &e); next; next = Process32NextW(snapshot, &e)) {
        if (_wcsicmp(e.szExeFile, L"MassEffect1.exe") != 0) continue;
        HANDLE const process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (!process) continue;
        wchar_t path[32768]{}; DWORD count = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(process, 0, path, &count) && detect::PathUnder(path, root)) found = e.th32ProcessID;
        CloseHandle(process);
        if (found) break;
    }
    CloseHandle(snapshot);
    return found;
}

// Read only bytes added by this launch. Never treat a previous run's FOCUSED line as current readiness.
struct XrLog {
    fs::path path = detect::HalcyonLogPath();
    std::uintmax_t offset = 0;
    std::string partial;
    int state = 0;
    void Start() { std::error_code ec; offset = fs::file_size(path, ec); if (ec) offset = 0; partial.clear(); state = 0; }
    void Poll() {
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec) return;
        if (size < offset) { offset = 0; state = 0; partial.clear(); }
        if (size == offset) return;
        std::ifstream f(path, std::ios::binary);
        if (!f) return;
        f.seekg(static_cast<std::streamoff>(offset));
        char buffer[16384];
        while (f.read(buffer, sizeof(buffer)) || f.gcount()) {
            auto const n = f.gcount(); offset += static_cast<std::uintmax_t>(n);
            partial.append(buffer, static_cast<size_t>(n));
            size_t end;
            while ((end = partial.find('\n')) != std::string::npos) {
                auto const line = partial.substr(0, end);
                partial.erase(0, end + 1);
                std::string const marker = "[XR] session state -> ";
                auto const at = line.find(marker);
                if (at != std::string::npos && at + marker.size() < line.size()) {
                    char const value = line[at + marker.size()];
                    if (value >= '0' && value <= '9') { state = value - '0'; Log("Game OpenXR state=" + std::to_string(state)); }
                }
            }
            if (partial.size() > 65536) partial.clear();
        }
    }
};

struct Menu {
    mevr::menu::Ui ui;
    Rml::DataModelHandle model;
    std::string title = "Ready when you are", detail = "Launch Mass Effect 1 to begin.";
    bool busy = false, failed = false, passive = false, launchRequested = false, quit = false, recenter = false;
    void Init(Graphics& g, Options const& o) {
        mevr::menu::UiSetup setup;
        setup.device = g.device.Get(); setup.documents = o.assets; setup.document = L"launcher.rml";
        setup.fontDirs = {o.fonts}; setup.width = kWidth; setup.height = kHeight;
        setup.log = [](mevr::menu::LogLevel, std::string const& line) { Log(line); };
        Require(ui.Init(setup), "Menu initialization failed.");
        auto c = ui.Context()->CreateDataModel("launcher");
        Require(static_cast<bool>(c), "Menu data model creation failed.");
        c.Bind("title", &title); c.Bind("detail", &detail); c.Bind("busy", &busy); c.Bind("failed", &failed);
        c.Bind("passive", &passive);
        c.BindEventCallback("act", [this](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& args) {
            if (args.empty()) return;
            std::string const action = args[0].Get<Rml::String>();
            Log("Menu action: " + action);
            if (action == "launch" && !busy) launchRequested = true;
            if (action == "quit") quit = true;
            if (action == "recenter") recenter = true;
        });
        model = c.GetModelHandle();
        Require(ui.LoadDocument(), "Launcher document failed to load.");
    }
    void Status(std::string t, std::string d, bool error = false) {
        title = std::move(t); detail = std::move(d); failed = error; model.DirtyAllVariables();
        Log(title + " | " + detail);
    }
    void Mouse(vr::VREvent_t const& event) {
        auto* c = ui.Context();
        if (event.eventType == vr::VREvent_MouseMove || event.eventType == vr::VREvent_MouseButtonDown || event.eventType == vr::VREvent_MouseButtonUp) {
            c->ProcessMouseMove(static_cast<int>(event.data.mouse.x), kHeight - static_cast<int>(event.data.mouse.y), 0);
            if (event.data.mouse.button == vr::VRMouseButton_Left) {
                if (event.eventType == vr::VREvent_MouseButtonDown) c->ProcessMouseButtonDown(0, 0);
                if (event.eventType == vr::VREvent_MouseButtonUp) c->ProcessMouseButtonUp(0, 0);
            }
        }
        if (event.eventType == vr::VREvent_FocusLeave) c->ProcessMouseLeave();
        if (event.eventType == vr::VREvent_Quit) quit = true;
    }
    void ExerciseInput() {
        ui.Context()->Update();
        auto* button = ui.Document()->GetElementById("launch");
        Require(button != nullptr, "Launch button missing.");
        auto const position = button->GetAbsoluteOffset(Rml::BoxArea::Border);
        auto const size = button->GetBox().GetSize(Rml::BoxArea::Border);
        vr::VREvent_t e{};
        e.data.mouse.x = position.x + size.x / 2;
        e.data.mouse.y = kHeight - (position.y + size.y / 2);
        e.data.mouse.button = vr::VRMouseButton_Left;
        e.eventType = vr::VREvent_MouseMove; Mouse(e);
        e.eventType = vr::VREvent_MouseButtonDown; Mouse(e);
        e.eventType = vr::VREvent_MouseButtonUp; Mouse(e);
        Require(launchRequested, "Overlay pointer did not activate the launch button.");
        launchRequested = false; busy = true;
        Status("Starting Mass Effect 1", "Your game is loading. This panel stays with you until VR is ready.");
        ui.Context()->Update();
        e.eventType = vr::VREvent_MouseButtonDown; Mouse(e);
        e.eventType = vr::VREvent_MouseButtonUp; Mouse(e);
        Require(!launchRequested, "A disabled launch button accepted a duplicate launch.");
        Log("Synthetic overlay pointer and duplicate-launch checks passed. No game launched.");
    }
};

int Run(Options o) {
    if (!o.log.parent_path().empty()) fs::create_directories(o.log.parent_path());
    gLog.open(o.log, std::ios::trunc);
    Log("MEVR VR launcher prototype starting; pid=" + std::to_string(GetCurrentProcessId()));
    Graphics g; // keep D3D resources alive until after OpenVR destroys its overlay
    SteamVr vr;
    bool const preview = !o.preview.empty();
    if (!preview) vr.Init(o.steamvr);
    g.Init(vr.system);
    Menu menu; menu.Init(g, o);
    if (preview) {
        if (o.exerciseInput) menu.ExerciseInput();
        if (o.previewState == "focus") {
            menu.passive = true;
            menu.Status("Your game is rendering", "If SteamVR is showing its dashboard, select Resume to enter MEVR.");
        }
        for (int i = 0; i < 3; ++i) menu.ui.Render(g.context.Get(), g.target.Get());
        g.Capture(o.preview);
        return 0;
    }
    auto const installs = detect::FindInstalls();
    if (o.root.empty() && !installs.empty()) o.root = installs[0].root;
    if (o.store.empty()) {
        for (auto const& install : installs) {
            std::error_code ec;
            if (fs::equivalent(install.root, o.root, ec)) { o.store = install.source == "Steam" ? "steam" : "ea"; break; }
        }
    }
    auto const runtime = detect::ActiveRuntime();
    Log("Game OpenXR runtime=" + runtime.kind + "; manifest=" + runtime.manifest);
    std::string blocked;
    if (!detect::IsGameRoot(o.root)) blocked = "Game not found. Start this prototype with --game-root pointing to your Legendary Edition folder.";
    else if (o.store.empty()) blocked = "Choose the launch service with --store steam or --store ea.";
    else if (!fs::is_regular_file(o.root / L"Game/ME1/Binaries/Win64/ASI/LE1MEVR.asi")) blocked = "Install MEVR with MEVR Setup before launching in VR.";
    else if (o.store == "steam" && !launch::LauncherBoots(o.root)) blocked = "Use MEVR Setup to repair the Legendary Edition launcher loader before trying this prototype.";
    else if (runtime.kind != "SteamVR" && !o.allowRuntimeMismatch) blocked = "Set SteamVR as your OpenXR runtime in SteamVR Settings, then reopen this prototype.";
    if (!blocked.empty()) { menu.busy = true; menu.Status("Setup needed", blocked, true); }
    if (o.allowRuntimeMismatch) Log("QA override: allowing a non-SteamVR registry runtime; this does not change the game's runtime.");
    menu.ui.Render(g.context.Get(), g.target.Get());
    g.context->Flush(); vr.Submit(g.texture.Get());
    vr.Check(vr.overlays->ShowOverlay(vr.panel));
    Log("Room-anchored menu visible; system laser input enabled.");
    if (!o.capture.empty()) g.Capture(o.capture);

    std::future<launch::Result> request;
    bool launched = false, released = false, focusHint = false, gameSeen = false;
    auto started = Clock::now(), releasedAt = started, nextPoll = started;
    mevr::launcher::Handoff handoff;
    XrLog xrLog;
    DWORD previousPid = 0, previousRenderer = 0, previousFocus = 0;
    bool previousDashboard = false;
    uint32_t previousFrame = 0;
    int result = 0;
    auto const visibleAt = Clock::now();
    while (!menu.quit) {
        auto const now = Clock::now();
        double const alive = std::chrono::duration<double>(now - visibleAt).count();
        if (o.seconds > 0 && alive >= o.seconds) { Log("Bounded run ended."); if (launched) result = 4; break; }
        if (!o.headsetCapture.empty() && alive >= 1) {
            try { vr.CaptureHeadset(g, o.headsetCapture); }
            catch (std::exception const& e) { Log(std::string("Headset capture failed: ") + e.what()); }
            o.headsetCapture.clear();
        }
        vr::VREvent_t event{};
        while (vr.overlays->PollNextOverlayEvent(vr.panel, &event, sizeof(event))) menu.Mouse(event);
        while (vr.system->PollNextEvent(&event, sizeof(event))) if (event.eventType == vr::VREvent_Quit) menu.quit = true;
        if (menu.quit) break;
        if (menu.recenter) { menu.recenter = false; vr.Place(); }
        if (o.autoLaunch && alive >= 2) { o.autoLaunch = false; if (!menu.busy) menu.launchRequested = true; }
        if (menu.launchRequested) {
            menu.launchRequested = false;
            if (detect::GameRunningIn(o.root)) menu.Status("Game already running", "Close the current game before starting another session.", true);
            else {
                menu.busy = true;
                menu.Status("Starting Mass Effect 1", "Your game is loading. This panel stays with you until VR is ready.");
                xrLog.Start(); started = now; launched = true;
                request = std::async(std::launch::async, [root = o.root, steam = o.store == "steam"] { return launch::StartLe1(root, steam); });
                Log("Launch requested through the shared Steam/EA launch path.");
            }
        }
        if (request.valid() && request.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto const reply = request.get(); Log("Launch request result: " + reply.message);
            if (!reply.started) { menu.Status("Launch did not start", reply.message + " Close this panel and try again from MEVR Setup.", true); launched = false; result = 3; }
        }
        if (launched && now >= nextPoll) {
            nextPoll = now + std::chrono::milliseconds(100);
            DWORD const pid = GamePid(o.root);
            DWORD const renderer = vr.compositor->GetLastFrameRenderer();
            DWORD const focus = vr.compositor->GetCurrentSceneFocusProcess();
            bool const dashboard = vr.overlays->IsDashboardVisible();
            if (pid != previousPid) xrLog.state = 0; // a replacement process cannot inherit the previous one's focus
            if (pid != previousPid || renderer != previousRenderer || focus != previousFocus || dashboard != previousDashboard) {
                Log("Handoff telemetry: game=" + std::to_string(pid) + " renderer=" + std::to_string(renderer) + " scene=" + std::to_string(focus) + " dashboard=" + std::to_string(dashboard));
                previousPid = pid; previousRenderer = renderer; previousFocus = focus; previousDashboard = dashboard;
            }
            xrLog.Poll();
            if (pid && !gameSeen) { gameSeen = true; menu.Status("Mass Effect 1 is starting", "Waiting for the game's VR frames."); }
            vr::Compositor_FrameTiming timing{}; timing.m_nSize = sizeof(timing);
            bool const frame = vr.compositor->GetFrameTiming(&timing) && timing.m_nFrameIndex != previousFrame;
            previousFrame = timing.m_nFrameIndex;
            bool const rendering = pid && renderer == pid && focus == pid && frame;
            auto phase = handoff.Step({std::chrono::duration<double>(now - started).count(), pid != 0, rendering, xrLog.state == 5, dashboard});
            if (phase == mevr::launcher::Phase::Released && !released) {
                released = true; releasedAt = now;
                vr.Interactive(false); vr.Check(vr.overlays->HideOverlay(vr.panel));
                menu.ui.Context()->ProcessMouseLeave();
                Log("Game frames confirmed. Overlay hidden and laser input released; waiting for OpenXR FOCUSED.");
            }
            if (released && !focusHint && now - releasedAt >= std::chrono::seconds(5)) {
                focusHint = true;
                menu.passive = true;
                menu.Status("Your game is rendering", "If SteamVR is showing its dashboard, select Resume to enter MEVR.");
                // Passive, lower panel: never reacquire laser focus while the game is trying to become FOCUSED.
                vr.Check(vr.overlays->ShowOverlay(vr.panel));
                Log("Showing passive focus guidance; overlay input remains disabled.");
            }
            if (phase == mevr::launcher::Phase::Complete) {
                vr.Check(vr.overlays->HideOverlay(vr.panel));
                Log("HANDOFF COMPLETE: game rendering, dashboard closed, fresh OpenXR FOCUSED confirmed.");
                break;
            }
            if (phase == mevr::launcher::Phase::Failed) {
                menu.passive = false;
                menu.Status("VR startup needs attention", pid ? "The game has not completed VR startup. Close this panel and check MEVR Setup's problem report." : "The game closed or did not start. Close this panel and check MEVR Setup's problem report.", true);
                Log("HANDOFF INCOMPLETE: timeout or game exit; no focus success claimed.");
                vr.Check(vr.overlays->ShowOverlay(vr.panel));
                vr.Interactive(true); launched = false; result = 3;
            }
        }
        if (menu.ui.ReloadIfChanged()) menu.model.DirtyAllVariables();
        menu.ui.Render(g.context.Get(), g.target.Get());
        g.context->Flush(); vr.Submit(g.texture.Get());
        vr.overlays->WaitFrameSync(33);
    }
    vr.Interactive(false); vr.Check(vr.overlays->HideOverlay(vr.panel));
    Log("Launcher closed.");
    return result;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HRESULT const com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int code = 1;
    try { code = Run(Parse(argc, argv)); }
    catch (std::exception const& e) { Log(std::string("ERROR: ") + e.what()); }
    if (SUCCEEDED(com)) CoUninitialize();
    return code;
}
