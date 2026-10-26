// SPDX-License-Identifier: MIT
// MEVR-Setup.exe (research/33): installs, tunes, repairs, uninstalls and launches the VR mod for Mass Effect 1 LE
// without ME3Tweaks Mod Manager. A Win32 window drawing our in-game menu's glass style with the same RmlUi, D3D11
// renderer, decorators, fonts and stylesheet (src/plugin/menu, assets/menu/main.rcss + setup.rml/.rcss); the work
// is src/install.
//
//   MEVR-Setup.exe                         the window
//   MEVR-Setup.exe --request [--wait-pid N] opened by the game after "Install after I quit": Optional content, once
//                                          the game has exited
//   MEVR-Setup.exe --game <folder>         a game folder of your choice (e.g. a fake one for testing)
//   MEVR-Setup.exe --preview <folder>      every page to PNGs, offline (no game, no window)
//   Command line (no window; --out <file> also writes the output there):
//     --status | --install [--quality headset|performance|balanced|sharp|max|keep] [--no-settings] [--ignore-version]
//     [--source Steam] [--keep-upstream (refuse instead of moving Halcyon's MELE-VR aside)] | --uninstall [--keep-settings]
//     [--remove-local-data] | --repair | --apply-settings [--quality q] | --restore-settings
//   with --game <folder> and --payload <folder> (default: payload\ next to the exe).

#include "SetupApp.hpp"

#include "core/PngFile.hpp"
#include "install/Detect.hpp"
#include "install/GameSettings.hpp"
#include "install/Installer.hpp"
#include "install/Updates.hpp"
#include "plugin/menu/MenuBackdrop.hpp"
#include "plugin/menu/MenuRenderer.hpp"
#include "plugin/menu/MenuUi.hpp"

#include <RmlUi/Core.h>

#include <Windows.h>
#include <ShObjIdl.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace detect = mevr::install::detect;

constexpr int kWidthDp = 1280;
constexpr int kHeightDp = 800;

fs::path ExePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

// The first folder that exists: next to the exe (payload\<name>, <name>), else the repository's (a development build).
fs::path FindDir(fs::path const& exeDir, wchar_t const* name, wchar_t const* repoRelative) {
    std::error_code ec;
    for (fs::path const& candidate : {exeDir / L"payload" / name, exeDir / name}) {
        if (fs::is_directory(candidate, ec)) {
            return candidate;
        }
    }
    for (fs::path dir = exeDir; !dir.empty(); dir = dir.parent_path()) {
        if (fs::is_directory(dir / repoRelative, ec)) {
            return dir / repoRelative;
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    return exeDir / name;
}

// ---- A stand-in for the game behind the glass: deep space, a nebula, a planet's limb, stars --------------------------
std::vector<std::uint32_t> SpaceScene(int size) {
    std::vector<std::uint32_t> px(static_cast<std::size_t>(size) * size);
    auto hash = [](std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    };
    auto glow = [](float x, float y, float cx, float cy, float rx, float ry) {
        float const dx = (x - cx) / rx;
        float const dy = (y - cy) / ry;
        return std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float const u = static_cast<float>(x) / static_cast<float>(size);
            float const v = static_cast<float>(y) / static_cast<float>(size);
            float r = 0.02f + 0.03f * v, g = 0.04f + 0.04f * v, b = 0.08f + 0.06f * v;
            float const nebula = glow(u, v, 0.28f, 0.34f, 0.42f, 0.30f);
            r += 0.30f * nebula * nebula;
            g += 0.10f * nebula * nebula;
            b += 0.34f * nebula;
            float const cyan = glow(u, v, 0.70f, 0.22f, 0.34f, 0.22f);
            g += 0.30f * cyan * cyan;
            b += 0.38f * cyan * cyan;
            float const warm = glow(u, v, 0.86f, 0.62f, 0.20f, 0.16f);
            r += 0.95f * warm * warm;
            g += 0.50f * warm * warm;
            b += 0.15f * warm * warm;
            // A planet's limb across the bottom, lit from the right.
            float const pdx = u - 0.42f;
            float const pdy = v - 1.55f;
            float const d = std::sqrt(pdx * pdx + pdy * pdy);
            if (d < 0.86f) {
                float const lit = std::clamp(0.5f + 0.8f * pdx, 0.0f, 1.0f);
                r = 0.05f + 0.25f * lit;
                g = 0.07f + 0.20f * lit;
                b = 0.10f + 0.16f * lit;
                float const rim = std::clamp((d - 0.84f) / 0.02f, 0.0f, 1.0f);
                r += 0.4f * rim;
                g += 0.7f * rim;
                b += 0.9f * rim;
            } else {
                std::uint32_t const h = hash(static_cast<std::uint32_t>(y * size + x));
                if ((h & 1023u) < 2u) {
                    float const s = 0.5f + static_cast<float>((h >> 10) & 255u) / 512.0f;
                    r = g = b = s;
                }
            }
            auto byte = [](float c) { return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
            px[static_cast<std::size_t>(y) * size + x] = 0xFF000000u | (byte(b) << 16) | (byte(g) << 8) | byte(r);
        }
    }
    return px;
}

// The glass's backdrop: the scene above through the menu's own backdrop passes (blurred, darkened, graded).
struct Glass {
    mevr::menu::Backdrop backdrop;
    ComPtr<ID3D11Texture2D> scene;
    ComPtr<ID3D11ShaderResourceView> sceneSrv;

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context) {
        int const size = 1024;
        std::vector<std::uint32_t> const pixels = SpaceScene(size);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = size;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), size * 4u, 0};
        if (FAILED(device->CreateTexture2D(&td, &data, scene.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(scene.Get(), nullptr, sceneSrv.GetAddressOf())) || !backdrop.Init(device, context)) {
            return false;
        }
        int const rect[4] = {0, 0, size, size};
        backdrop.Capture(context, sceneSrv.Get(), rect, size, size, false);
        mevr::menu::BackdropView view;
        view.center[2] = -1.2f;
        view.right[0] = 0.72f;
        view.up[1] = 0.45f;
        float const t = std::tan(50.0f * 3.14159265f / 180.0f);
        view.tanLeft = -t;
        view.tanRight = t;
        view.tanUp = t;
        view.tanDown = -t;
        return backdrop.Compose(context, view);
    }
};

bool MakeDevice(bool warp, ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context) {
    D3D_FEATURE_LEVEL const levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = E_FAIL;
    if (!warp) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
            device.GetAddressOf(), nullptr, context.GetAddressOf());
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
            device.GetAddressOf(), nullptr, context.GetAddressOf());
    }
    return SUCCEEDED(hr);
}

mevr::menu::UiSetup MakeUiSetup(ID3D11Device* device, fs::path const& exeDir, int width, int height) {
    mevr::menu::UiSetup setup;
    setup.device = device;
    setup.documents = FindDir(exeDir, L"menu", L"assets/menu");
    setup.document = L"setup.rml";
    setup.fontDirs = {FindDir(exeDir, L"fonts", L"third_party/fonts")};
    setup.width = width;
    setup.height = height;
    return setup;
}

std::string RepoSetting(fs::path const& exeDir) {
    wchar_t buffer[512]{};
    GetPrivateProfileStringW(L"Update", L"repo", detect::Wide(mevr::install::updates::kDefaultRepo).c_str(), buffer, 512,
        (exeDir / L"MEVR-Setup.ini").c_str());
    return detect::Utf8(buffer);
}

// ---- The offline preview ------------------------------------------------------------------------------------------
int Preview(fs::path const& output, bool warp) {
    fs::create_directories(output);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (!MakeDevice(warp, device, context)) {
        std::fprintf(stderr, "no D3D11 device\n");
        return 1;
    }
    Glass glass;
    if (!glass.Init(device.Get(), context.Get())) {
        std::fprintf(stderr, "backdrop failed\n");
        return 1;
    }
    int const w = kWidthDp;
    int const h = kHeightDp;
    double clock = 0.0;
    mevr::menu::Ui ui;
    mevr::menu::UiSetup setup = MakeUiSetup(device.Get(), ExePath().parent_path(), w, h);
    setup.clock = [&clock] { return clock; };
    setup.log = [](mevr::menu::LogLevel level, std::string const& text) {
        if (level != mevr::menu::LogLevel::Info) {
            std::printf("%s\n", text.c_str());
        }
    };
    if (!ui.Init(setup)) {
        return 1;
    }
    ui.GetRenderer()->SetExternalTexture("backdrop", glass.backdrop.View(), static_cast<int>(mevr::menu::kBackdropWidth),
        static_cast<int>(mevr::menu::kBackdropHeight));
    mevr::setup::Options options;
    options.exe = ExePath();
    mevr::setup::App app(options, {});
    if (!app.Bind(ui.Context()) || !ui.LoadDocument()) {
        return 1;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    device->CreateTexture2D(&td, nullptr, target.GetAddressOf());
    device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf());
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    device->CreateTexture2D(&td, nullptr, staging.GetAddressOf());
    ui.Context()->ProcessMouseMove(-50, -50, 0);

    struct Shot {
        char const* file;
        char const* state;
        char const* page;
        bool bottom = false;  // scrolled to the end (the log)
    };
    Shot const shots[] = {
        {"welcome-fresh", "fresh", "welcome"},
        {"welcome-installed", "installed", "welcome"},
        {"welcome-nogame", "nogame", "welcome"},
        {"welcome-oldversion", "oldversion", "welcome"},
        {"install", "fresh", "install"},
        {"install-oldversion", "oldversion", "install"},
        {"install-busy", "busy", "install"},
        {"install-done", "installed-log", "install", true},
        {"install-waiting", "waiting", "install"},
        {"content", "fresh", "content"},
        {"content-request", "content", "content"},
        {"content-confirm", "confirm-pack", "content"},
        {"settings", "installed", "settings"},
        {"settings-fresh", "fresh", "settings"},
        {"repair", "installed", "repair"},
        {"uninstall", "installed", "uninstall"},
        {"uninstall-confirm", "confirm-uninstall", "uninstall"},
        {"launch", "installed", "launch"},
    };
    for (Shot const& shot : shots) {
        app.Sample(shot.state);
        app.SetPage(shot.page);
        for (int i = 0; i < 30; ++i) {
            clock += 1.0 / 60.0;
            if (i == 2 || i == 20) {  // after the layout has caught up with the new state
                if (Rml::Element* content = ui.Document()->GetElementById("content")) {
                    content->SetScrollTop(shot.bottom ? content->GetScrollHeight() : 0.0f);
                }
            }
            ui.Render(context.Get(), rtv.Get());
        }
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w) * h);
        if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            for (int y = 0; y < h; ++y) {
                std::memcpy(&pixels[static_cast<std::size_t>(y) * w], static_cast<std::uint8_t const*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(w) * 4);
            }
            context->Unmap(staging.Get(), 0);
        }
        // The window's black behind the (premultiplied) page, as the swapchain shows it.
        for (std::uint32_t& p : pixels) {
            p |= 0xFF000000u;
        }
        fs::path const file = output / (std::string(shot.file) + ".png");
        std::string error;
        if (mevr::png::Write(file, w, h, pixels.data(), &error)) {
            std::printf("wrote %s\n", file.string().c_str());
        } else {
            std::printf("%s: %s\n", file.string().c_str(), error.c_str());
        }
    }
    ui.Shutdown();
    return 0;
}

// ---- The command line --------------------------------------------------------------------------------------------
struct Cli {
    std::string command;
    std::optional<fs::path> game;
    std::optional<fs::path> payload;
    std::optional<fs::path> out;
    std::string quality = "performance";
    std::string source;
    bool noSettings = false;
    bool ignoreVersion = false;
    bool keepSettings = false;
    bool removeLocalData = false;
    bool keepUpstream = false;
};

int RunCli(Cli const& cli) {
    std::string text;
    auto say = [&](std::string const& line) {
        text += line + "\n";
        std::printf("%s\n", line.c_str());
    };
    std::optional<fs::path> root;
    std::string source = cli.source;
    if (cli.game) {
        root = detect::RootFromPicked(*cli.game);
        source = source.empty() ? "Folder" : source;
    } else if (auto const installs = detect::FindInstalls(); !installs.empty()) {
        root = installs.front().root;
        source = source.empty() ? installs.front().source : source;
    }
    int code = 0;
    if (!root) {
        say("game: not found");
        code = 2;
    } else {
        mevr::install::Layout const layout{*root};
        mevr::install::settings::Choices choices;
        if (cli.quality == "headset") {
            if (auto const log = mevr::install::ReadFile(detect::HalcyonLogPath())) {
                if (auto const size = detect::EyeSizeFromHalcyonLog(*log)) {
                    choices.target = mevr::install::settings::TargetFor("headset", size->width, size->height);
                }
            }
        } else {
            choices.target = mevr::install::settings::TargetFor(cli.quality, 0, 0);
        }
        auto report = [&](mevr::install::Outcome const& o) {
            for (std::string const& line : o.log) {
                say("  " + line);
            }
            say(o.ok ? "ok" : "FAILED");
            code = o.ok ? 0 : 1;
        };
        if (cli.command == "install") {
            auto const payload = cli.payload ? std::optional<mevr::install::Payload>(mevr::install::Payload{*cli.payload})
                                             : mevr::install::FindPayload(ExePath().parent_path());
            if (!payload || !payload->Valid()) {
                say("payload: not found or incomplete");
                code = 1;
            } else {
                mevr::install::InstallOptions options;
                options.gameSettings = !cli.noSettings;
                options.choices = choices;
                options.setupExe = ExePath();
                options.source = source;
                options.ignoreVersion = cli.ignoreVersion;
                options.moveUpstreamAside = !cli.keepUpstream;
                report(mevr::install::Install(layout, *payload, options));
            }
        } else if (cli.command == "uninstall") {
            report(mevr::install::Uninstall(layout, cli.keepSettings, ExePath(), cli.removeLocalData));
        } else if (cli.command == "repair") {
            auto const payload = cli.payload ? std::optional<mevr::install::Payload>(mevr::install::Payload{*cli.payload})
                                             : mevr::install::FindPayload(ExePath().parent_path());
            if (!payload) {
                say("payload: not found");
                code = 1;
            } else {
                report(mevr::install::Repair(layout, *payload));
            }
        } else if (cli.command == "apply-settings") {
            report(mevr::install::ApplySettings(layout, choices));
        } else if (cli.command == "restore-settings") {
            report(mevr::install::RestoreSettings(layout));
        }
        mevr::install::Status const s = mevr::install::ReadStatus(*root, source);
        say("game: " + detect::Utf8(s.root.wstring()) + " (" + source + ")");
        say("version: " + (s.version ? s.version->ToString() : std::string("unknown")) +
            (s.versionCheck == detect::VersionCheck::Supported ? " (supported)" : " (NOT supported)"));
        say(std::string("loader: ") + detect::BinkStateText(s.bink));
        say("installed: " + (s.installed ? "yes, " + s.installedVersion + ", loader " + s.loader : std::string("no")));
        say(std::string("game settings applied: ") + (s.gameSettings ? "yes" : "no"));
        say(std::string("textures marked by MEM: ") + (s.texturesMarked ? "yes" : "no"));
        detect::XrRuntime const xr = detect::ActiveRuntime();
        say("VR runtime: " + (xr.kind.empty() ? std::string("none") : xr.kind + " (" + xr.manifest + ")"));
    }
    if (cli.out) {
        std::ofstream(*cli.out, std::ios::binary) << text;
    }
    return code;
}

// ---- The window ---------------------------------------------------------------------------------------------------
struct Window {
    HWND hwnd = nullptr;
    Rml::Context* rml = nullptr;
    mevr::setup::App* app = nullptr;
    bool quit = false;
    bool tracking = false;
};
Window gWindow;

int Modifiers() {
    int m = 0;
    if (GetKeyState(VK_CONTROL) < 0) {
        m |= Rml::Input::KM_CTRL;
    }
    if (GetKeyState(VK_SHIFT) < 0) {
        m |= Rml::Input::KM_SHIFT;
    }
    if (GetKeyState(VK_MENU) < 0) {
        m |= Rml::Input::KM_ALT;
    }
    return m;
}

Rml::Input::KeyIdentifier KeyFor(WPARAM vk) {
    switch (vk) {
    case VK_UP: return Rml::Input::KI_UP;
    case VK_DOWN: return Rml::Input::KI_DOWN;
    case VK_LEFT: return Rml::Input::KI_LEFT;
    case VK_RIGHT: return Rml::Input::KI_RIGHT;
    case VK_TAB: return Rml::Input::KI_TAB;
    case VK_RETURN: return Rml::Input::KI_RETURN;
    case VK_SPACE: return Rml::Input::KI_SPACE;
    case VK_PRIOR: return Rml::Input::KI_PRIOR;
    case VK_NEXT: return Rml::Input::KI_NEXT;
    case VK_HOME: return Rml::Input::KI_HOME;
    case VK_END: return Rml::Input::KI_END;
    default: return Rml::Input::KI_UNKNOWN;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Rml::Context* const rml = gWindow.rml;
    switch (message) {
    case WM_CLOSE:
        if (gWindow.app != nullptr && gWindow.app->Busy()) {
            if (MessageBoxW(hwnd, L"A job is still running. Close anyway?", L"MEVR Setup", MB_YESNO | MB_ICONWARNING) != IDYES) {
                return 0;
            }
        }
        gWindow.quit = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEMOVE:
        if (rml != nullptr) {
            if (!gWindow.tracking) {
                TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                gWindow.tracking = true;
            }
            rml->ProcessMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), Modifiers());
        }
        return 0;
    case WM_MOUSELEAVE:
        gWindow.tracking = false;
        if (rml != nullptr) {
            rml->ProcessMouseLeave();
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        if (rml != nullptr) {
            rml->ProcessMouseButtonDown(0, Modifiers());
        }
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (rml != nullptr) {
            rml->ProcessMouseButtonUp(0, Modifiers());
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (rml != nullptr) {
            rml->ProcessMouseWheel(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA, Modifiers());
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (rml != nullptr) {
            rml->ProcessKeyDown(KeyFor(wParam), Modifiers());
        }
        return 0;
    case WM_KEYUP:
        if (rml != nullptr) {
            rml->ProcessKeyUp(KeyFor(wParam), Modifiers());
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::optional<fs::path> PickWithDialog(HWND owner, std::wstring const& title, bool folder) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    dialog->SetTitle(title.c_str());
    if (!folder) {
        COMDLG_FILTERSPEC const filters[] = {{L"Texture packs (*.7z, *.zip, *.rar, *.mem)", L"*.7z;*.zip;*.rar;*.mem"}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filters);
    }
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    fs::path const result(path);
    CoTaskMemFree(path);
    return result;
}

int RunWindow(mevr::setup::Options options) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    fs::path const exeDir = options.exe.parent_path();
    // The size: 1280 x 800 dp at the monitor's scale, smaller if the work area is.
    HMONITOR const monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof info};
    GetMonitorInfoW(monitor, &info);
    UINT const dpi = GetDpiForSystem();
    float scale = static_cast<float>(dpi) / 96.0f;
    int const workW = info.rcWork.right - info.rcWork.left;
    int const workH = info.rcWork.bottom - info.rcWork.top;
    scale = std::min({scale, static_cast<float>(workW - 40) / kWidthDp, static_cast<float>(workH - 80) / kHeightDp});
    int const clientW = static_cast<int>(std::lround(kWidthDp * scale));
    int const clientH = static_cast<int>(std::lround(kHeightDp * scale));

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = &WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"MEVRSetup";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    DWORD const style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect{0, 0, clientW, clientH};
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    int const ww = rect.right - rect.left;
    int const wh = rect.bottom - rect.top;
    HWND const hwnd = CreateWindowExW(0, wc.lpszClassName, L"MEVR Setup - Mass Effect VR", style, info.rcWork.left + (workW - ww) / 2,
        info.rcWork.top + (workH - wh) / 2, ww, wh, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        return 1;
    }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    gWindow.hwnd = hwnd;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (!MakeDevice(false, device, context)) {
        MessageBoxW(hwnd, L"Direct3D 11 isn't available.", L"MEVR Setup", MB_ICONERROR);
        return 1;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    device.As(&dxgiDevice);
    dxgiDevice->GetAdapter(adapter.GetAddressOf());
    adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = static_cast<UINT>(clientW);
    sd.Height = static_cast<UINT>(clientH);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapchain;
    if (FAILED(factory->CreateSwapChainForHwnd(device.Get(), hwnd, &sd, nullptr, nullptr, swapchain.GetAddressOf()))) {
        MessageBoxW(hwnd, L"Couldn't create the window's swap chain.", L"MEVR Setup", MB_ICONERROR);
        return 1;
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    ComPtr<ID3D11Texture2D> backbuffer;
    ComPtr<ID3D11RenderTargetView> rtv;
    swapchain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
    device->CreateRenderTargetView(backbuffer.Get(), nullptr, rtv.GetAddressOf());

    Glass glass;
    glass.Init(device.Get(), context.Get());
    mevr::menu::Ui ui;
    mevr::menu::UiSetup setup = MakeUiSetup(device.Get(), exeDir, clientW, clientH);
    if (!ui.Init(setup)) {
        MessageBoxW(hwnd, L"The interface didn't start (its files are missing next to MEVR-Setup.exe).", L"MEVR Setup", MB_ICONERROR);
        return 1;
    }
    ui.Context()->SetDensityIndependentPixelRatio(scale);
    ui.GetRenderer()->SetExternalTexture("backdrop", glass.backdrop.View(), static_cast<int>(mevr::menu::kBackdropWidth),
        static_cast<int>(mevr::menu::kBackdropHeight));

    mevr::setup::Host host;
    host.pickFolder = [hwnd](std::wstring const& title) { return PickWithDialog(hwnd, title, true); };
    host.pickFile = [hwnd](std::wstring const& title) { return PickWithDialog(hwnd, title, false); };
    host.openUrl = [](std::string const& url) {
        if (url.rfind("https://", 0) == 0) {
            ShellExecuteW(nullptr, L"open", detect::Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    };
    host.restartAsAdmin = [hwnd] {
        std::wstring const args = GetCommandLineW();
        // Everything after the exe's own (quoted) path.
        std::wstring rest;
        if (!args.empty() && args.front() == L'"') {
            std::size_t const close = args.find(L'"', 1);
            rest = close == std::wstring::npos ? L"" : args.substr(close + 1);
        } else {
            std::size_t const space = args.find(L' ');
            rest = space == std::wstring::npos ? L"" : args.substr(space);
        }
        fs::path const exe = ExePath();
        SHELLEXECUTEINFOW sei{sizeof sei};
        sei.lpVerb = L"runas";
        sei.lpFile = exe.c_str();
        sei.lpParameters = rest.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    };
    mevr::setup::App app(options, host);
    if (!app.Bind(ui.Context()) || !ui.LoadDocument()) {
        MessageBoxW(hwnd, L"The interface's document didn't load.", L"MEVR Setup", MB_ICONERROR);
        return 1;
    }
    app.Start();
    gWindow.rml = ui.Context();
    gWindow.app = &app;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (!gWindow.quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                gWindow.quit = true;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (gWindow.quit) {
            break;
        }
        if (IsIconic(hwnd)) {
            Sleep(50);
            continue;
        }
        app.Tick();
        ui.Render(context.Get(), rtv.Get());
        swapchain->Present(1, 0);
    }
    gWindow.rml = nullptr;
    gWindow.app = nullptr;
    ui.Shutdown();
    CoUninitialize();
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // DLLs only from System32 (and those loaded by full path): never one planted next to the setup in a Downloads
    // folder. The link flag /DEPENDENTLOADFLAG does the same for the static imports.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(argv + 1, argv + argc);
    LocalFree(argv);

    Cli cli;
    mevr::setup::Options options;
    options.exe = ExePath();
    options.repo = RepoSetting(options.exe.parent_path());
    std::optional<fs::path> preview;
    bool warp = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::wstring const& a = args[i];
        auto next = [&]() -> std::wstring { return i + 1 < args.size() ? args[++i] : std::wstring(); };
        if (a == L"--preview") {
            preview = fs::path(next());
        } else if (a == L"--warp") {
            warp = true;
        } else if (a == L"--game") {
            cli.game = fs::path(next());
            options.gameRoot = cli.game;
        } else if (a == L"--payload") {
            cli.payload = fs::path(next());
        } else if (a == L"--out") {
            cli.out = fs::path(next());
        } else if (a == L"--quality") {
            cli.quality = detect::Utf8(next());
        } else if (a == L"--source") {
            cli.source = detect::Utf8(next());
        } else if (a == L"--no-settings") {
            cli.noSettings = true;
        } else if (a == L"--ignore-version") {
            cli.ignoreVersion = true;
        } else if (a == L"--keep-settings") {
            cli.keepSettings = true;
        } else if (a == L"--remove-local-data") {
            cli.removeLocalData = true;
        } else if (a == L"--keep-upstream") {
            cli.keepUpstream = true;
        } else if (a == L"--request") {
            options.request = true;
        } else if (a == L"--wait-pid") {
            options.waitPid = std::wcstoul(next().c_str(), nullptr, 10);
        } else if (a.rfind(L"--", 0) == 0) {
            std::wstring const name = a.substr(2);
            if (name == L"status" || name == L"install" || name == L"uninstall" || name == L"repair" || name == L"apply-settings" ||
                name == L"restore-settings") {
                cli.command = detect::Utf8(name);
            }
        }
    }
    if (preview || !cli.command.empty()) {
        // Output to the console we were started from, if any.
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        int const code = preview ? Preview(*preview, warp) : RunCli(cli);
        std::fflush(stdout);
        return code;
    }
    return RunWindow(options);
}
