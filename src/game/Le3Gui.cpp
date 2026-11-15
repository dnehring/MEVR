// SPDX-License-Identifier: MIT
#include "game/Le3Gui.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include <cmath>
#include <format>
#include <array>

namespace mevr::game::le3_gui {
namespace {
ue3::FunctionRef gHud{"SFXGame.BioPlayerController.GetHUDMovie", ue3::kEveryGame};
ue3::FunctionRef gNumber{"GFxUI.GFxValue.GetNumber", ue3::kEveryGame};
ue3::FunctionRef gBool{"GFxUI.GFxValue.GetBool", ue3::kEveryGame};
ue3::FunctionRef gPosition{"GFxUI.GFxValue.GetPosition", ue3::kEveryGame};
ue3::FunctionRef gManager{"SFXGame.BioPlayerController.GetSFXUIController", ue3::kEveryGame};
ue3::FunctionRef gFocus{"SFXGame.SFXGUIInteraction.GetFocusMovieForPlayerController", ue3::kEveryGame};
ue3::FunctionRef gBrowser{"SFXGame.SFXGUIInteraction.GetBrowserHandler", ue3::kEveryGame};
ue3::FieldRef gTag{"SFXGame.SFXGUIMovie", "nmTag", ue3::kEveryGame};
ue3::FieldRef gWorld{"SFXGame.SFXGUIMovie", "oWorldInfo", ue3::kEveryGame};
ue3::FieldRef gAlerts{"SFXGame.BioWorldInfo", "m_lstBrowserAlerts", ue3::kEveryGame};
ue3::FieldRef gOverrides{"SFXGame.BioWorldInfo", "SubPageStateOverrides", ue3::kEveryGame};
ue3::FieldRef gPage{gOverrides, "Page"};
ue3::FieldRef gState{gOverrides, "State"};

ue3::Object* GetObject(ue3::Object* receiver, ue3::FunctionRef& ref, bool withPlayer) {
    if (!ue3::runtime::Up() || !ue3::runtime::IsAlive(receiver)) return nullptr;
    auto* fn = ref.Get();
    if (!fn) return nullptr;
    ue3::Params p(ue3::runtime::Get(), fn);
    auto* pc = player::Controller();
    if (!p.Valid() || (withPlayer && (!pc || !p.Set("oPlayerController", pc)))) return nullptr;
    if (!ue3::runtime::Call(receiver, fn, p)) return nullptr;
    auto* result = p.Return<ue3::Object*>().value_or(nullptr);
    return ue3::runtime::IsAlive(result) ? result : nullptr;
}
}
ue3::Object* Manager() { return GetObject(player::Controller(), gManager, false); }
ue3::Object* FocusMovie() { return GetObject(Manager(), gFocus, true); }
ue3::Object* Browser(ue3::Object* manager) { return GetObject(manager, gBrowser, true); }
std::string Tag(ue3::Object* movie) {
    if (!ue3::runtime::IsAlive(movie)) return {};
    auto const tag = gTag.Read<ue3::Name>(movie);
    return tag ? ue3::runtime::Get().ToString(*tag) : std::string{};
}
bool PageAllowed(ue3::Object* browser, std::uint8_t type) {
    // InitializeBrowser has already calculated game/story restrictions before the browser takes focus.
    // Apply its same subpage overrides. This adapter never opens Save/Load/Exit or changes plot state.
    if (!ue3::runtime::IsAlive(browser) || (type != 0 && type != 2 && type != 4 && type != 5)) return false;
    auto* world = gWorld.Read<ue3::Object*>(browser).value_or(nullptr);
    if (!ue3::runtime::IsAlive(world)) return false;
    auto const alerts = gAlerts.Read<std::array<std::uint8_t, 11>>(world);
    auto const overrides = gOverrides.ElementsOf(world);
    if (!alerts || !overrides) return false;
    std::uint8_t state = (*alerts)[type];
    if (type != 0) { // the native browser explicitly ignores map overrides
        for (int i = 0; i < overrides->count; ++i) {
            if (gPage.Read<std::uint8_t>(overrides->At(i)).value_or(255) == type)
                state = gState.Read<std::uint8_t>(overrides->At(i)).value_or(2);
        }
    }
    return state <= 1; // BBS_Normal / BBS_Alert; disabled and unknown remain native-only
}
std::string HudReport() {
    auto* hud = GetObject(player::Controller(), gHud, false);
    if (!hud) return "ME3 HUD: no live GetHUDMovie result; no native UI changed";
    auto& r = ue3::runtime::Get();
    auto number = [&](ue3::Object* object, wchar_t const* member, bool stage = false) -> std::optional<float> {
        auto* fn = stage ? r.FindFunction(r.ClassOf(object), "GetVariableFloat") : gNumber.Get();
        if (!fn) return std::nullopt;
        ue3::Params p(r, fn);
        ue3::memory::OwnedString key(member);
        if (!p.Valid() || !key.Valid() || !p.Set(stage ? "sPath" : "member", key.Get()) || !ue3::runtime::Call(object, fn, p))
            return std::nullopt;
        auto const v = p.Return<float>();
        return v && std::isfinite(*v) ? v : std::nullopt;
    };
    auto text = [](std::optional<float> value) { return value ? std::format("{:.3f}", *value) : std::string("unavailable"); };
    std::string out = std::format("ME3 HUD: {} tag={} stage={}x{}; local clip bounds only, crop NOT accepted; no native UI changed",
        r.FullName(hud), Tag(hud), text(number(hud, L"Stage.width", true)), text(number(hud, L"Stage.height", true)));
    // Exact fields assigned by installed SFXSFHandler_HUD.InitDisplay; no object enumeration or guessed movie paths.
    static ue3::FieldRef clips[] = {
        {"SFXGame.SFXSFHandler_HUD", "WeaponIcon", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "WeaponAmmo", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "WeaponClip", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "GrenadeAmmo", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "OverheatIndicator", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "PlayerPowerLeft", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "PlayerPowerRight", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "TargetStatus", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "TargetName", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "CenterStatus", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "ActionIcon", ue3::kEveryGame},
        {"SFXGame.SFXSFHandler_HUD", "POI", ue3::kEveryGame}};
    constexpr char const* names[] = {"WeaponIcon", "WeaponAmmo", "WeaponClip", "GrenadeAmmo", "OverheatIndicator",
        "PlayerPowerLeft", "PlayerPowerRight", "TargetStatus", "TargetName", "CenterStatus", "ActionIcon", "POI"};
    for (unsigned i = 0; i < std::size(clips); ++i) {
        auto* clip = clips[i].Read<ue3::Object*>(hud).value_or(nullptr);
        if (!ue3::runtime::IsAlive(clip)) { out += std::format("\n  {} unavailable", names[i]); continue; }
        ue3::Params at(r, gPosition.Get());
        bool const positioned = at.Valid() && ue3::runtime::Call(clip, gPosition.Get(), at) && at.ReturnBool().value_or(false);
        ue3::Params visible(r, gBool.Get());
        ue3::memory::OwnedString key(L"_visible");
        bool const readVisible = visible.Valid() && key.Valid() && visible.Set("member", key.Get()) &&
            ue3::runtime::Call(clip, gBool.Get(), visible);
        auto const shown = readVisible ? visible.ReturnBool() : std::nullopt;
        out += std::format("\n  {} x={} y={} width={} height={} alpha={} visible={}", names[i],
            text(positioned ? at.Get<float>("X") : std::nullopt), text(positioned ? at.Get<float>("Y") : std::nullopt),
            text(number(clip, L"_width")), text(number(clip, L"_height")), text(number(clip, L"_alpha")),
            shown ? (*shown ? "true" : "false") : "unavailable");
    }
    return out;
}

}
