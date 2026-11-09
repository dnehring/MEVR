// SPDX-License-Identifier: MIT
#include "game/HudPieces.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/CockpitProbe.hpp"
#include "game/ViewAnchor.hpp"
#include "game/WorldUi.hpp"
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
// Raised to the eye, the panel over the receiver sat right in the sight line (the owner's QA, 2026-11-05: "I can't actually
// see through my iron sights"): it leaves the gun above this raised weight (sights::RaisedWeight) and comes back below
// the lower one, so it doesn't flicker at the edge. Meanwhile the host shows the weapon panel on the HUD panel.
constexpr float kRaisedHide = 0.55f;
constexpr float kRaisedShow = 0.35f;
bool gRaisedHidden = false;
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

// Shepard's drawn arms for the omni-tool's wrist tap (research/57 section 3): the bones found once per skeleton asset.
struct BodyBones {
    ue3::Object const* asset = nullptr;
    int leftWrist = -1;
    int leftElbow = -1;
    int rightIndex1 = -1;
    int rightIndex2 = -1;
    bool Found() const { return leftWrist >= 0 && leftElbow >= 0 && rightIndex1 >= 0 && rightIndex2 >= 0; }
};
BodyBones gBody;
std::string gBodyWhy = "not looked for yet";
// A cross-check for snapshots: the right wrist from SpaceBases, and the engine's own GetBoneLocation of the index's second
// joint, in tracking space.
int gRightWristBone = -1;
std::uint64_t gIndex2Key = 0;
XrVector3f gRightWrist{};
std::optional<XrVector3f> gIndex2Native;

int BoneNamed(engine::skeleton::RefSkeleton const& skeleton, std::string_view name) {
    for (std::size_t i = 0; i < skeleton.names.size(); ++i) {
        if (skeleton.names[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Drawn first-person arms on foot or in the wrist screen's held on-foot frame; last posed bones after arm IK/fingers. Game thread.
void FillBody(hp::Pieces& pieces, bool onFoot) {
    std::string why;
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    ue3::Object* const mesh = pawn != nullptr ? player::Mesh(pawn) : nullptr;
    ue3::Object const* const asset = mesh != nullptr ? player::SkeletalMesh(mesh) : nullptr;
    std::optional<engine::TrackingFrame> const frame = onFoot ? view_anchor::Current() : world_ui::WristTrackingFrame();
    if (!onFoot && !frame) {
        why = "not on foot";
    } else if (!arm_ik::FirstPerson()) {
        why = "arm IK isn't drawing the arms in first person";
    } else if (asset == nullptr) {
        why = "no player mesh";
    } else if (!frame) {
        why = "no view frame";
    } else {
        if (asset != gBody.asset) {
            gBody = {};
            gBody.asset = asset;
            if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
                gBody.leftWrist = BoneNamed(*skeleton, "LeftWrist");
                gBody.leftElbow = BoneNamed(*skeleton, "LeftElbow");
                gBody.rightIndex1 = BoneNamed(*skeleton, "RightIndexFinger1");
                gBody.rightIndex2 = BoneNamed(*skeleton, "RightIndexFinger2");
                gRightWristBone = BoneNamed(*skeleton, "RightWrist");
                gIndex2Key = gBody.rightIndex2 >= 0 ? skeleton->keys[static_cast<std::size_t>(gBody.rightIndex2)] : 0;
            }
            log::Info("hud pieces: the omni-tool tap's bones: LeftWrist {}, LeftElbow {}, RightIndexFinger1 {}, RightIndexFinger2 {}{}",
                gBody.leftWrist, gBody.leftElbow, gBody.rightIndex1, gBody.rightIndex2,
                gBody.Found() ? "" : ": not all found, the tap stays on the controllers");
        }
        if (!gBody.Found()) {
            why = "the skeleton lacks a bone";
        } else {
            namespace sk = engine::skeleton;
            auto const w = sk::BoneLocation(mesh, gBody.leftWrist, sk::Space::World);
            auto const e = sk::BoneLocation(mesh, gBody.leftElbow, sk::Space::World);
            auto const i1 = sk::BoneLocation(mesh, gBody.rightIndex1, sk::Space::World);
            auto const i2 = sk::BoneLocation(mesh, gBody.rightIndex2, sk::Space::World);
            if (w && e && i1 && i2) {
                pieces.leftWrist = engine::ToTracking(*frame, *w);
                pieces.leftElbow = engine::ToTracking(*frame, *e);
                pieces.rightIndex1 = engine::ToTracking(*frame, *i1);
                pieces.rightIndex2 = engine::ToTracking(*frame, *i2);
                pieces.bodyValid = true;
                if (auto const rw = gRightWristBone >= 0 ? sk::BoneLocation(mesh, gRightWristBone, sk::Space::World) : std::nullopt) {
                    gRightWrist = engine::ToTracking(*frame, *rw);
                }
                // The engine's own answer once a second (two ProcessEvents): it agreed to 0.01 mm on the fake headset (2026-11-05).
                static std::uint64_t nativeMs = 0;
                if (std::uint64_t const now = GetTickCount64(); gIndex2Key != 0 && now - nativeMs >= 1000) {
                    nativeMs = now;
                    auto const native = sk::NativeBoneLocation(mesh, gIndex2Key, sk::Space::World);
                    gIndex2Native = native ? std::optional<XrVector3f>(engine::ToTracking(*frame, *native)) : std::nullopt;
                }
            } else {
                why = "the bones aren't posed";
            }
        }
    }
    if (why != gBodyWhy) {
        log::Info("hud pieces: the omni-tool tap {}", why.empty() ? "follows Shepard's drawn forearm and index fingertip"
                                                                  : std::format("is on the controllers ({})", why));
        gBodyWhy = why;
    }
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
    } else if (float const raised = sights::RaisedWeight();
               (gRaisedHidden = raised > (gRaisedHidden ? kRaisedShow : kRaisedHide))) {
        why = "the gun is raised to the eye: the sights stay clear";
    } else if (auto const top = sights::GunTop(ray); !top) {
        why = "the barrel points straight up or down";
    } else {
        bool const pistol = weapon::Type() == weapon::RangedType::Pistol;  // beside the slide, clear of its sight
        pieces.gunAnchor = hp::GunPoint(ray.origin, ray.direction, *top, gSettings.gunBackM, pistol ? gSettings.pistolUpM : gSettings.gunUpM,
            pistol ? gSettings.pistolSideM : 0.0f);
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
    FillBody(pieces, onFoot);
    Publish(link, pieces);
}

std::string StatusJson() {
    hp::Pieces const& p = gLast;
    return std::format("{{\"gunStatus\": {}, \"wristStatus\": {}, \"gunVisible\": {}, \"why\": {}, \"anchor\": [{}, {}, {}], "
                       "\"up\": [{}, {}, {}], \"gunBackM\": {}, \"gunUpM\": {}, \"serial\": {}, \"hostTakesIt\": {}, \"body\": {}}}",
        gSettings.gunStatus ? "true" : "false", gSettings.wristStatus ? "true" : "false", p.gunVisible ? "true" : "false",
        gWhy.empty() ? std::string("null") : json::JsonString(gWhy), json::JsonNumber(p.gunAnchor.x), json::JsonNumber(p.gunAnchor.y),
        json::JsonNumber(p.gunAnchor.z), json::JsonNumber(p.gunUp.x), json::JsonNumber(p.gunUp.y), json::JsonNumber(p.gunUp.z),
        json::JsonNumber(gSettings.gunBackM), json::JsonNumber(gSettings.gunUpM), p.serial, gSent ? "true" : "false",
        p.bodyValid ? std::format("{{\"leftWrist\": [{}, {}, {}], \"leftElbow\": [{}, {}, {}], \"rightIndex2\": [{}, {}, {}], "
                                  "\"rightWrist\": [{}, {}, {}], \"rightIndex2Native\": {}}}",
                          json::JsonNumber(p.leftWrist.x), json::JsonNumber(p.leftWrist.y), json::JsonNumber(p.leftWrist.z),
                          json::JsonNumber(p.leftElbow.x), json::JsonNumber(p.leftElbow.y), json::JsonNumber(p.leftElbow.z),
                          json::JsonNumber(p.rightIndex2.x), json::JsonNumber(p.rightIndex2.y), json::JsonNumber(p.rightIndex2.z),
                          json::JsonNumber(gRightWrist.x), json::JsonNumber(gRightWrist.y), json::JsonNumber(gRightWrist.z),
                          gIndex2Native ? std::format("[{}, {}, {}]", json::JsonNumber(gIndex2Native->x), json::JsonNumber(gIndex2Native->y),
                                                  json::JsonNumber(gIndex2Native->z))
                                        : std::string("null"))
                    : json::JsonString(gBodyWhy));
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
    gBody = {};
    gBodyWhy = "not looked for yet";
}

} // namespace mevr::game::hud_pieces
