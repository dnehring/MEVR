// SPDX-License-Identifier: MIT
#include "core/xr/Floor.hpp"

#include "core/Log.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace mevr::xr {
namespace {

char const* SpaceName(XrReferenceSpaceType type) {
    switch (type) {
    case XR_REFERENCE_SPACE_TYPE_VIEW: return "VIEW";
    case XR_REFERENCE_SPACE_TYPE_LOCAL: return "LOCAL";
    case XR_REFERENCE_SPACE_TYPE_STAGE: return "STAGE";
    case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR: return "LOCAL_FLOOR";
    case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT: return "UNBOUNDED_MSFT";
    default: break;
    }
    return "other";
}

XrSpace MakeSpace(XrSession session, XrReferenceSpaceType type) {
    XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    info.referenceSpaceType = type;
    info.poseInReferenceSpace.orientation.w = 1.0f;
    XrSpace space = XR_NULL_HANDLE;
    XrResult const result = xrCreateReferenceSpace(session, &info, &space);
    if (XR_FAILED(result)) {
        log::Warn("floor: xrCreateReferenceSpace({}) failed ({})", SpaceName(type), static_cast<int>(result));
        return XR_NULL_HANDLE;
    }
    return space;
}

} // namespace

bool FloorSpace::Create(XrSession session) {
    Forget();
    if (session == XR_NULL_HANDLE) {
        return false;
    }
    std::uint32_t count = 0;
    std::vector<XrReferenceSpaceType> types;
    if (XR_SUCCEEDED(xrEnumerateReferenceSpaces(session, 0, &count, nullptr)) && count > 0 && count < 64) {
        types.resize(count);
        if (XR_FAILED(xrEnumerateReferenceSpaces(session, count, &count, types.data()))) {
            types.clear();
        }
        types.resize(std::min<std::size_t>(types.size(), count));
    }
    std::string listed;
    for (XrReferenceSpaceType type : types) {
        listed += std::format("{}{}", listed.empty() ? "" : ", ", SpaceName(type));
    }
    auto const has = [&types](XrReferenceSpaceType type) { return std::find(types.begin(), types.end(), type) != types.end(); };
    XrReferenceSpaceType chosen = XR_REFERENCE_SPACE_TYPE_MAX_ENUM;
    if (has(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR)) {
        chosen = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
    } else if (has(XR_REFERENCE_SPACE_TYPE_STAGE)) {
        chosen = XR_REFERENCE_SPACE_TYPE_STAGE;
    }
    if (chosen == XR_REFERENCE_SPACE_TYPE_MAX_ENUM) {
        log::Warn("floor: the runtime offers no floor space (reference spaces: {}): the holsters keep [Holsters] eyeHeightM",
            listed.empty() ? "none listed" : listed);
        return false;
    }
    floor_ = MakeSpace(session, chosen);
    view_ = floor_ != XR_NULL_HANDLE ? MakeSpace(session, XR_REFERENCE_SPACE_TYPE_VIEW) : XR_NULL_HANDLE;
    if (floor_ == XR_NULL_HANDLE || view_ == XR_NULL_HANDLE) {
        if (floor_ != XR_NULL_HANDLE) {
            xrDestroySpace(floor_);
        }
        floor_ = XR_NULL_HANDLE;
        view_ = XR_NULL_HANDLE;
        return false;
    }
    session_ = session;
    source_ = chosen == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR ? vr::floor::Source::LocalFloor : vr::floor::Source::Stage;
    log::Info("floor: the head's height above the floor comes from {} (reference spaces: {})", SpaceName(chosen), listed);
    return true;
}

void FloorSpace::Forget() {
    session_ = XR_NULL_HANDLE;
    floor_ = XR_NULL_HANDLE;
    view_ = XR_NULL_HANDLE;
    source_ = vr::floor::Source::None;
}

std::optional<float> FloorSpace::EyeHeight(XrTime time) const {
    if (floor_ == XR_NULL_HANDLE || view_ == XR_NULL_HANDLE || time <= 0) {
        return std::nullopt;
    }
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(view_, floor_, time, &location))) {
        return std::nullopt;
    }
    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0) {
        return std::nullopt;
    }
    return location.pose.position.y;
}

} // namespace mevr::xr
