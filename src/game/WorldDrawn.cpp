// SPDX-License-Identifier: MIT
#include "game/WorldDrawn.hpp"

#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

namespace mevr::game::world_drawn {
namespace {

ue3::FieldRef gLastRenderTime{"Engine.PrimitiveComponent", "LastRenderTime", ue3::kEveryGame};
ue3::Object const* gMesh = nullptr;  // the mesh the last value came from (a new pawn or mesh starts over)
float gLast = -1.0f;
std::uint64_t gMovedMs = 0;

} // namespace

void Tick() {
    if (!ue3::runtime::Up()) return;
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    ue3::Object* const mesh = pawn != nullptr && ue3::runtime::IsAlive(pawn) ? player::Mesh(pawn) : nullptr;
    auto const last = mesh != nullptr ? gLastRenderTime.Read<float>(mesh) : std::nullopt;
    if (!last) return;
    if (mesh == gMesh && gLast >= 0.0f && *last != gLast) gMovedMs = GetTickCount64();
    gMesh = mesh;
    gLast = *last;
}

bool Now(std::uint64_t withinMs) {
    return gMovedMs != 0 && GetTickCount64() - gMovedMs < withinMs;
}

} // namespace mevr::game::world_drawn
