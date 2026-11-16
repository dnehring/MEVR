// SPDX-License-Identifier: MIT
#pragma once
#if defined(MEVR_GAME_LE3)
#include "game/Le3Galaxy.hpp"
#include "game/ViewAnchor.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include <algorithm>
#include <array>
#include <cctype>

// Read-only precursor to the CIC view hold. Resolve by native identity and tag,
// never by the serialized actor number/position or the map camera's coordinates.
// Called explicitly on the game thread through the existing console channel.
namespace mevr::game::le3_galaxy {
inline json::Value ProjectorVector(std::array<double, 3> const& value) {
    auto out = json::Value::Array();
    for (double x : value) out.Push(x);
    return out;
}
inline json::Value ProjectorStreaming(ue3::Object* wi) {
    // Same native fields as ME1's `mevr stream chunks/states`, scoped to the
    // already identified CIC art and map packages. No streaming setters here.
    auto out = json::Value::Object();
    out["available"] = false;
    ue3::FieldRef levels{"Engine.WorldInfo", "StreamingLevels", ue3::kEveryGame};
    auto const list = levels.ElementsOf(wi, 4096);
    auto& r = ue3::runtime::Get();
    auto* const cls = r.FindClass("Engine.LevelStreaming");
    if (!cls || !list || list->stride != sizeof(ue3::Object*)) return out;
    auto rows = json::Value::Array();
    for (int i = 0; i < list->count; ++i) {
        auto* const level = *static_cast<ue3::Object**>(list->At(i));
        if (!level) continue;
        if (!r.IsAlive(level) || !r.IsA(level, cls)) return out;
        auto const package = Read<ue3::Name>(level, "Engine.LevelStreaming", "PackageName");
        if (!package) return out;
        auto name = r.ToString(*package);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "bioa_nor_210cic" && name != "bioa_nor_200" && name != "bioa_nor_203agalaxymap") continue;
        auto row = json::Value::Object();
        row["package"] = name; row["descriptor"] = Identity(level);
        auto const loaded = Read<ue3::Object*>(level, "Engine.LevelStreaming", "LoadedLevel");
        if (loaded) {
            row["loaded"] = *loaded != nullptr;
            row["loadedObjectAlive"] = *loaded != nullptr && r.IsAlive(*loaded);
        }
        row["visible"] = Scalar(ReadBool(level, "Engine.LevelStreaming", "bIsVisible"));
        row["shouldLoad"] = Scalar(ReadBool(level, "Engine.LevelStreaming", "bShouldBeLoaded"));
        row["shouldShow"] = Scalar(ReadBool(level, "Engine.LevelStreaming", "bShouldBeVisible"));
        row["loadPending"] = Scalar(ReadBool(level, "Engine.LevelStreaming", "bHasLoadRequestPending"));
        row["unloadPending"] = Scalar(ReadBool(level, "Engine.LevelStreaming", "bHasUnloadRequestPending"));
        rows.Push(std::move(row));
    }
    out["levels"] = std::move(rows);
    out["available"] = true;
    ue3::FieldRef states{"SFXGame.BioWorldInfo", "m_WorldStreamingStates", ue3::kEveryGame};
    ue3::FieldRef stateName{states, "Name"}, stateEnabled{states, "Enabled"};
    auto const entries = states.ElementsOf(wi, 4096);
    auto stateRows = json::Value::Array();
    if (entries) {
        for (int i = 0; i < entries->count; ++i) {
            auto const value = stateName.Read<ue3::Name>(entries->At(i));
            if (!value) continue;
            auto const name = r.ToString(*value);
            if (name != "stream_inGalaxyMap" && name != "stream_GalaxyMap" && name != "stream_leaveMap2") continue;
            auto row = json::Value::Object(); row["name"] = name;
            row["enabled"] = Scalar(stateEnabled.ReadBool(entries->At(i)));
            stateRows.Push(std::move(row));
        }
    }
    out["statesAvailable"] = entries.has_value(); out["states"] = std::move(stateRows);
    return out;
}
inline json::Value ObserveProjector(ue3::Object* pc) {
    auto out = json::Value::Object();
    out["schemaVersion"] = 1; out["game"] = "LE3"; out["readOnly"] = true;
    out["available"] = false; out["trackingAvailable"] = false;
    out["presentationVerified"] = false; out["cameraHoldActive"] = false;
    out["reason"] = "no-current-player-world";
    if (!ue3::runtime::Up() || !ue3::runtime::IsAlive(pc) || pc != player::Controller()) return out;
    auto* const wi = Object(pc, "Engine.Actor", "WorldInfo");
    auto* const pawn = player::Pawn(pc);
    auto const mode = player::GameMode(pc);
    if (!wi || !pawn || !mode || Object(pawn, "Engine.Actor", "WorldInfo") != wi) return out;
    out["gameMode"] = *mode; out["world"] = Identity(wi);
    out["streaming"] = ProjectorStreaming(wi);
    out["reason"] = "not-on-foot-or-map";
    if (*mode != 0 && !engine::adapter::IsMapMode(*mode)) return out;
    auto& r = ue3::runtime::Get();
    auto* const interp = r.FindClass("Engine.InterpActor");
    auto* const point = r.FindClass("SFXGame.SFXPointOfInterest");
    auto* const componentClass = r.FindClass("Engine.StaticMeshComponent");
    out["reason"] = "missing-projector-classes";
    if (!interp || !point || !componentClass) return out;
    ue3::FieldRef tag{"Engine.Actor", "Tag", ue3::kEveryGame};
    ue3::Object* actor = nullptr;
    ue3::Object* use = nullptr;
    int projectorCount = 0, useCount = 0;
    for (int i = 0, count = r.ObjectCount(); i < count; ++i) {
        auto* const o = r.ObjectAt(i);
        if (!o || !r.IsAlive(o) || (!r.IsA(o, interp) && !r.IsA(o, point)) ||
            Object(o, "Engine.Actor", "WorldInfo") != wi || r.NameText(o).starts_with("Default__")) continue;
        auto const value = tag.Read<ue3::Name>(o);
        if (!value) continue;
        auto const name = r.ToString(*value);
        if (r.IsA(o, interp) && name == "GalaxyMapProxy") { actor = o; ++projectorCount; }
        if (r.IsA(o, point) && name == "placu_galaxyMap") { use = o; ++useCount; }
    }
    out["projectorCandidates"] = projectorCount; out["useCandidates"] = useCount;
    out["reason"] = "missing-or-ambiguous-cic-projector";
    if (projectorCount != 1 || useCount != 1) return out;
    auto* const component = Object(actor, "Engine.DynamicSMActor", "StaticMeshComponent");
    out["reason"] = "invalid-projector-component";
    if (!component || !r.IsA(component, componentClass) || r.OuterOf(component) != actor) return out;
    auto* const mesh = Object(component, "Engine.StaticMeshComponent", "StaticMesh");
    if (!mesh || r.PathName(mesh) != "BIOA_GalaxyMap_S.GalaxyMapProxy") return out;
    ue3::FieldRef bounds{"Engine.PrimitiveComponent", "Bounds", ue3::kEveryGame};
    ue3::FieldRef box{bounds, "BoxExtent"};
    auto const ext = box.Read<ue3::Vector>(bounds.Address(component));
    auto const matrix = Read<std::array<float, 16>>(component, "Engine.PrimitiveComponent", "LocalToWorld");
    out["reason"] = "invalid-projector-transform-or-bounds";
    if (!ext || !matrix) return out;
    for (float x : *matrix) if (!std::isfinite(x)) return out;
    auto const& m = *matrix;
    if (std::fabs(m[3]) > 1e-5f || std::fabs(m[7]) > 1e-5f || std::fabs(m[11]) > 1e-5f ||
        std::fabs(m[15] - 1) > 1e-5f) return out;
    std::array<double, 3> forward{m[0], m[1], m[2]}, right{m[4], m[5], m[6]};
    auto length = [](auto const& v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); };
    double const f = length(forward), s = length(right);
    // A nonuniformly scaled rectangle is not the square assumed by this measurement.
    if (f < 1e-6 || s < 1e-6 || std::fabs(f - s) > 1e-4 * std::max(f, s)) return out;
    for (double& x : forward) x /= f;
    for (double& x : right) x /= s;
    auto const radius = vr::galaxy_holo::ProjectorRadius({ext->x, ext->y, ext->z}, forward, right);
    if (!radius || *radius <= 50 || *radius >= 2000) return out;
    std::array<double, 3> const centre{m[12], m[13], m[14]};
    std::array<double, 3> const normal{forward[1]*right[2] - forward[2]*right[1],
        forward[2]*right[0] - forward[0]*right[2], forward[0]*right[1] - forward[1]*right[0]};
    // Bracket the read with current player/object ownership. No cached raw pointers
    // or retained room reference are published by this diagnostic.
    out["reason"] = "projector-context-changed";
    if (pc != player::Controller() || player::Pawn(pc) != pawn || player::GameMode(pc) != mode ||
        Object(pc, "Engine.Actor", "WorldInfo") != wi || Object(actor, "Engine.Actor", "WorldInfo") != wi ||
        Object(use, "Engine.Actor", "WorldInfo") != wi ||
        Object(actor, "Engine.DynamicSMActor", "StaticMeshComponent") != component ||
        Object(component, "Engine.StaticMeshComponent", "StaticMesh") != mesh) return out;
    out["world"] = Identity(wi); out["projector"] = Identity(actor); out["useTarget"] = Identity(use);
    out["component"] = Identity(component); out["mesh"] = Identity(mesh); out["gameMode"] = *mode;
    out["centreUU"] = ProjectorVector(centre); out["normal"] = ProjectorVector(normal);
    out["radiusUU"] = *radius;
    out["boundsExtentUU"] = ProjectorVector({ext->x, ext->y, ext->z});
    out["available"] = true; out["reason"] = "live-square-projector-measured-read-only";
    // The native map camera cannot teach a room anchor. Only a current, unheld
    // on-foot VR frame can produce a tracking-space measurement.
    if (*mode == 0 && player::FloorLockSample().cameraOwned && !view_anchor::Held()) {
        if (auto const frame = view_anchor::Current(); frame && std::isfinite(frame->unitsPerMeter) && frame->unitsPerMeter > 0) {
            auto const at = engine::ToTracking(*frame, {centre[0], centre[1], centre[2]});
            auto const up = engine::DirectionToTracking(*frame, {normal[0], normal[1], normal[2]});
            if (std::isfinite(at.x) && std::isfinite(at.y) && std::isfinite(at.z) &&
                std::isfinite(up.x) && std::isfinite(up.y) && std::isfinite(up.z)) {
                out["trackingCentreM"] = ProjectorVector({at.x, at.y, at.z});
                out["trackingNormal"] = ProjectorVector({up.x, up.y, up.z});
                out["trackingRadiusM"] = *radius / frame->unitsPerMeter;
                out["trackingAvailable"] = true;
            }
        }
    }
    return out;
}
} // namespace mevr::game::le3_galaxy
#endif
