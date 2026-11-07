// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/FloorLock.hpp"
#include "core/vr/HostView.hpp"

#include <string>

namespace mevr::host::floor_lock {
void SetPawn(vr::floor_lock::PawnSample const* sample);
void RefreshSettings(); // game thread, once a second
bool Enabled();
void Queue(vr::host_view::State const& view, vr::floor::State const& floor, bool ordinaryWorld);
void Reset(); // session stopped: invalidate queued and published XR views
float Translate(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward);
std::string StatusJson();
} // namespace mevr::host::floor_lock
