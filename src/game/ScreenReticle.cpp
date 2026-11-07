// SPDX-License-Identifier: MIT
#include "game/ScreenReticle.hpp"

#include "core/Log.hpp"
#include "core/vr/ScreenReticle.hpp"
#include "game/CockpitProbe.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <type_traits>
#include <vector>

namespace mevr::game::screen_reticle {
namespace {
ue3::FieldRef gManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gPanels{"Engine.BioSFManager", "m_lstPanels"};
ue3::FieldRef gTag{"Engine.BioSFPanel", "nmTag"};
ue3::FieldRef gHandler{"Engine.BioSFPanel", "m_DefaultHandler"};
ue3::FieldRef gHandlers{"Engine.BioSFPanel", "ScriptHandlers"};
ue3::FieldRef gMovie{"Engine.BioSFPanel", "MovieRef"};
ue3::FieldRef gVisible{"Engine.BioSFPanel", "IsVisible"};
ue3::FieldRef gHandlerPanel{"Engine.BioSFHandler", "oPanel"};
ue3::ClassRef gReticule{"SFXGame.BioSFHandler_Reticule"};
ue3::ClassRef gPanelClass{"Engine.BioSFPanel"};
ue3::FieldRef gReticuleInfo{"SFXGame.BioSFHandler_Reticule", "m_Reticule"};
ue3::FieldRef gMode{gReticuleInfo, "m_eMode"};
ue3::FieldRef gLastMode{"SFXGame.BioSFHandler_Reticule", "m_eLastReticuleMode"};
ue3::FunctionRef gGetFloat{"Engine.BioSFPanel.GetVariableFloat"};
ue3::FunctionRef gGetBool{"Engine.BioSFPanel.GetVariableBool"};
ue3::FunctionRef gSetFloat{"Engine.BioSFPanel.SetVariableFloat"};

struct Owned {
    ue3::Object* movie = nullptr;
    float alpha = 100.0f;
};
std::map<ue3::Object*, Owned> gOwned;
std::map<ue3::Object*, std::string> gLogged;
std::string gMeshLogged;
bool gRequested = false;

template <class T>
std::optional<T> ReadVariable(ue3::Object* panel, ue3::FunctionRef& fn, wchar_t const* path) {
    ue3::Params p(ue3::runtime::Get(), fn.Get());
    ue3::memory::OwnedString name(path);  // engine allocation, never a stack-backed FString with an out parameter
    if (!p.Valid() || !name.Valid() || !p.Set("sPath", name.Get()) || !ue3::runtime::Call(panel, fn.Get(), p)) {
        return std::nullopt;
    }
    if constexpr (std::is_same_v<T, bool>) {
        return p.ReturnBool();
    } else {
        auto const value = p.Return<float>();
        return value && std::isfinite(*value) ? value : std::nullopt;
    }
}

bool SetAlpha(ue3::Object* panel, float alpha) {
    ue3::Params p(ue3::runtime::Get(), gSetFloat.Get());
    ue3::memory::OwnedString path(L"_root._alpha");
    return p.Valid() && path.Valid() && p.Set("sPath", path.Get()) && p.Set("fVar", alpha) &&
           ue3::runtime::Call(panel, gSetFloat.Get(), p);
}

bool SameMovie(ue3::Object* panel, Owned const& owned) {
    return ue3::runtime::IsAlive(panel) && ue3::IsA(panel, gPanelClass) && gMovie.Read<ue3::Object*>(panel) == owned.movie;
}

void Restore(ue3::Object* panel, Owned const& owned) {
    // Restore only our zero; a game-authored alpha written since then already belongs to the game.
    if (SameMovie(panel, owned) && ReadVariable<float>(panel, gGetFloat, L"_root._alpha") == 0.0f) {
        bool const ok = SetAlpha(panel, owned.alpha);
        log::Info("[MEVR-CROSSHAIR] restore {} alpha {} ok={}", ue3::runtime::FullName(panel), owned.alpha, ok);
    }
}

std::string Observe(bool apply) {
    if (!ue3::runtime::Ready()) return "reticles: reflection unavailable";
    auto& r = ue3::runtime::Get();
    ue3::Object* const pc = player::Controller();
    int const gameMode = player::GameMode(pc).value_or(255);
    ue3::Object* const manager = gManager.Read<ue3::Object*>(pc).value_or(nullptr);
    auto const panels = gPanels.ElementsOf(manager, 64);
    std::string report = std::format("reticles: gameMode={} VR suppression request={} (independent of aim ray)", gameMode, gRequested);
    std::map<ue3::Object*, std::string> observed;
    std::vector<ue3::Object*> kept;
    if (panels) for (std::int32_t i = 0; i < panels->count; ++i) {
        ue3::Object* const panel = *static_cast<ue3::Object**>(panels->At(i));
        if (!r.IsAlive(panel) || !ue3::IsA(panel, gPanelClass)) continue;
        auto const tag = gTag.Read<ue3::Name>(panel);
        std::string const tagText = tag ? r.ToString(*tag) : "?";
        // Include every registered handler, not only the last one to receive ProcessEvent.
        std::vector<ue3::Object*> handlers;
        if (auto* const h = gHandler.Read<ue3::Object*>(panel).value_or(nullptr); r.IsAlive(h)) handlers.push_back(h);
        if (auto const list = gHandlers.ElementsOf(panel, 64)) for (std::int32_t j = 0; j < list->count; ++j) {
            auto* const h = *static_cast<ue3::Object**>(list->At(j));
            if (r.IsAlive(h) && std::find(handlers.begin(), handlers.end(), h) == handlers.end()) handlers.push_back(h);
        }
        bool reticule = false;
        bool sniper = false;
        bool modeKnown = false;
        std::string names;
        for (auto* const h : handlers) {
            names += (names.empty() ? "" : "; ") + r.FullName(h);
            if (!ue3::IsA(h, gReticule)) continue;
            auto const mode = gMode.Read<std::uint8_t>(gReticuleInfo.Address(h));
            auto const last = gLastMode.Read<std::uint8_t>(h);
            names += std::format(" mode={} last={} oPanel={}", mode ? static_cast<int>(*mode) : -1,
                last ? static_cast<int>(*last) : -1, r.FullName(gHandlerPanel.Read<ue3::Object*>(h).value_or(nullptr)));
            if (gHandlerPanel.Read<ue3::Object*>(h) != panel) continue;
            reticule = true;
            modeKnown = modeKnown || (mode && *mode < 10);
            sniper = sniper || mode == 2 || last == 2;
        }
        auto alpha = ReadVariable<float>(panel, gGetFloat, L"_root._alpha");
        bool const hide = reticule && vr::screen_reticle::Hide(gRequested, gameMode, !modeKnown ? -1 : sniper ? 2 : 1);
        if (apply) {
            auto owned = gOwned.find(panel);
            if (owned != gOwned.end() && !SameMovie(panel, owned->second)) {
                gOwned.erase(owned);
                owned = gOwned.end();
            }
            if (hide && alpha) {
                // Keep the latest nonzero game-authored alpha. Never change IsVisible or force _visible true.
                if (*alpha != 0.0f) {
                    Owned const before{gMovie.Read<ue3::Object*>(panel).value_or(nullptr), *alpha};
                    if (SetAlpha(panel, 0.0f)) {
                        gOwned[panel] = before;
                        alpha = ReadVariable<float>(panel, gGetFloat, L"_root._alpha");
                    }
                }
                kept.push_back(panel);
            } else if (owned != gOwned.end()) {
                Restore(panel, owned->second);
                gOwned.erase(owned);
                alpha = ReadVariable<float>(panel, gGetFloat, L"_root._alpha");
            }
        }
        auto const visible = gVisible.ReadBool(panel);
        auto const rootVisible = ReadVariable<bool>(panel, gGetBool, L"_root._visible");
        auto const owned = gOwned.find(panel);
        std::string const line = std::format("tag={} panel={} movie={} handlers=[{}] IsVisible={} _root._visible={} _root._alpha={} savedAlpha={} suppress={} sniper={}",
            tagText, r.FullName(panel), r.FullName(gMovie.Read<ue3::Object*>(panel).value_or(nullptr)), names,
            visible ? (*visible ? "1" : "0") : "?", rootVisible ? (*rootVisible ? "1" : "0") : "?",
            alpha ? std::format("{:g}", *alpha) : "?", owned != gOwned.end() ? std::format("{:g}", owned->second.alpha) : "-", hide, sniper);
        report += "\n  " + line;
        observed[panel] = line;
        if (apply && (!gLogged.contains(panel) || gLogged[panel] != line)) log::Info("[MEVR-CROSSHAIR] {}", line);
    }
    if (apply) {
        for (auto it = gOwned.begin(); it != gOwned.end();) {
            if (std::find(kept.begin(), kept.end(), it->first) == kept.end()) {
                Restore(it->first, it->second);
                it = gOwned.erase(it);
            } else ++it;
        }
        for (auto const& [panel, line] : gLogged) {
            if (!observed.contains(panel)) log::Info("[MEVR-CROSSHAIR] panel removed: {}", line);
        }
        gLogged = std::move(observed);
    }
    std::string const mesh = cockpit_probe::ReticleReport();
    report += "\n  " + mesh;
    if (apply && mesh != gMeshLogged) {
        log::Info("[MEVR-CROSSHAIR] {}", mesh);
        gMeshLogged = mesh;
    }
    return report;
}
} // namespace

void Tick(bool requested) {
    if (requested != gRequested) log::Info("[MEVR-CROSSHAIR] VR hand-reticle suppression request={}", requested);
    gRequested = requested;
    (void)Observe(true);
}

std::string Command() { return Observe(false); }

void Shutdown(bool exiting) {
    if (!exiting && ue3::runtime::Ready()) {
        for (auto const& [panel, owned] : gOwned) Restore(panel, owned);
    }
    gOwned.clear();
    gLogged.clear();
    gMeshLogged.clear();
    gRequested = false;
}
} // namespace mevr::game::screen_reticle
