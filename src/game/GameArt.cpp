// SPDX-License-Identifier: MIT
// The game's own UI art cached for the host (game/GameArt.hpp, research/55).
#include "game/GameArt.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace mevr::game::game_art {
namespace {

namespace fs = std::filesystem;

std::string gStatus = "{\"state\": \"waiting\"}";

#if defined(MEVR_GAME_LE1)
// The movies (research/53's audit, research/55): GUI_SF_Fonts and the main wheel live in Startup_INT (always loaded); the rest
// come with their screens' packages, loaded here.
constexpr std::array<char const*, 6> kMovies = {
    "GUI_SF_Fonts.Fonts",
    "GUI_SF_InGameGui.PC_MainWheel",
    "GUI_SF_Journal.PC_Journal",
    "GUI_SF_Journal.PC_Codex",
    "GUI_SF_Inventory.PCInventory",
    "GUI_SF_CharacterRecord.PC_CharacterRecord",
};
constexpr int kWaitTicks = 90;           // into a level before the work (not during its load)
constexpr std::size_t kMaxBytes = 32u << 20;

// Our reflection layer (research/34); engine/ReflectCheck has a row for each.
ue3::ClassRef gMovieInfoClass{"GFxUI.GFxMovieInfo"};
ue3::FieldRef gRawData{"GFxUI.GFxMovieInfo", "RawData"};
ue3::FunctionRef gDynamicLoadObject{"Core.Object.DynamicLoadObject"};

bool gDone = false;
int gTicks = 0;

fs::path Folder() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return fs::path(buffer) / L"MEVR" / L"gameart";
}

// The loaded movies by their path ("GUI_SF_Fonts.Fonts"): one pass over the objects, the class tested before the name.
std::vector<std::pair<std::string, ue3::Object*>> Loaded() {
    std::vector<std::pair<std::string, ue3::Object*>> found;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gMovieInfoClass.Get();
    if (cls == nullptr) {
        return found;
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        std::string const full = r.FullName(o);  // "GFxMovieInfo GUI_SF_Fonts.Fonts"
        std::size_t const space = full.find(' ');
        if (space != std::string::npos) {
            found.emplace_back(full.substr(space + 1), o);
        }
    }
    return found;
}

ue3::Object* Load(std::string const& path) {
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || gDynamicLoadObject.Get() == nullptr || gMovieInfoClass.Get() == nullptr) {
        return nullptr;
    }
    ue3::memory::OwnedString text(ue3::memory::Widen(path));
    if (!text.Valid()) {
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gDynamicLoadObject.Get());
    if (!p.Set("ObjectName", text.Get()) || !p.Set("ObjectClass", gMovieInfoClass.Get()) || !p.SetBool("MayFail", true)) {
        return nullptr;
    }
    ue3::runtime::Call(controller, gDynamicLoadObject.Get(), p);  // the engine's own loader, on the game thread
    return p.Return<ue3::Object*>().value_or(nullptr);
}

// Writes the bytes unless a file of that size is already there. The bytes it wrote (0 when kept).
std::size_t Cache(fs::path const& file, std::uint8_t const* data, std::size_t size, std::string& error) {
    std::error_code ec;
    if (fs::exists(file, ec) && fs::file_size(file, ec) == size && !ec) {
        return 0;
    }
    fs::path const tmp = fs::path(file).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<char const*>(data), static_cast<std::streamsize>(size));
        if (!out) {
            error = "couldn't write " + ini::Utf8(tmp);
            return 0;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        error = "couldn't replace " + ini::Utf8(file) + ": " + ec.message();
        fs::remove(tmp, ec);
        return 0;
    }
    return size;
}

void Run() {
    fs::path const folder = Folder();
    std::error_code ec;
    if (folder.empty() || (fs::create_directories(folder, ec), ec)) {
        log::Warn("game art: no folder for the cache ({}): the omni-tool's rail keeps its own look", ini::Utf8(folder));
        gStatus = "{\"state\": \"no folder\"}";
        return;
    }
    std::vector<std::pair<std::string, ue3::Object*>> loaded = Loaded();
    int cached = 0, written = 0, kept = 0, loadedNow = 0;
    std::size_t bytesWritten = 0;
    std::string missing;
    for (char const* path : kMovies) {
        ue3::Object* movie = nullptr;
        for (auto const& [name, object] : loaded) {
            if (ue3::Lower(name) == ue3::Lower(path)) {
                movie = object;
            }
        }
        if (movie == nullptr) {
            movie = Load(path);
            loadedNow += movie != nullptr ? 1 : 0;
        }
        auto const* const bytes = movie != nullptr ? gRawData.In<ue3::TArray<std::uint8_t>>(movie) : nullptr;
        if (bytes == nullptr || bytes->data == nullptr || bytes->count <= 8 || static_cast<std::size_t>(bytes->count) > kMaxBytes) {
            missing += std::string(missing.empty() ? "" : ", ") + path;
            continue;
        }
        std::string error;
        std::size_t const wrote = Cache(folder / (ue3::memory::Widen(path) + L".gfx"), bytes->data, static_cast<std::size_t>(bytes->count), error);
        if (!error.empty()) {
            log::Warn("game art: {}", error);
            missing += std::string(missing.empty() ? "" : ", ") + path;
            continue;
        }
        ++cached;
        if (wrote > 0) {
            ++written;
            bytesWritten += wrote;
        } else {
            ++kept;
        }
    }
    log::Info("game art: {} of {} movies cached in {} ({} written, {:.1f} MB; {} unchanged; {} loaded for it){}", cached, kMovies.size(),
        ini::Utf8(folder), written, static_cast<double>(bytesWritten) / (1024.0 * 1024.0), kept, loadedNow,
        missing.empty() ? std::string() : "; missing: " + missing);
    gStatus = std::format("{{\"state\": \"done\", \"cached\": {}, \"written\": {}, \"missing\": \"{}\"}}", cached, written, missing);
}
#endif

} // namespace

void Tick() {
#if defined(MEVR_GAME_LE1)
    if (gDone || !ue3::runtime::Ready() || player::Controller() == nullptr) {
        return;
    }
    if (++gTicks < kWaitTicks) {
        return;
    }
    gDone = true;
    Run();
#endif
}

void Shutdown() {
#if defined(MEVR_GAME_LE1)
    gDone = false;
    gTicks = 0;
#endif
}

std::string StatusJson() {
    return gStatus;
}

} // namespace mevr::game::game_art
