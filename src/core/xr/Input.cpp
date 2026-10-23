#include "core/xr/Input.hpp"

#include "core/Log.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace mevr::xr {
namespace {

bool Check(XrResult result, char const* what) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    log::Warn("OpenXR: {} failed ({})", what, static_cast<int>(result));
    return false;
}

XrPath ToPath(XrInstance instance, char const* text) {
    XrPath path = XR_NULL_PATH;
    xrStringToPath(instance, text, &path);
    return path;
}

XrAction CreateAction(XrActionSet set, XrActionType type, char const* name,
    char const* localized, std::array<XrPath, 2> const& hands) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.countSubactionPaths = static_cast<std::uint32_t>(hands.size());
    info.subactionPaths = hands.data();
    XrAction action = XR_NULL_HANDLE;
    if (!Check(xrCreateAction(set, &info, &action), name)) {
        return XR_NULL_HANDLE;
    }
    return action;
}

struct Binding {
    XrAction action;
    std::string path;
};

void Bind(std::vector<Binding>& list, XrAction action, char const* hand, char const* leaf) {
    list.push_back({action, std::string("/user/hand/") + hand + leaf});
}

// Suggestions for one interaction profile. A runtime that doesn't know a profile rejects it;
// that's fine as long as one profile is accepted.
bool Suggest(XrInstance instance, char const* profile, std::vector<Binding> const& bindings) {
    std::vector<XrActionSuggestedBinding> suggested;
    for (Binding const& binding : bindings) {
        suggested.push_back({binding.action, ToPath(instance, binding.path.c_str())});
    }
    XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    info.interactionProfile = ToPath(instance, profile);
    info.countSuggestedBindings = static_cast<std::uint32_t>(suggested.size());
    info.suggestedBindings = suggested.data();
    XrResult const result = xrSuggestInteractionProfileBindings(instance, &info);
    if (XR_FAILED(result)) {
        log::Warn("OpenXR: bindings for {} rejected ({})", profile, static_cast<int>(result));
        return false;
    }
    return true;
}

float ReadFloat(XrSession session, XrAction action, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand;
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(session, &info, &state)) || !state.isActive) {
        return 0.0f;
    }
    return state.currentState;
}

bool ReadBool(XrSession session, XrAction action, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(session, &info, &state)) || !state.isActive) {
        return false;
    }
    return state.currentState == XR_TRUE;
}

XrVector2f ReadStick(XrSession session, XrAction action, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand;
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xrGetActionStateVector2f(session, &info, &state)) || !state.isActive) {
        return {0.0f, 0.0f};
    }
    return state.currentState;
}

bool Locate(XrSpace space, XrSpace base, XrTime time, XrPosef& out) {
    if (space == XR_NULL_HANDLE) {
        return false;
    }
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(space, base, time, &location))) {
        return false;
    }
    XrSpaceLocationFlags const needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((location.locationFlags & needed) != needed) {
        return false;
    }
    out = location.pose;
    return true;
}

} // namespace

Input::Target Input::From(Session const& session) {
    return {session.Instance(), session.Handle(), session.LocalSpace(), session.InstanceGeneration(), session.Generation()};
}

bool Input::Create(Target const& target) {
    XrInstance const instance = target.instance;
    hands_ = {ToPath(instance, "/user/hand/left"), ToPath(instance, "/user/hand/right")};

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName, "gameplay");
    std::strcpy(setInfo.localizedActionSetName, "Gameplay");
    if (!Check(xrCreateActionSet(instance, &setInfo, &set_), "xrCreateActionSet")) {
        set_ = XR_NULL_HANDLE;
        return false;
    }
    instanceGeneration_ = target.instanceGeneration;
    sessionGeneration_ = target.sessionGeneration;
    created_ = true;  // from here on, Release cleans up whatever exists

    trigger_ = CreateAction(set_, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", hands_);
    squeeze_ = CreateAction(set_, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Grip", hands_);
    stick_ = CreateAction(set_, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", hands_);
    stickClick_ = CreateAction(set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick click", hands_);
    primary_ = CreateAction(set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "A / X", hands_);
    secondary_ = CreateAction(set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "B / Y", hands_);
    menu_ = CreateAction(set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", hands_);
    gripPose_ = CreateAction(set_, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose", hands_);
    aimPose_ = CreateAction(set_, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose", hands_);
    // Optional: without it the controllers work, they just don't vibrate (Vibrate).
    haptic_ = CreateAction(set_, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Vibration", hands_);
    for (XrAction action : {trigger_, squeeze_, stick_, stickClick_, primary_, secondary_, menu_, gripPose_, aimPose_}) {
        if (action == XR_NULL_HANDLE) {
            Release(target);
            return false;
        }
    }

    std::vector<Binding> touch;
    for (char const* hand : {"left", "right"}) {
        Bind(touch, trigger_, hand, "/input/trigger/value");
        Bind(touch, squeeze_, hand, "/input/squeeze/value");
        Bind(touch, stick_, hand, "/input/thumbstick");
        Bind(touch, stickClick_, hand, "/input/thumbstick/click");
        Bind(touch, gripPose_, hand, "/input/grip/pose");
        Bind(touch, aimPose_, hand, "/input/aim/pose");
    }
    Bind(touch, primary_, "left", "/input/x/click");
    Bind(touch, secondary_, "left", "/input/y/click");
    Bind(touch, primary_, "right", "/input/a/click");
    Bind(touch, secondary_, "right", "/input/b/click");
    Bind(touch, menu_, "left", "/input/menu/click");
    if (haptic_ != XR_NULL_HANDLE) {
        Bind(touch, haptic_, "left", "/output/haptic");
        Bind(touch, haptic_, "right", "/output/haptic");
    }

    std::vector<Binding> index;
    for (char const* hand : {"left", "right"}) {
        Bind(index, trigger_, hand, "/input/trigger/value");
        Bind(index, squeeze_, hand, "/input/squeeze/value");
        Bind(index, stick_, hand, "/input/thumbstick");
        Bind(index, stickClick_, hand, "/input/thumbstick/click");
        Bind(index, primary_, hand, "/input/a/click");
        Bind(index, secondary_, hand, "/input/b/click");
        Bind(index, gripPose_, hand, "/input/grip/pose");
        Bind(index, aimPose_, hand, "/input/aim/pose");
        if (haptic_ != XR_NULL_HANDLE) {
            Bind(index, haptic_, hand, "/output/haptic");
        }
    }

    std::vector<Binding> simple;
    for (char const* hand : {"left", "right"}) {
        Bind(simple, trigger_, hand, "/input/select/click");
        Bind(simple, menu_, hand, "/input/menu/click");
        Bind(simple, gripPose_, hand, "/input/grip/pose");
        Bind(simple, aimPose_, hand, "/input/aim/pose");
        if (haptic_ != XR_NULL_HANDLE) {
            Bind(simple, haptic_, hand, "/output/haptic");
        }
    }

    bool const touchOk = Suggest(instance, "/interaction_profiles/oculus/touch_controller", touch);
    bool const indexOk = Suggest(instance, "/interaction_profiles/valve/index_controller", index);
    bool const simpleOk = Suggest(instance, "/interaction_profiles/khr/simple_controller", simple);
    if (!touchOk && !indexOk && !simpleOk) {
        log::Error("OpenXR: no controller profile accepted; motion controllers unavailable");
        Release(target);
        return false;
    }

    for (int hand : {kLeft, kRight}) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.subactionPath = hands_[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        spaceInfo.action = gripPose_;
        Check(xrCreateActionSpace(target.session, &spaceInfo, &gripSpaces_[hand]), "xrCreateActionSpace(grip)");
        spaceInfo.action = aimPose_;
        Check(xrCreateActionSpace(target.session, &spaceInfo, &aimSpaces_[hand]), "xrCreateActionSpace(aim)");
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &set_;
    if (!Check(xrAttachSessionActionSets(target.session, &attach), "xrAttachSessionActionSets")) {
        Release(target);
        return false;
    }
    loggedFocus_ = false;
    log::Info("OpenXR: controller actions ready");
    return true;
}

void Input::Release(Target const& target) {
    if (!created_) {
        return;
    }
    bool const sessionAlive = target.session != XR_NULL_HANDLE && target.sessionGeneration == sessionGeneration_;
    bool const instanceAlive = target.instance != XR_NULL_HANDLE && target.instanceGeneration == instanceGeneration_;
    for (auto* spaces : {&gripSpaces_, &aimSpaces_}) {
        for (XrSpace& space : *spaces) {
            if (space != XR_NULL_HANDLE && sessionAlive) {
                xrDestroySpace(space);
            }
            space = XR_NULL_HANDLE;
        }
    }
    if (set_ != XR_NULL_HANDLE && instanceAlive) {
        xrDestroyActionSet(set_);  // also destroys its actions
    }
    set_ = XR_NULL_HANDLE;
    trigger_ = squeeze_ = stick_ = stickClick_ = primary_ = secondary_ = menu_ = gripPose_ = aimPose_ = haptic_ = XR_NULL_HANDLE;
    created_ = false;
}

bool Input::Matches(Target const& target) const {
    return created_ && target.session != XR_NULL_HANDLE && target.sessionGeneration == sessionGeneration_ &&
        target.instanceGeneration == instanceGeneration_;
}

void Input::Sync(Target const& target, XrTime time, vr::controllers::State& out) {
    out = {};
    focused_ = false;
    if (!Matches(target)) {
        return;
    }
    XrActiveActionSet active{set_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    XrResult const result = xrSyncActions(target.session, &sync);
    if (result == XR_SESSION_NOT_FOCUSED) {
        if (!loggedFocus_) {
            log::Info("OpenXR: controllers idle (session not focused)");
            loggedFocus_ = true;
        }
        return;
    }
    if (!Check(result, "xrSyncActions")) {
        return;
    }
    loggedFocus_ = false;
    focused_ = true;

    XrSession const handle = target.session;
    for (int hand : {kLeft, kRight}) {
        vr::controllers::Hand& h = hand == kLeft ? out.left : out.right;
        XrPath const path = hands_[hand];
        h.trigger = ReadFloat(handle, trigger_, path);
        h.squeeze = ReadFloat(handle, squeeze_, path);
        XrVector2f const stick = ReadStick(handle, stick_, path);
        h.stickX = stick.x;
        h.stickY = stick.y;
        h.stickClick = ReadBool(handle, stickClick_, path);
        h.primary = ReadBool(handle, primary_, path);
        h.secondary = ReadBool(handle, secondary_, path);
        h.menu = ReadBool(handle, menu_, path);
        h.gripValid = Locate(gripSpaces_[hand], target.base, time, h.grip);
        h.aimValid = Locate(aimSpaces_[hand], target.base, time, h.aim);
    }
    auto const pressed = [](vr::controllers::Hand const& h) {
        return h.trigger > 0.0f || h.squeeze > 0.0f || h.stickX != 0.0f || h.stickY != 0.0f || h.stickClick ||
            h.primary || h.secondary || h.menu;
    };
    // Tracked, or used while tracking is lost (buttons still work then).
    out.active = out.left.gripValid || out.right.gripValid || pressed(out.left) || pressed(out.right);
}

bool Input::Vibrate(Target const& target, int hand, float seconds, float amplitude, float frequencyHz) {
    if (!Matches(target) || haptic_ == XR_NULL_HANDLE || hand < kLeft || hand > kRight || !(seconds > 0.0f) ||
        !(amplitude > 0.0f)) {
        return false;
    }
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic_;
    info.subactionPath = hands_[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = static_cast<XrDuration>(static_cast<double>(seconds) * 1e9);
    vibration.frequency = frequencyHz > 0.0f ? frequencyHz : XR_FREQUENCY_UNSPECIFIED;
    vibration.amplitude = amplitude > 1.0f ? 1.0f : amplitude;
    return Check(xrApplyHapticFeedback(target.session, &info, reinterpret_cast<XrHapticBaseHeader const*>(&vibration)),
        "xrApplyHapticFeedback");
}

std::string Input::InteractionProfile(Target const& target, int hand) const {
    if (!Matches(target) || hand < kLeft || hand > kRight) {
        return {};
    }
    XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
    if (XR_FAILED(xrGetCurrentInteractionProfile(target.session, hands_[hand], &state)) ||
        state.interactionProfile == XR_NULL_PATH) {
        return {};
    }
    char text[XR_MAX_PATH_LENGTH]{};
    std::uint32_t length = 0;
    if (XR_FAILED(xrPathToString(target.instance, state.interactionProfile, XR_MAX_PATH_LENGTH, &length, text))) {
        return {};
    }
    return text;
}

bool Input::StopVibration(Target const& target, int hand) {
    if (!Matches(target) || haptic_ == XR_NULL_HANDLE || hand < kLeft || hand > kRight) {
        return false;
    }
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic_;
    info.subactionPath = hands_[hand];
    return Check(xrStopHapticFeedback(target.session, &info), "xrStopHapticFeedback");
}

} // namespace mevr::xr
