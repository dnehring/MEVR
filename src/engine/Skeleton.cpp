#include "engine/Skeleton.hpp"

#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

namespace mevr::engine::skeleton {
namespace {

constexpr char const* kGetBoneNames = "Function Engine.SkeletalMeshComponent.GetBoneNames";
constexpr char const* kGetBoneLocation = "Function Engine.SkeletalMeshComponent.GetBoneLocation";
constexpr char const* kMatchRefBone = "Function Engine.SkeletalMeshComponent.MatchRefBone";

UFunction* gGetBoneNames = nullptr;
UFunction* gGetBoneLocation = nullptr;
UFunction* gMatchRefBone = nullptr;

} // namespace

bool TryStartup() {
    gGetBoneNames = FindFunction(L"GetBoneNames", kGetBoneNames);
    gGetBoneLocation = FindFunction(L"GetBoneLocation", kGetBoneLocation);
    gMatchRefBone = FindFunction(L"MatchRefBone", kMatchRefBone);
    return gGetBoneNames != nullptr && gGetBoneLocation != nullptr && gMatchRefBone != nullptr;
}

void Shutdown() {
    gGetBoneNames = nullptr;
    gGetBoneLocation = nullptr;
    gMatchRefBone = nullptr;
}

std::vector<SFXName> BoneNames(USkeletalMeshComponent* mesh) {
    if (gGetBoneNames == nullptr || mesh == nullptr) {
        return {};
    }
    USkeletalMeshComponent_execGetBoneNames_Parms parms;
    mesh->ProcessEvent(gGetBoneNames, &parms, nullptr);
    std::vector<SFXName> names(parms.BoneNames.GetData(), parms.BoneNames.GetData() + parms.BoneNames.Count());
    parms.BoneNames.~TArray();  // the parms union doesn't free it
    return names;
}

std::optional<UnrealVector> BoneLocation(USkeletalMeshComponent* mesh, SFXName const& bone, Space space) {
    if (gGetBoneLocation == nullptr || gMatchRefBone == nullptr || mesh == nullptr) {
        return std::nullopt;
    }
    USkeletalMeshComponent_execMatchRefBone_Parms match;
    match.BoneName = bone;
    mesh->ProcessEvent(gMatchRefBone, &match, nullptr);
    if (match.ReturnValue < 0 || match.ReturnValue >= static_cast<int>(mesh->SpaceBases.Count())) {
        return std::nullopt;  // not this skeleton's bone, or not posed yet
    }
    USkeletalMeshComponent_execGetBoneLocation_Parms parms;
    parms.BoneName = bone;
    parms.Space = static_cast<int>(space);
    mesh->ProcessEvent(gGetBoneLocation, &parms, nullptr);
    return UnrealVector{parms.ReturnValue.X, parms.ReturnValue.Y, parms.ReturnValue.Z};
}

} // namespace mevr::engine::skeleton
