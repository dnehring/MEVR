// SPDX-License-Identifier: MIT
#include "game/HudPieces.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "game/CockpitProbe.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/Sights.hpp"
#include "game/Weapon.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <optional>

namespace mevr::game::hud_pieces {
namespace {

namespace hp = vr::hud_pieces;
namespace json = vr::test_request;

hp::Settings gSettings;
hp::Pieces gLast;
bool gSent = false;
bool gToldNoHost = false;
std::uintptr_t gWeaponKey = 0;
std::uint32_t gSerial = 0;
std::string gWhy;  // why the gun piece isn't shown (empty: shown)
unsigned gTicksShown = 0;
unsigned gTicksHidden = 0;

char const* TypeName() {
    switch (weapon::Type()) {
    case weapon::RangedType::Pistol: return "pistol";
    case weapon::RangedType::Shotgun: return "shotgun";
    case weapon::RangedType::AssaultRifle: return "assault rifle";
    case weapon::RangedType::Sniper: return "sniper rifle";
    case weapon::RangedType::Unknown: break;
    }
    return "gun";
}

void Publish(Link const& link, hp::Pieces const& pieces) {
    gLast = pieces;
    if (link.publish != nullptr) {
        link.publish(&pieces);
        gSent = true;
    }
}

} // namespace

void Tick(vr::aim_ray::Ray const& ray, Link const& link) {
    gSettings = hand_calibration::GetHud();
    ue3::Object* const controller = player::Controller();
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    bool const onFoot = mode && *mode == 0;

    // The held weapon changed: a new serial, so the host logs the new place.
    std::uintptr_t const key = weapon::Key();
    if (key != gWeaponKey) {
        gWeaponKey = key;
        ++gSerial;
    }

    hp::Pieces pieces;
    pieces.serial = gSerial;
    std::string why;
    if (!gSettings.gunStatus) {
        why = "off ([Hud] gunStatus = 0)";
    } else if (!onFoot) {
        why = "not on foot";
    } else if (!ray.valid) {
        why = "no gun aimed along its barrel";
    } else if (auto const top = sights::GunTop(ray); !top) {
        why = "the barrel points straight up or down";
    } else {
        pieces.gunAnchor = hp::GunPoint(ray.origin, ray.direction, *top, gSettings.gunBackM, gSettings.gunUpM);
        pieces.gunUp = *top;
        pieces.gunVisible = std::isfinite(pieces.gunAnchor.x) && std::isfinite(pieces.gunAnchor.y) && std::isfinite(pieces.gunAnchor.z);
        if (!pieces.gunVisible) {
            why = "the anchor isn't finite";
        }
    }
    if (why != gWhy) {
        if (why.empty()) {
            log::Info("hud pieces: the weapon panel rides on the {} ({:.0f} mm behind the muzzle, {:.0f} mm above the bore)", TypeName(),
                gSettings.gunBackM * 1000.0f, gSettings.gunUpM * 1000.0f);
        } else if (gWhy.empty() && gSent) {
            log::Info("hud pieces: the weapon panel is back on the HUD ({})", why);
        }
        gWhy = why;
    }
    (pieces.gunVisible ? gTicksShown : gTicksHidden) += 1;
    if (link.publish == nullptr && gSettings.gunStatus && !gToldNoHost) {
        gToldNoHost = true;
        log::Warn("hud pieces: the host (services older than v22) can't show the weapon panel on the gun: close the game and "
                  "redeploy the host");
    }
    cockpit_probe::FillHud(pieces);  // in the Mako's cockpit: its displays (research/47)
    Publish(link, pieces);
}

std::string StatusJson() {
    hp::Pieces const& p = gLast;
    return std::format("{{\"gunStatus\": {}, \"wristStatus\": {}, \"gunVisible\": {}, \"why\": {}, \"anchor\": [{}, {}, {}], "
                       "\"up\": [{}, {}, {}], \"gunBackM\": {}, \"gunUpM\": {}, \"serial\": {}, \"hostTakesIt\": {}}}",
        gSettings.gunStatus ? "true" : "false", gSettings.wristStatus ? "true" : "false", p.gunVisible ? "true" : "false",
        gWhy.empty() ? std::string("null") : json::JsonString(gWhy), json::JsonNumber(p.gunAnchor.x), json::JsonNumber(p.gunAnchor.y),
        json::JsonNumber(p.gunAnchor.z), json::JsonNumber(p.gunUp.x), json::JsonNumber(p.gunUp.y), json::JsonNumber(p.gunUp.z),
        json::JsonNumber(gSettings.gunBackM), json::JsonNumber(gSettings.gunUpM), p.serial, gSent ? "true" : "false");
}

void LogStats() {
    if (gTicksShown == 0 && gTicksHidden == 0) {
        return;
    }
    if (gTicksShown > 0) {
        log::Info("    hud pieces: the weapon panel on the {} for {} ticks{}", TypeName(), gTicksShown,
            gTicksHidden > 0 ? std::format(", on the HUD for {} ({})", gTicksHidden, gWhy) : std::string());
    }
    gTicksShown = gTicksHidden = 0;
}

void Shutdown(Link const& link) {
    if (gSent && link.publish != nullptr) {
        hp::Pieces const none;
        link.publish(&none);
    }
    gSent = false;
    gLast = {};
    gWhy.clear();
    gWeaponKey = 0;
    gToldNoHost = false;
    gSettings = {};
}

} // namespace mevr::game::hud_pieces
