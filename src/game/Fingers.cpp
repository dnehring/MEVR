// SPDX-License-Identifier: MIT
#include "game/Fingers.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "game/HandCalibration.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <vector>

namespace mevr::game::fingers {
namespace {

namespace fg = ik::fingers;

enum Side { kLeft = 0, kRight = 1 };
constexpr char const* kSideNames[2] = {"left", "right"};

// What a hand does this tick.
enum class Mode {
    Off,   // not driven (the reason is in the status)
    Free,  // the index follows the trigger, the other fingers the grip, the thumb its rest (or the grip)
    Gun,   // on the gun: only the index finger, off the trigger until it's touched (or squeezed)
    Held,  // the support grip holds the foregrip: the animation's hold, untouched
};

char const* Name(Mode mode) {
    switch (mode) {
    case Mode::Off: return "off";
    case Mode::Free: return "free";
    case Mode::Gun: return "gun";
    case Mode::Held: return "held";
    }
    return "?";
}

// Per skeleton asset: the finger bones and their axes.
struct Found {
    void const* asset = nullptr;
    int boneCount = 0;
    std::array<fg::HandBones, 2> bones{};
    std::array<std::optional<fg::HandAxes>, 2> axes{};
    std::array<bool, 2> loggedBones{};
    std::array<bool, 2> loggedNoAxes{};
};
Found gFound;

struct HandState {
    Mode mode = Mode::Off;
    char const* why = "";
    float trigger = 0.0f;
    float squeeze = 0.0f;
    double triggerTouch = -1.0;  // the eased sensor weights given to the rule (-1: no sensor, or [Hands] fingerTouch = 0)
    double thumbTouch = -1.0;
    fg::Curl curl;
};
std::array<HandState, 2> gHands;
// The touch sensors' eased weights per hand (trigger, thumb), kept across ticks; -1 while the hand has no such sensor.
std::array<std::array<double, 2>, 2> gTouch{{{-1.0, -1.0}, {-1.0, -1.0}}};
std::chrono::steady_clock::time_point gLastFill{};
std::array<int, 2> gLoggedSensors{-1, -1};  // what each controller's sensors were last logged as (bit 0 trigger, bit 1 thumb)
std::array<Mode, 2> gLoggedMode{Mode::Off, Mode::Off};
std::string gStatus = "off";
std::string gLoggedStatus;
bool gDriving = false;

std::string Format(ik::Vec3 v) {
    return std::format("({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z);
}

std::string ChainText(Skeleton const& skeleton, fg::Chain const& chain) {
    if (chain.count <= 0) {
        return "not found";
    }
    std::string text;
    for (int k = 0; k < chain.count; ++k) {
        int const bone = chain.joints[static_cast<std::size_t>(k)];
        text += std::format("{}{}({})", k == 0 ? "" : " > ", skeleton.names[static_cast<std::size_t>(bone)], bone);
    }
    return text;
}

void LogBones(Skeleton const& skeleton, int side) {
    fg::HandBones const& bones = gFound.bones[static_cast<std::size_t>(side)];
    std::string text;
    for (int f = 0; f < fg::kFingerCount; ++f) {
        text += std::format("{}{} {}", f == 0 ? "" : "; ", fg::kFingerNames[f], ChainText(skeleton, bones.fingers[static_cast<std::size_t>(f)]));
    }
    log::Info("fingers: {} hand {}({}): {} of 5 fingers: {}", kSideNames[side], skeleton.names[static_cast<std::size_t>(bones.hand)], bones.hand,
        bones.Found(), text);
}

void LogAxes(int side) {
    fg::HandAxes const& axes = *gFound.axes[static_cast<std::size_t>(side)];
    std::string text;
    for (int f = 0; f < fg::kFingerCount; ++f) {
        text += std::format("{}{} {}", f == 0 ? "" : ", ", fg::kFingerNames[f],
            axes.valid[static_cast<std::size_t>(f)] ? Format(axes.axis[static_cast<std::size_t>(f)]) : std::string("none"));
    }
    log::Info("fingers: {} hand: palm normal {} in the hand bone's axes (from {}; the rest pose's own curl agrees on {} finger(s), "
              "disagrees on {}); flexion axes: {}",
        kSideNames[side], Format(axes.palm), axes.palmFromThumb ? "the thumb's base" : "the rest pose's curl", axes.restCurlAgree,
        axes.restCurlDisagree, text);
}

// The bones and axes for this skeleton (found once; the axes measured from the pose while nothing of ours is in it).
void Resolve(Skeleton const& skeleton, std::span<engine::skeleton::BoneAtom const> pose) {
    int const count = static_cast<int>(skeleton.names.size());
    if (gFound.asset != skeleton.asset || gFound.boneCount != count) {
        gFound = {};
        gFound.asset = skeleton.asset;
        gFound.boneCount = count;
        for (int side : {kLeft, kRight}) {
            gFound.bones[static_cast<std::size_t>(side)] = fg::FindHand(skeleton.names, skeleton.parents, skeleton.hands[static_cast<std::size_t>(side)]);
        }
    }
    if (static_cast<int>(pose.size()) != count) {
        return;
    }
    for (int side : {kLeft, kRight}) {
        fg::HandBones const& bones = gFound.bones[static_cast<std::size_t>(side)];
        if (bones.hand < 0) {
            continue;
        }
        if (!gFound.loggedBones[static_cast<std::size_t>(side)]) {
            gFound.loggedBones[static_cast<std::size_t>(side)] = true;
            LogBones(skeleton, side);
        }
        if (gFound.axes[static_cast<std::size_t>(side)] || bones.Found() == 0) {
            continue;
        }
        std::vector<ik::Transform> transforms(pose.size());
        for (std::size_t i = 0; i < pose.size(); ++i) {
            transforms[i] = engine::skeleton::ToTransform(pose[i]);
        }
        gFound.axes[static_cast<std::size_t>(side)] = fg::MeasureAxes(transforms, bones);
        if (gFound.axes[static_cast<std::size_t>(side)]) {
            LogAxes(side);
        } else if (!gFound.loggedNoAxes[static_cast<std::size_t>(side)]) {
            gFound.loggedNoAxes[static_cast<std::size_t>(side)] = true;
            log::Warn("fingers: {} hand: the finger bones don't tell the palm plane or its side (index, middle and pinky knuckles, the "
                      "middle finger's first joint, the thumb's base or the rest curl are needed): its fingers stay animated",
                kSideNames[side]);
        }
    }
}

void SetStatus(std::string status) {
    gStatus = std::move(status);
    if (gStatus != gLoggedStatus) {
        gLoggedStatus = gStatus;
        log::Info("fingers: {}", gStatus);
    }
}

} // namespace

void Fill(engine::bone_pose::Request& request, Skeleton const& skeleton, std::span<engine::skeleton::BoneAtom const> pose,
    vr::controllers::State const& controllers, Inputs const& in) {
    hand_calibration::Fingers const settings = hand_calibration::GetFingers();
    request.fingers.fullCurlDegrees = settings.curlDeg;
    auto const now = std::chrono::steady_clock::now();
    double const seconds = gLastFill == std::chrono::steady_clock::time_point{}
        ? 0.0
        : std::clamp(std::chrono::duration<double>(now - gLastFill).count(), 0.0, 0.1);
    gLastFill = now;
    for (HandState& hand : gHands) {
        hand = {};
        hand.why = "not driven";
    }
    gDriving = false;
    if (!settings.on) {
        return SetStatus("off ([Hands] fingers = 0)");
    }
    if (!in.firstPerson) {
        return SetStatus("not first person");
    }
    if (!controllers.active) {
        return SetStatus("no tracked controllers");
    }
    Resolve(skeleton, pose);
    std::string driven;
    for (int side : {kLeft, kRight}) {
        HandState& state = gHands[static_cast<std::size_t>(side)];
        vr::controllers::Hand const& tracked = side == kLeft ? controllers.left : controllers.right;
        fg::HandBones const& bones = gFound.bones[static_cast<std::size_t>(side)];
        std::optional<fg::HandAxes> const& axes = gFound.axes[static_cast<std::size_t>(side)];
        state.trigger = std::clamp(tracked.trigger, 0.0f, 1.0f);
        state.squeeze = std::clamp(tracked.squeeze, 0.0f, 1.0f);
        // The touch sensors (research/43 "Touch sensors"): eased weights, kept while the controller reports the sensor.
        std::array<double, 2>& touch = gTouch[static_cast<std::size_t>(side)];
        bool const known[2] = {settings.touch && tracked.triggerTouchKnown, settings.touch && tracked.thumbTouchKnown};
        bool const touching[2] = {tracked.triggerTouch, tracked.thumbTouch};
        for (int k = 0; k < 2; ++k) {
            touch[static_cast<std::size_t>(k)] = known[k] ? fg::EaseTouch(touch[static_cast<std::size_t>(k)], touching[k], seconds) : -1.0;
        }
        state.triggerTouch = touch[0];
        state.thumbTouch = touch[1];
        int const sensors = (tracked.triggerTouchKnown ? 1 : 0) | (tracked.thumbTouchKnown ? 2 : 0);
        if (sensors != gLoggedSensors[static_cast<std::size_t>(side)]) {
            gLoggedSensors[static_cast<std::size_t>(side)] = sensors;
            log::Info("fingers: the {} controller {}{}", kSideNames[side],
                sensors == 0 ? "has no touch sensors: the index follows the trigger, the thumb the grip"
                             : std::format("has touch sensors ({}{}{})", (sensors & 1) != 0 ? "trigger" : "",
                                   sensors == 3 ? ", " : "", (sensors & 2) != 0 ? "thumb" : ""),
                sensors != 0 && !settings.touch ? ": not used ([Hands] fingerTouch = 0)" : "");
        }
        if (bones.hand < 0 || bones.Found() == 0) {
            state.why = "no finger bones";
            continue;
        }
        if (!axes) {
            state.why = "no flexion axes";
            continue;
        }
        if (!tracked.gripValid && !tracked.aimValid) {
            state.why = "controller not tracked";
            continue;
        }
        if (side == kLeft && in.weaponDrawn && in.leftOnGun) {
            state.mode = Mode::Held;
            state.why = "on the foregrip (as animated)";
            continue;  // the animation's hold: nothing written for this hand
        }
        fg::Controller controller;
        controller.trigger = state.trigger;
        controller.squeeze = state.squeeze;
        controller.triggerTouch = state.triggerTouch;
        controller.thumbTouch = state.thumbTouch;
        if (side == kRight && in.weaponDrawn) {
            state.mode = Mode::Gun;
            state.why = state.triggerTouch >= 0.0 ? "holding the gun: the index finger off the trigger until it touches it"
                                                  : "holding the gun: the index finger off the trigger until it's squeezed";
            state.curl = fg::FromController(fg::Rule::Gun, controller);
        } else {
            state.mode = Mode::Free;
            state.why = state.triggerTouch >= 0.0 || state.thumbTouch >= 0.0
                ? "free: the index points or rests on the trigger, the other fingers follow the grip, the thumb its rest"
                : "free: the index follows the trigger, the other fingers the grip";
            state.curl = fg::FromController(fg::Rule::Free, controller);  // the thumb scaled down by the solver (kThumbScale)
        }
        engine::bone_pose::FingerHand& out = request.fingers.hands[static_cast<std::size_t>(side)];
        out.enabled = true;
        out.bones = bones;
        std::array<int, fg::kMaxBones> all{};
        out.keyCount = bones.All(all);
        for (int i = 0; i < out.keyCount; ++i) {
            out.keys[static_cast<std::size_t>(i)] = skeleton.keys[static_cast<std::size_t>(all[static_cast<std::size_t>(i)])];
        }
        out.axes = *axes;
        out.curl = state.curl;
        gDriving = true;
        driven += std::format("{}{} {}", driven.empty() ? "" : ", ", kSideNames[side], Name(state.mode));
    }
    for (int side : {kLeft, kRight}) {
        Mode const mode = gHands[static_cast<std::size_t>(side)].mode;
        if (mode != gLoggedMode[static_cast<std::size_t>(side)]) {
            gLoggedMode[static_cast<std::size_t>(side)] = mode;
            log::Info("fingers: {} hand {}", kSideNames[side], gHands[static_cast<std::size_t>(side)].why);
        }
    }
    SetStatus(driven.empty() ? std::format("not driven ({}; {})", gHands[kLeft].why, gHands[kRight].why)
                             : std::format("driving: {} ({:.0f} degrees per joint at full curl)", driven, settings.curlDeg));
}

void Stop(char const* why) {
    for (HandState& hand : gHands) {
        hand = {};
        hand.why = why;
    }
    gDriving = false;
    SetStatus(why);
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::Fingers const settings = hand_calibration::GetFingers();
    std::optional<engine::bone_pose::FingersSample> const written = engine::bone_pose::LatestFingers();
    std::string hands;
    for (int side : {kLeft, kRight}) {
        HandState const& state = gHands[static_cast<std::size_t>(side)];
        std::string curl;
        for (double c : state.curl.finger) {
            curl += std::format("{}{}", curl.empty() ? "" : ", ", json::JsonNumber(c));
        }
        fg::HandBones const& bones = gFound.bones[static_cast<std::size_t>(side)];
        std::optional<fg::HandAxes> const& axes = gFound.axes[static_cast<std::size_t>(side)];
        auto const weight = [](double w) { return w >= 0.0 ? json::JsonNumber(w) : std::string("null"); };
        hands += std::format("\"{}\": {{\"mode\": {}, \"why\": {}, \"trigger\": {}, \"grip\": {}, \"triggerTouch\": {}, "
                             "\"thumbTouch\": {}, \"curl\": [{}], \"fingersFound\": {}, "
                             "\"axes\": {}, \"palmFromThumb\": {}, \"bonesWritten\": {}, \"skipped\": {}}}{}",
            kSideNames[side], json::JsonString(Name(state.mode)), json::JsonString(state.why), json::JsonNumber(state.trigger),
            json::JsonNumber(state.squeeze), weight(state.triggerTouch), weight(state.thumbTouch), curl,
            bones.hand >= 0 ? bones.Found() : 0, axes ? "true" : "false",
            axes && axes->palmFromThumb ? "true" : "false", written ? written->bonesWritten[static_cast<std::size_t>(side)] : 0,
            written && written->skipped[static_cast<std::size_t>(side)] ? "true" : "false", side == kLeft ? ", " : "");
    }
    return std::format("{{\"enabled\": {}, \"curlDeg\": {}, \"touchSensors\": {}, \"driving\": {}, \"status\": {}, {}}}",
        settings.on ? "true" : "false", json::JsonNumber(settings.curlDeg), settings.touch ? "true" : "false", gDriving ? "true" : "false",
        json::JsonString(gStatus), hands);
}

void LogStats() {
    if (!gDriving) {
        return;
    }
    std::optional<engine::bone_pose::FingersSample> const written = engine::bone_pose::LatestFingers();
    std::string text;
    for (int side : {kLeft, kRight}) {
        HandState const& state = gHands[static_cast<std::size_t>(side)];
        std::string touch;
        if (state.triggerTouch >= 0.0 || state.thumbTouch >= 0.0) {
            touch = std::format(", touch: trigger {}, thumb {}", state.triggerTouch >= 0.0 ? std::format("{:.2f}", state.triggerTouch) : "none",
                state.thumbTouch >= 0.0 ? std::format("{:.2f}", state.thumbTouch) : "none");
        }
        text += std::format("{}{} {} (trigger {:.2f}, grip {:.2f}{}{})", side == kLeft ? "" : "; ", kSideNames[side], Name(state.mode),
            state.trigger, state.squeeze, touch,
            written ? std::format(", {} bones{}", written->bonesWritten[static_cast<std::size_t>(side)],
                          written->skipped[static_cast<std::size_t>(side)] ? ", SKIPPED: bone names changed" : "")
                    : std::string(", not written yet"));
    }
    log::Info("    fingers: {}", text);
}

void Shutdown() {
    gFound = {};
    for (HandState& hand : gHands) {
        hand = {};
    }
    gLoggedMode = {Mode::Off, Mode::Off};
    gTouch = {{{-1.0, -1.0}, {-1.0, -1.0}}};
    gLastFill = {};
    gLoggedSensors = {-1, -1};
    gStatus = "off";
    gLoggedStatus.clear();
    gDriving = false;
}

std::optional<ik::Vec3> PalmNormal(void const* asset, int side) {
    if (asset == nullptr || gFound.asset != asset || (side != 0 && side != 1)) {
        return std::nullopt;
    }
    std::optional<fg::HandAxes> const& axes = gFound.axes[static_cast<std::size_t>(side)];
    return axes ? std::optional<ik::Vec3>{axes->palm} : std::nullopt;
}

} // namespace mevr::game::fingers
