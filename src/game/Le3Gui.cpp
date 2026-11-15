// SPDX-License-Identifier: MIT
#include "game/Le3Gui.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"
#include <array>

namespace mevr::game::le3_gui {
namespace {
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
}
