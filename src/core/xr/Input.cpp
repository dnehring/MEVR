#include "core/xr/Input.hpp"

#include "core/Log.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace mevr::xr {
namespace {

XrPath ToPath(XrInstance instance, char const* text) {
    XrPath path = XR_NULL_PATH;
    xrStringToPath(instance, text, &path);
    return path;
}

XrAction CreateAction(Session& session, XrActionSet set, XrActionType type, char const* name,
    char const* localized, std::array<XrPath, 2> const& hands) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.countSubactionPaths = static_cast<std::uint32_t>(hands.size());
    info.subactionPaths = hands.data();
    XrAction action = XR_NULL_HANDLE;
    if (!session.Check(xrCreateAction(set, &info, &action), name)) {
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
bool Suggest(Session& session, char const* profile, std::vector<Binding> const& bindings) {
    XrInstance const instance = session.Instance();
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

bool Input::Create(Session& session) {
    XrInstance const instance = session.Instance();
    hands_ = {ToPath(instance, "/user/hand/left"), ToPath(instance, "/user/hand/right")};

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName, "gameplay");
    std::strcpy(setInfo.localizedActionSetName, "Gameplay");
    if (!session.Check(xrCreateActionSet(instance, &setInfo, &set_), "xrCreateActionSet")) {
        set_ = XR_NULL_HANDLE;
        return false;
    }
    instanceGeneration_ = session.InstanceGeneration();
    sessionGeneration_ = session.Generation();
    created_ = true;  // from here on, Release cleans up whatever exists

    trigger_ = CreateAction(session, set_, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", hands_);
    squeeze_ = CreateAction(session, set_, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Grip", hands_);
    stick_ = CreateAction(session, set_, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", hands_);
    stickClick_ = CreateAction(session, set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick click", hands_);
    primary_ = CreateAction(session, set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "A / X", hands_);
    secondary_ = CreateAction(session, set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "B / Y", hands_);
    menu_ = CreateAction(session, set_, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", hands_);
    gripPose_ = CreateAction(session, set_, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose", hands_);
    aimPose_ = CreateAction(session, set_, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose", hands_);
    for (XrAction action : {trigger_, squeeze_, stick_, stickClick_, primary_, secondary_, menu_, gripPose_, aimPose_}) {
        if (action == XR_NULL_HANDLE) {
            Release(session);
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
    }

    std::vector<Binding> simple;
    for (char const* hand : {"left", "right"}) {
        Bind(simple, trigger_, hand, "/input/select/click");
        Bind(simple, menu_, hand, "/input/menu/click");
        Bind(simple, gripPose_, hand, "/input/grip/pose");
        Bind(simple, aimPose_, hand, "/input/aim/pose");
    }

    bool const touchOk = Suggest(session, "/interaction_profiles/oculus/touch_controller", touch);
    bool const indexOk = Suggest(session, "/interaction_profiles/valve/index_controller", index);
    bool const simpleOk = Suggest(session, "/interaction_profiles/khr/simple_controller", simple);
    if (!touchOk && !indexOk && !simpleOk) {
        log::Error("OpenXR: no controller profile accepted; motion controllers unavailable");
        Release(session);
        return false;
    }

    for (int hand : {kLeft, kRight}) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.subactionPath = hands_[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        spaceInfo.action = gripPose_;
        session.Check(xrCreateActionSpace(session.Handle(), &spaceInfo, &gripSpaces_[hand]), "xrCreateActionSpace(grip)");
        spaceInfo.action = aimPose_;
        session.Check(xrCreateActionSpace(session.Handle(), &spaceInfo, &aimSpaces_[hand]), "xrCreateActionSpace(aim)");
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &set_;
    if (!session.Check(xrAttachSessionActionSets(session.Handle(), &attach), "xrAttachSessionActionSets")) {
        Release(session);
        return false;
    }
    loggedFocus_ = false;
    log::Info("OpenXR: controller actions ready");
    return true;
}

void Input::Release(Session& session) {
    if (!created_) {
        return;
    }
    bool const sessionAlive = session.Handle() != XR_NULL_HANDLE && session.Generation() == sessionGeneration_;
    bool const instanceAlive = session.HasInstance() && session.InstanceGeneration() == instanceGeneration_;
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
    trigger_ = squeeze_ = stick_ = stickClick_ = primary_ = secondary_ = menu_ = gripPose_ = aimPose_ = XR_NULL_HANDLE;
    created_ = false;
}

bool Input::Matches(Session const& session) const {
    return created_ && session.Handle() != XR_NULL_HANDLE && session.Generation() == sessionGeneration_ &&
        session.InstanceGeneration() == instanceGeneration_;
}

void Input::Sync(Session& session, XrTime time, vr::controllers::State& out) {
    out = {};
    if (!Matches(session)) {
        return;
    }
    XrActiveActionSet active{set_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    XrResult const result = xrSyncActions(session.Handle(), &sync);
    if (result == XR_SESSION_NOT_FOCUSED) {
        if (!loggedFocus_) {
            log::Info("OpenXR: controllers idle (session not focused)");
            loggedFocus_ = true;
        }
        return;
    }
    if (!session.Check(result, "xrSyncActions")) {
        return;
    }
    loggedFocus_ = false;

    XrSession const handle = session.Handle();
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
        h.gripValid = Locate(gripSpaces_[hand], session.LocalSpace(), time, h.grip);
        h.aimValid = Locate(aimSpaces_[hand], session.LocalSpace(), time, h.aim);
    }
    auto const pressed = [](vr::controllers::Hand const& h) {
        return h.trigger > 0.0f || h.squeeze > 0.0f || h.stickX != 0.0f || h.stickY != 0.0f || h.stickClick ||
            h.primary || h.secondary || h.menu;
    };
    // Tracked, or used while tracking is lost (buttons still work then).
    out.active = out.left.gripValid || out.right.gripValid || pressed(out.left) || pressed(out.right);
}

} // namespace mevr::xr
