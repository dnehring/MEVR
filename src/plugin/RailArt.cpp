// SPDX-License-Identifier: MIT
// The omni-tool's rail drawn with ME1's own UI art (plugin/RailArt.hpp, research/56).
#include "plugin/RailArt.hpp"

#include "core/Ini.hpp"
#include "core/gfx/Raster.hpp"
#include "core/gfx/Swf.hpp"
#include "core/gfx/SwfFont.hpp"
#include "plugin/menu/MenuRenderer.hpp"

#include <RmlUi/Core.h>

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mevr::host::rail_art {
namespace {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

constexpr wchar_t kFonts[] = L"GUI_SF_Fonts.Fonts.gfx";
constexpr wchar_t kJournal[] = L"GUI_SF_Journal.PC_Journal.gfx";
constexpr wchar_t kInventory[] = L"GUI_SF_Inventory.PCInventory.gfx";
constexpr std::uint16_t kFrameShape = 324;      // PCInventory's item-list frame (576 x 282)
constexpr float kTabSourceHeight = 41.1f;       // the journal tab's height, px (core/gfx's measure)

// Kept for the process: RmlUi reads a font face's memory for as long as it lives, and the renderer samples the views.
std::map<std::string, std::vector<std::uint8_t>> gFontBytes;
std::map<std::string, ComPtr<ID3D11ShaderResourceView>> gViews;
std::uint64_t gNextCheckMs = 0;
fs::file_time_type gStamp{};
fs::path gFolder;  // the folder the last Load read

std::optional<std::vector<std::uint8_t>> ReadFile(fs::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::optional<gfx::Movie> ReadMovie(fs::path const& path, std::string& detail) {
    auto const bytes = ReadFile(path);
    if (!bytes) {
        detail += std::format(" {} not cached;", ini::Utf8(path.filename()));
        return std::nullopt;
    }
    std::string error;
    auto movie = gfx::Movie::Parse(std::span<std::uint8_t const>(bytes->data(), bytes->size()), &error);
    if (!movie) {
        detail += std::format(" {} unreadable ({});", ini::Utf8(path.filename()), error);
    }
    return movie;
}

fs::file_time_type Newest() {
    fs::file_time_type newest{};
    std::error_code ec;
    for (wchar_t const* name : {kFonts, kJournal, kInventory}) {
        auto const t = fs::last_write_time(gFolder / name, ec);
        if (!ec && t > newest) {
            newest = t;
        }
    }
    return newest;
}

bool Upload(ID3D11Device* device, menu::Renderer* renderer, std::string const& name, gfx::Image const& image) {
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() != static_cast<std::size_t>(image.width) * image.height) {
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(image.width);
    td.Height = static_cast<UINT>(image.height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // premultiplied, sRGB-encoded bytes as the menu's own
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{image.pixels.data(), static_cast<UINT>(image.width) * 4u, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device->CreateTexture2D(&td, &data, texture.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
        return false;
    }
    renderer->SetExternalTexture(name, view.Get(), image.width, image.height);
    gViews[name] = view;  // the renderer holds the raw view: ours keeps it alive
    return true;
}

} // namespace

fs::path CacheFolder() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return fs::path(buffer) / L"MEVR" / L"gameart";
}

bool CacheChanged() {
    std::uint64_t const now = GetTickCount64();
    if (now < gNextCheckMs) {
        return false;
    }
    gNextCheckMs = now + 2000;
    return Newest() != gStamp;
}

Status Load(ID3D11Device* device, menu::Renderer* renderer, Sizes const& sizes, fs::path const& from) {
    Status status;
    gFolder = from.empty() ? CacheFolder() : from;
    gStamp = Newest();
    gNextCheckMs = GetTickCount64() + 2000;
    if (device == nullptr || renderer == nullptr) {
        status.detail = "no device";
        return status;
    }
    auto const start = std::chrono::steady_clock::now();
    fs::path const folder = gFolder;
    // The fonts: the game's three faces (their names in GUI_SF_Fonts: Biomass_jpn, Tigensu_jpn, Myriad Pro+DFGGothicP-W5).
    int registered = 0;
    if (gFontBytes.size() == 3) {
        registered = 3;
    } else if (auto const movie = ReadMovie(folder / kFonts, status.detail)) {
        struct Face {
            char const* prefix;
            char const* family;
        };
        for (Face const face : {Face{"Biomass", "ME1 Biomass"}, Face{"Tigensu", "ME1 Tigensu"}, Face{"Myriad", "ME1 Myriad"}}) {
            if (gFontBytes.contains(face.family)) {
                ++registered;
                continue;
            }
            for (gfx::FontInfo const& info : gfx::Fonts(*movie)) {
                if (!info.name.starts_with(face.prefix)) {
                    continue;
                }
                std::string error;
                auto ttf = gfx::FontAsTtf(*movie, info.id, face.family, {}, &error);
                if (!ttf) {
                    status.detail += std::format(" {} not converted ({});", info.name, error);
                    break;
                }
                std::vector<std::uint8_t>& kept = gFontBytes[face.family];
                kept = std::move(*ttf);
                Rml::Span<Rml::byte const> const span(reinterpret_cast<Rml::byte const*>(kept.data()), kept.size());
                if (Rml::LoadFontFace(span, face.family, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, false)) {
                    ++registered;
                } else {
                    gFontBytes.erase(face.family);
                    status.detail += std::format(" RmlUi refused {};", face.family);
                }
                break;
            }
        }
    }
    status.fonts = registered == 3;
    // The journal's tab in its states, fitted to the rail's buttons.
    if (auto const movie = ReadMovie(folder / kJournal, status.detail)) {
        auto const tab = movie->Exported("leftTab");
        int made = 0;
        if (tab) {
            struct State {
                char const* label;
                char const* name;
            };
            gfx::FitOptions fit;
            fit.cornerScale = static_cast<float>(sizes.tabHeight) / kTabSourceHeight;
            for (State const state : {State{"primaryNormal", "me1-tab"}, State{"primaryMouseOn", "me1-tab-hover"},
                     State{"selectedMouseOff", "me1-tab-selected"}, State{"selectedMouseOn", "me1-tab-selected-hover"}}) {
                auto const frame = movie->FrameOf(*tab, state.label);
                if (!frame) {
                    status.detail += std::format(" the tab has no '{}';", state.label);
                    continue;
                }
                gfx::Image const image = gfx::RenderFit(*movie, *tab, *frame, sizes.tabWidth, sizes.tabHeight, gfx::Insets{50, 0, 15, 0}, fit);
                made += Upload(device, renderer, state.name, image) ? 1 : 0;
            }
        } else {
            status.detail += " no `leftTab` in the journal's movie;";
        }
        status.tabs = made == 4;
    }
    // The inventory's item-list frame behind the rail: the left 262 px (the side bump and the whole title notch, about 200 x 30,
    // where the title goes) and the corners at the game's own size, the straight middle squeezed, the sides stretched.
    if (auto const movie = ReadMovie(folder / kInventory, status.detail)) {
        gfx::Image const image = gfx::RenderFit(*movie, kFrameShape, 0, sizes.panelWidth, sizes.panelHeight, gfx::Insets{262, 60, 60, 50});
        status.panel = Upload(device, renderer, "me1-panel", image);
        if (!status.panel) {
            status.detail += " the frame didn't render;";
        }
    }
    double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    status.summary = std::format("the game's art from {}: fonts {}, tab {}, frame {} ({:.0f} ms){}", ini::Utf8(folder), status.fonts ? "yes" : "no",
        status.tabs ? "yes" : "no", status.panel ? "yes" : "no", ms, status.detail.empty() ? std::string() : ";" + status.detail);
    return status;
}

} // namespace mevr::host::rail_art
