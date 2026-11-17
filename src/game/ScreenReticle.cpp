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

#if defined(MEVR_GAME_LE3)
#include "engine/GameAdapter.hpp"
#include "game/Le3Gui.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"
#endif

namespace mevr::game::screen_reticle {
#if defined(MEVR_GAME_LE3)
// ME3's crosshair (research/le3-hud-panel.md "The crosshair hidden"): not a BioSFManager panel but a movie of its own, which
// the GUI manager (SFXGUIInteraction, game/Le3Gui) keeps under its MovieTag_Reticle: SFXSFHandler_Reticle, an
// SFXGUIMovieLegacyAdapter, which keeps ME1's and ME2's panel calls by name (GetVariableFloat, SetVariableFloat on a path).
// It draws the held weapon's reticle (m_oCurrentWeaponReticle, what its GetWeaponReticleClass answers: the weapon's
// GUIReticleClass, zoomed its GUIZoomReticleClass) and the hit markers. ME1's rule (vr::screen_reticle) with ME3's gameplay
// modes: while the host asks (the hand reticle on, VR gameplay) the movie's `_root._alpha` is 0 and only our zero is given
// back. The scope view stays the game's, as ME1's sniper mode and ME2's sniper crosshair: a zoomed weapon whose zoom reticle
// is a class of its own (a sniper rifle's SFXGUI_StandardSniperZoomReticle), shown or still loading.
namespace {
ue3::ClassRef gInteraction{"SFXGame.SFXGUIInteraction", ue3::kEveryGame};
ue3::FieldRef gReticleTag{"SFXGame.SFXGUIInteraction", "MovieTag_Reticle", ue3::kEveryGame};
ue3::FunctionRef gGetMovie{"SFXGame.SFXGUIInteraction.GetMovie", ue3::kEveryGame};
ue3::ClassRef gReticleMovie{"SFXGame.SFXSFHandler_Reticle", ue3::kEveryGame};
ue3::FieldRef gCurrentReticle{"SFXGame.SFXSFHandler_Reticle", "m_oCurrentWeaponReticle", ue3::kEveryGame};
ue3::FieldRef gReticleShown{"SFXGame.SFXSFHandler_Reticle", "m_bReticleVisible", ue3::kEveryGame};
ue3::FunctionRef gGetFloat{"SFXGame.SFXGUIMovieLegacyAdapter.GetVariableFloat", ue3::kEveryGame};
ue3::FunctionRef gSetFloat{"SFXGame.SFXGUIMovieLegacyAdapter.SetVariableFloat", ue3::kEveryGame};
ue3::FunctionRef gGetVisible{"SFXGame.SFXGUIMovie.GetVisible", ue3::kEveryGame};
ue3::FieldRef gPawnWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
ue3::ClassRef gWeapon{"SFXGame.SFXWeapon", ue3::kEveryGame};
ue3::FieldRef gReticleClass{"SFXGame.SFXWeapon", "GUIReticleClass", ue3::kEveryGame};
ue3::FieldRef gZoomReticleClass{"SFXGame.SFXWeapon", "GUIZoomReticleClass", ue3::kEveryGame};
ue3::FieldRef gZoomed{"SFXGame.SFXWeapon_NativeBase", "bIsZoomed", ue3::kEveryGame};

ue3::Object* gOwnedMovie = nullptr;  // the reticle movie whose alpha we zeroed
float gOwnedAlpha = 100.0f;          // the game's alpha before our zero
std::string gLogged;
bool gRequested = false;
bool gToldMissing = false;

// The GUI manager's reticle movie for the local player, or nullptr (not open, the GUI manager not up, a lookup failed).
ue3::Object* ReticleMovie(ue3::Object* pc, std::string* why) {
    ue3::Object* const manager = le3_gui::Manager();
    if (manager == nullptr || !ue3::IsA(manager, gInteraction)) {
        if (why != nullptr) *why = "no GUI manager (BioPlayerController.GetSFXUIController)";
        return nullptr;
    }
    auto const tag = gReticleTag.Read<ue3::Name>(manager);
    ue3::Function* const fn = gGetMovie.Get();
    if (!tag || fn == nullptr) {
        if (why != nullptr) *why = "SFXGUIInteraction.MovieTag_Reticle or GetMovie didn't resolve";
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !p.Set("i_pOwningPlayer", pc) || !p.Set("nmMovieTag", *tag) || !ue3::runtime::Call(manager, fn, p)) {
        if (why != nullptr) *why = "SFXGUIInteraction.GetMovie failed";
        return nullptr;
    }
    ue3::Object* const movie = p.Return<ue3::Object*>().value_or(nullptr);
    if (!ue3::runtime::IsAlive(movie)) {
        if (why != nullptr) *why = std::format("no movie open under {}", ue3::runtime::NameText(*tag));
        return nullptr;
    }
    if (!ue3::IsA(movie, gReticleMovie)) {
        if (why != nullptr) *why = std::format("{}'s movie {} isn't an SFXSFHandler_Reticle: left alone",
            ue3::runtime::NameText(*tag), ue3::runtime::FullName(movie));
        return nullptr;
    }
    return movie;
}

std::optional<float> Alpha(ue3::Object* movie) {
    ue3::Params p(ue3::runtime::Get(), gGetFloat.Get());
    ue3::memory::OwnedString path(L"_root._alpha");  // engine allocation, never a stack-backed FString
    if (!p.Valid() || !path.Valid() || !p.Set("sPath", path.Get()) || !ue3::runtime::Call(movie, gGetFloat.Get(), p)) {
        return std::nullopt;
    }
    auto const value = p.Return<float>();
    return value && std::isfinite(*value) ? value : std::nullopt;
}

bool SetAlpha(ue3::Object* movie, float alpha) {
    ue3::Params p(ue3::runtime::Get(), gSetFloat.Get());
    ue3::memory::OwnedString path(L"_root._alpha");
    return p.Valid() && path.Valid() && p.Set("sPath", path.Get()) && p.Set("fVar", alpha) &&
           ue3::runtime::Call(movie, gSetFloat.Get(), p);
}

std::optional<bool> MovieVisible(ue3::Object* movie) {
    ue3::Params p(ue3::runtime::Get(), gGetVisible.Get());
    if (!p.Valid() || !ue3::runtime::Call(movie, gGetVisible.Get(), p)) return std::nullopt;
    return p.ReturnBool();
}

// What the movie draws now: whether that is readable (else nothing is hidden), whether it is the scope view, and the words
// for the log.
struct Shown {
    bool known = false;
    bool scope = false;
    std::string text;
};
Shown ReadShown(ue3::Object* pc, ue3::Object* movie) {
    auto& r = ue3::runtime::Get();
    Shown out;
    auto const current = gCurrentReticle.Read<ue3::Object*>(movie);
    out.known = current.has_value();
    ue3::Object* const reticle = current.value_or(nullptr);
    ue3::Object* weapon = gPawnWeapon.Read<ue3::Object*>(player::Pawn(pc)).value_or(nullptr);
    if (!r.IsAlive(weapon) || !ue3::IsA(weapon, gWeapon)) weapon = nullptr;
    auto* const normal = weapon != nullptr ? gReticleClass.Read<ue3::Class*>(weapon).value_or(nullptr) : nullptr;
    auto* const zoom = weapon != nullptr ? gZoomReticleClass.Read<ue3::Class*>(weapon).value_or(nullptr) : nullptr;
    auto const zoomed = weapon != nullptr ? gZoomed.ReadBool(weapon) : std::nullopt;
    bool const zoomShown = zoom != nullptr && r.IsAlive(reticle) && r.IsA(reticle, zoom);
    // A scope of its own: zoomed (its reticle may still be loading: GetWeaponReticleClass already answers it), or showing it.
    out.scope = zoom != nullptr && zoom != normal && (zoomed == true || zoomShown);
    auto const shown = gReticleShown.ReadBool(movie);
    out.text = std::format("reticle={} weapon={} zoomed={} zoomReticle={} reticleVisible={}",
        r.IsAlive(reticle) ? r.FullName(reticle) : "None", weapon != nullptr ? r.FullName(weapon) : "None",
        zoomed ? (*zoomed ? "1" : "0") : "?", zoom != nullptr ? r.NameText(zoom) : "None",
        shown ? (*shown ? "1" : "0") : "?");
    return out;
}

void Restore(char const* why) {
    // Only our zero: an alpha the game wrote since then is already the game's.
    if (gOwnedMovie != nullptr && ue3::runtime::IsAlive(gOwnedMovie) && ue3::IsA(gOwnedMovie, gReticleMovie) &&
        Alpha(gOwnedMovie) == 0.0f) {
        bool const ok = SetAlpha(gOwnedMovie, gOwnedAlpha);
        log::Info("[MEVR-CROSSHAIR] restore {} alpha {} ({}) ok={}", ue3::runtime::FullName(gOwnedMovie), gOwnedAlpha, why, ok);
    }
    gOwnedMovie = nullptr;
}

std::string Observe(bool apply) {
    if (!ue3::runtime::Up()) return "reticles: reflection unavailable";
    ue3::Object* const pc = player::Controller();
    int const gameMode = player::GameMode(pc).value_or(255);
    bool const gameplay = engine::adapter::IsHudGameplayMode(gameMode);
    std::string report = std::format("reticles: gameMode={} gameplay={} VR suppression request={} (independent of aim ray)",
        gameMode, gameplay, gRequested);
    std::string why;
    ue3::Object* const movie = pc != nullptr ? ReticleMovie(pc, &why) : nullptr;
    if (movie == nullptr) {
        if (pc == nullptr) why = "no player controller";
        report += "\n  ME3's reticle movie: " + why;
        if (apply) {
            if (gOwnedMovie != nullptr) Restore("its movie is gone");
            if (gRequested && !gToldMissing) {
                gToldMissing = true;
                log::Info("[MEVR-CROSSHAIR] the host asks to hide the crosshair; ME3's reticle movie: {}", why);
            }
        }
        return report;
    }
    if (apply && gOwnedMovie != nullptr && gOwnedMovie != movie) Restore("another reticle movie took its place");
    Shown const shown = ReadShown(pc, movie);
    auto alpha = Alpha(movie);
    bool const hide = vr::screen_reticle::HideInGameplay(gRequested, gameplay, !shown.known ? -1 : shown.scope ? 2 : 1);
    if (apply) {
        if (hide && alpha) {
            // Keep the latest nonzero game-authored alpha. Never change the movie's visibility.
            if (*alpha != 0.0f) {
                float const before = *alpha;
                if (SetAlpha(movie, 0.0f)) {
                    gOwnedMovie = movie;
                    gOwnedAlpha = before;
                    alpha = Alpha(movie);
                }
            }
        } else if (gOwnedMovie == movie) {
            Restore(hide ? "its alpha is unreadable" : shown.scope ? "the scope view is the game's" : "no longer asked");
            alpha = Alpha(movie);
        }
    }
    auto const visible = MovieVisible(movie);
    std::string const line = std::format("movie={} GetVisible={} _root._alpha={} savedAlpha={} {} suppress={} scope={}",
        ue3::runtime::FullName(movie), visible ? (*visible ? "1" : "0") : "?", alpha ? std::format("{:g}", *alpha) : "?",
        gOwnedMovie == movie ? std::format("{:g}", gOwnedAlpha) : "-", shown.text, hide, shown.scope);
    report += "\n  " + line;
    if (apply && line != gLogged) {
        log::Info("[MEVR-CROSSHAIR] {}", line);
        gLogged = line;
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
    if (!exiting && ue3::runtime::Up()) Restore("shutdown");
    gOwnedMovie = nullptr;
    gLogged.clear();
    gRequested = false;
    gToldMissing = false;
}
#else
namespace {
// ME1's and ME2's Scaleform panels (the same names in both): the player's GUI manager, its panels, each panel's handlers.
ue3::FieldRef gManager{"Engine.PlayerController", "m_oCachedScaleFormManager", ue3::kEveryGame};
ue3::FieldRef gPanels{"Engine.BioSFManager", "m_lstPanels", ue3::kEveryGame};
ue3::FieldRef gTag{"Engine.BioSFPanel", "nmTag", ue3::kEveryGame};
ue3::FieldRef gHandler{"Engine.BioSFPanel", "m_DefaultHandler", ue3::kEveryGame};
ue3::FieldRef gHandlers{"Engine.BioSFPanel", "ScriptHandlers", ue3::kEveryGame};
ue3::FieldRef gMovie{"Engine.BioSFPanel", "MovieRef", ue3::kEveryGame};
ue3::FieldRef gVisible{"Engine.BioSFPanel", "IsVisible", ue3::kEveryGame};
ue3::FieldRef gHandlerPanel{"Engine.BioSFHandler", "oPanel", ue3::kEveryGame};
ue3::ClassRef gPanelClass{"Engine.BioSFPanel", ue3::kEveryGame};
ue3::FunctionRef gGetFloat{"Engine.BioSFPanel.GetVariableFloat", ue3::kEveryGame};
ue3::FunctionRef gGetBool{"Engine.BioSFPanel.GetVariableBool", ue3::kEveryGame};
ue3::FunctionRef gSetFloat{"Engine.BioSFPanel.SetVariableFloat", ue3::kEveryGame};
#if defined(MEVR_GAME_LE2)
// ME2's crosshair handler (research/le2-renderer-integration.md "Step 5: the game's crosshair hidden"): SFXSFHandler_Reticle,
// the crosshair it shows m_eCurrentCrosshair (SFXReticleCrosshair: 0 none, 1 standard, 2 heavy weapon, 3 sniper, 4 the
// count), the sniper's scope read-outs m_bShowSniperDetails.
ue3::ClassRef gReticule{"SFXGame.SFXSFHandler_Reticle", ue3::kEveryGame};
ue3::FieldRef gCrosshair{"SFXGame.SFXSFHandler_Reticle", "m_eCurrentCrosshair", ue3::kEveryGame};
ue3::FieldRef gSniperDetails{"SFXGame.SFXSFHandler_Reticle", "m_bShowSniperDetails", ue3::kEveryGame};
constexpr std::uint8_t kSniperCrosshair = 3;
constexpr std::uint8_t kCrosshairCount = 4;
#else  // ME1 (never ME3: its stand-down is above)
ue3::ClassRef gReticule{"SFXGame.BioSFHandler_Reticule", ue3::kEveryGame};
ue3::FieldRef gReticuleInfo{"SFXGame.BioSFHandler_Reticule", "m_Reticule", ue3::kEveryGame};
ue3::FieldRef gMode{gReticuleInfo, "m_eMode"};
ue3::FieldRef gLastMode{"SFXGame.BioSFHandler_Reticule", "m_eLastReticuleMode", ue3::kEveryGame};
#endif

// What one reticle handler says: its mode for the log, whether the mode is readable, whether it is the sniper's (the
// native scope view, left to the game: vr::screen_reticle::Hide's mode 2).
struct ReticleMode {
    std::string text;
    bool known = false;
    bool sniper = false;
};
ReticleMode ReadMode(ue3::Object* handler) {
    ReticleMode out;
#if defined(MEVR_GAME_LE2)
    auto const crosshair = gCrosshair.Read<std::uint8_t>(handler);
    auto const details = gSniperDetails.ReadBool(handler);
    out.text = std::format(" crosshair={} sniperDetails={}", crosshair ? static_cast<int>(*crosshair) : -1,
        details ? (*details ? "1" : "0") : "?");
    out.known = crosshair && *crosshair < kCrosshairCount;
    out.sniper = crosshair == kSniperCrosshair || details == true;
#else
    auto const mode = gMode.Read<std::uint8_t>(gReticuleInfo.Address(handler));
    auto const last = gLastMode.Read<std::uint8_t>(handler);
    out.text = std::format(" mode={} last={}", mode ? static_cast<int>(*mode) : -1, last ? static_cast<int>(*last) : -1);
    out.known = mode && *mode < 10;
    out.sniper = mode == 2 || last == 2;
#endif
    return out;
}

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
    if (!ue3::runtime::Up()) return "reticles: reflection unavailable";
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
        bool others = false;  // a handler of another kind on this movie: its _root is more than the crosshair (LE2 only)
        std::string names;
        for (auto* const h : handlers) {
            names += (names.empty() ? "" : "; ") + r.FullName(h);
            if (!ue3::IsA(h, gReticule)) {
                others = true;
                continue;
            }
            ReticleMode const mode = ReadMode(h);
            names += mode.text + std::format(" oPanel={}", r.FullName(gHandlerPanel.Read<ue3::Object*>(h).value_or(nullptr)));
            if (gHandlerPanel.Read<ue3::Object*>(h) != panel) continue;
            reticule = true;
            modeKnown = modeKnown || mode.known;
            sniper = sniper || mode.sniper;
        }
#if defined(MEVR_GAME_LE2)
        bool const shared = reticule && others;  // its _root is more than the crosshair: left alone
        reticule = reticule && !others;
#else
        bool const shared = false;  // LE1's reticle movie is the crosshair's own (its other handlers were never asked about)
        (void)others;
#endif

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
        std::string const line = std::format("tag={} panel={} movie={} handlers=[{}] IsVisible={} _root._visible={} _root._alpha={} savedAlpha={} suppress={} sniper={}{}",
            tagText, r.FullName(panel), r.FullName(gMovie.Read<ue3::Object*>(panel).value_or(nullptr)), names,
            visible ? (*visible ? "1" : "0") : "?", rootVisible ? (*rootVisible ? "1" : "0") : "?",
            alpha ? std::format("{:g}", *alpha) : "?", owned != gOwned.end() ? std::format("{:g}", owned->second.alpha) : "-", hide, sniper,
            shared ? " (its movie has other handlers too: left alone)" : "");
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
    if (!exiting && ue3::runtime::Up()) {
        for (auto const& [panel, owned] : gOwned) Restore(panel, owned);
    }
    gOwned.clear();
    gLogged.clear();
    gMeshLogged.clear();
    gRequested = false;
}
#endif  // !MEVR_GAME_LE3
} // namespace mevr::game::screen_reticle
