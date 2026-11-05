// SPDX-License-Identifier: MIT
// A dev probe for our own touch inventory (game/InventoryProbe.hpp, research/56 section 6).
#include "game/InventoryProbe.hpp"

#include "core/Log.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <format>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::inventory_probe {
namespace {

namespace mem = ue3::memory;

ue3::FieldRef gScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel"};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gGame{"Engine.WorldInfo", "Game"};
ue3::FieldRef gInvInterface{"SFXGame.BioSFHandler_Inventory", "m_oInvInterface"};
ue3::FieldRef gCurrentMember{"SFXGame.BioSFHandler_Inventory", "m_nCurrentSquadMember"};
ue3::FieldRef gHandlerSlot{"SFXGame.BioSFHandler_Inventory", "m_eCurrentSlot"};
ue3::FieldRef gInterfaceSlot{"SFXGame.BioInventoryGuiInterface", "m_eCurrentSlot"};
ue3::FieldRef gSlotOrder{"SFXGame.BioInventoryGuiInterface", "lstEquipSlotOrder"};
ue3::FieldRef gItems{"SFXGame.BioInventoryGuiInterface", "EquippableItemsList"};
ue3::FieldRef gItemName{gItems, "ItemName"};
ue3::FieldRef gItemLevel{gItems, "sophistication"};
ue3::FieldRef gItemManufacturer{gItems, "ManufacturerName"};
ue3::FieldRef gItemCategory{gItems, "ItemCategoryName"};
ue3::FieldRef gItemPrice{gItems, "m_price"};
ue3::FieldRef gItemInvIndex{gItems, "InvIndex"};
ue3::FieldRef gItemNew{gItems, "m_bIsNew"};
ue3::FieldRef gItemEquippable{gItems, "m_bIsEquippable"};
ue3::FieldRef gItemRaw{gItems, "m_rawStats"};
ue3::FieldRef gRawStats{gItemRaw, "lstStats"};
ue3::FieldRef gStatCurrent{gRawStats, "Current"};
ue3::FieldRef gStatAbsolute{gRawStats, "nAbsolute"};
ue3::FieldRef gStatType{gRawStats, "eType"};

std::map<std::int32_t, std::string> gTexts;  // text references read this session

std::string Take(ue3::FString string) {
    std::string const text = string.count > 0 && string.count < 65536 ? mem::Narrow(string) : std::string();
    mem::FreeString(string);
    return text;
}

bool Call(ue3::Object* target, char const* name, std::function<void(ue3::Params&)> const& set,
    std::function<void(ue3::Params&)> const& get = nullptr) {
    if (target == nullptr) {
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const f = r.FindFunction(r.ClassOf(target), name);
    if (f == nullptr) {
        return false;
    }
    ue3::Params p(r, f);
    if (!p.Valid()) {
        return false;
    }
    if (set) {
        set(p);
    }
    if (!ue3::runtime::Call(target, f, p)) {
        return false;
    }
    if (get) {
        get(p);
    }
    return true;
}

// A text reference's text, through the game info's GetSimpleString (galaxy_map's way).
std::string Text(std::int32_t ref) {
    if (ref <= 0) {
        return {};
    }
    if (auto const it = gTexts.find(ref); it != gTexts.end()) {
        return it->second;
    }
    ue3::Object* const pc = player::Controller();
    ue3::Object* const world = pc != nullptr ? gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    ue3::Object* const game = world != nullptr ? gGame.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
    std::string text;
    Call(game, "GetSimpleString", [&](ue3::Params& p) {
        p.Set("StrRef", ref);
        p.SetBool("bParse", false);
    }, [&](ue3::Params& p) { text = Take(p.Return<ue3::FString>().value_or(ue3::FString{})); });
    gTexts[ref] = text;
    return text;
}

struct Screen {
    ue3::Object* handler = nullptr;
    ue3::Object* iface = nullptr;
    std::string why;
};

Screen Open() {
    Screen s;
    ue3::Object* const pc = player::Controller();
    ue3::Object* const manager = pc != nullptr ? gScaleFormManager.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    ue3::Object* const panel = manager != nullptr ? gFocusPanel.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
    s.handler = panel != nullptr ? gPanelHandler.Read<ue3::Object*>(panel).value_or(nullptr) : nullptr;
    s.iface = s.handler != nullptr ? gInvInterface.Read<ue3::Object*>(s.handler).value_or(nullptr) : nullptr;
    if (s.iface == nullptr) {
        s.why = s.handler == nullptr ? "no focused GUI panel (open the equipment screen: mevr omni equipment)"
                                     : std::format("the focused panel's handler ({}) has no inventory interface",
                                           ue3::runtime::Get().FullName(s.handler));
    }
    return s;
}

std::string ItemText(void const* item, int index) {
    std::string name = "?";
    if (auto const* s = gItemName.In<ue3::FString>(item); s != nullptr && s->count > 0 && s->count < 512) {
        name = mem::Narrow(*s);
    }
    std::string line = std::format("  [{}] {} (level {}, {}; {}; inv {}; {:.0f} credits{}{})", index, name,
        gItemLevel.Read<std::int32_t>(item).value_or(-1), Text(gItemCategory.Read<std::int32_t>(item).value_or(0)),
        Text(gItemManufacturer.Read<std::int32_t>(item).value_or(0)), gItemInvIndex.Read<std::int32_t>(item).value_or(-1),
        gItemPrice.Read<float>(item).value_or(0.0f), gItemNew.ReadBool(item).value_or(false) ? ", new" : "",
        gItemEquippable.ReadBool(item).value_or(true) ? "" : ", not equippable");
    void* const raw = gItemRaw.Address(item);
    ue3::PropertyInfo const* const stats = gRawStats.Get();
    std::uint8_t* const first = raw != nullptr ? static_cast<std::uint8_t*>(gRawStats.Address(raw)) : nullptr;
    if (first != nullptr && stats != nullptr) {
        line += ":";
        for (int i = 0; i < stats->arrayDim && i < 3; ++i) {
            std::uint8_t const* const stat = first + static_cast<std::ptrdiff_t>(i) * stats->elementSize;
            line += std::format(" stat {} = {:.2f} ({})", gStatType.Read<std::uint8_t>(stat).value_or(255),
                gStatCurrent.Read<float>(stat).value_or(-1.0f), gStatAbsolute.Read<std::int32_t>(stat).value_or(-1));
        }
    }
    return line;
}

std::string List(Screen const& s) {
    std::string out = std::format("inv: squad member {}, the handler's slot {}, the interface's slot {}",
        gCurrentMember.Read<std::int32_t>(s.handler).value_or(-1), gHandlerSlot.Read<std::uint8_t>(s.handler).value_or(255),
        gInterfaceSlot.Read<std::uint8_t>(s.iface).value_or(255));
    // The equipped item: GetEquippedItem(out FItemInfo); only its name is read (the out struct's other strings and arrays
    // stay with the parameter block: a probe's leak).
    std::string equipped = "none";
    Call(s.iface, "GetEquippedItem", nullptr, [&](ue3::Params& p) {
        ue3::ParamSpec const* const spec = p.Find("stEquippedItem");
        ue3::PropertyInfo const* const nameInfo = gItemName.Get();
        if (p.ReturnBool().value_or(false) && spec != nullptr && nameInfo != nullptr) {
            auto const* name = reinterpret_cast<ue3::FString const*>(static_cast<std::uint8_t const*>(p.Data()) + spec->offset + nameInfo->offset);
            if (name->count > 0 && name->count < 512) {
                equipped = mem::Narrow(*name);
            }
        }
    });
    out += std::format("; equipped: {}", equipped);
    auto const items = gItems.ElementsOf(s.iface, 2048);
    out += std::format("; {} items in the list", items ? items->count : 0);
    if (items) {
        for (std::int32_t i = 0; i < items->count && i < 60; ++i) {
            out += "\n" + ItemText(items->At(i), i);
        }
    }
    return out;
}

} // namespace

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    Screen const s = Open();
    if (s.iface == nullptr) {
        return "inv: " + s.why;
    }
    if (verb.empty() || verb == "list") {
        return List(s);
    }
    if (verb == "slot") {
        int slot = -1;
        words >> slot;
        bool const selected = Call(s.iface, "SelectItemSlot", [&](ue3::Params& p) { p.Set("eSlot", static_cast<std::uint8_t>(slot)); });
        bool const built = Call(s.iface, "BuildEquippableItemList", nullptr);
        return std::format("inv: SelectItemSlot({}) {}, BuildEquippableItemList {}\n{}", slot, selected, built, List(s));
    }
    if (verb == "slots") {
        std::uint8_t const was = gInterfaceSlot.Read<std::uint8_t>(s.iface).value_or(0);
        auto const order = gSlotOrder.ElementsOf(s.iface, 64);
        std::string out = std::format("inv: {} slots in lstEquipSlotOrder (on {} now):", order ? order->count : 0, was);
        if (order) {
            for (std::int32_t i = 0; i < order->count; ++i) {
                std::uint8_t const slot = *static_cast<std::uint8_t const*>(order->At(i));
                Call(s.iface, "SelectItemSlot", [&](ue3::Params& p) { p.Set("eSlot", slot); });
                Call(s.iface, "BuildEquippableItemList", nullptr);
                auto const items = gItems.ElementsOf(s.iface, 2048);
                out += std::format(" slot {}: {} items;", slot, items ? items->count : 0);
            }
        }
        Call(s.iface, "SelectItemSlot", [&](ue3::Params& p) { p.Set("eSlot", was); });
        Call(s.iface, "BuildEquippableItemList", nullptr);
        return out;
    }
    if (verb == "equip") {
        int index = -1;
        words >> index;
        bool result = false;
        bool const called = Call(s.iface, "Equip", [&](ue3::Params& p) {
            p.Set("filteredIndex", static_cast<std::int32_t>(index));
            p.SetBool("bXferXMods", false);
        }, [&](ue3::Params& p) { result = p.ReturnBool().value_or(false); });
        log::Info("inventory probe: Equip({}) called {}, returned {}", index, called, result);
        return std::format("inv: Equip({}, false) called {}, returned {}\n{}", index, called, result, List(s));
    }
    return "usage: mevr inv [list] | slot <n> | slots | equip <i>";
}

} // namespace mevr::game::inventory_probe
