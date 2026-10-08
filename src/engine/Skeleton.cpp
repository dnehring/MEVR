#include "engine/Skeleton.hpp"

#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

namespace mevr::engine::skeleton {
namespace {

constexpr char const* kGetBoneNames = "Function Engine.SkeletalMeshComponent.GetBoneNames";
constexpr char const* kGetBoneLocation = "Function Engine.SkeletalMeshComponent.GetBoneLocation";

UFunction* gGetBoneNames = nullptr;
UFunction* gGetBoneLocation = nullptr;

} // namespace

bool TryStartup() {
    gGetBoneNames = FindFunction(L"GetBoneNames", kGetBoneNames);
    gGetBoneLocation = FindFunction(L"GetBoneLocation", kGetBoneLocation);
    return gGetBoneNames != nullptr && gGetBoneLocation != nullptr;
}

void Shutdown() {
    gGetBoneNames = nullptr;
    gGetBoneLocation = nullptr;
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
    if (gGetBoneLocation == nullptr || mesh == nullptr) {
        return std::nullopt;
    }
    if (space == Space::Component && mesh->SpaceBases.Count() == 0) {
        return std::nullopt;  // no pose yet: component space would index an empty array
    }
    USkeletalMeshComponent_execGetBoneLocation_Parms parms;
    parms.BoneName = bone;
    parms.Space = static_cast<int>(space);
    mesh->ProcessEvent(gGetBoneLocation, &parms, nullptr);
    return UnrealVector{parms.ReturnValue.X, parms.ReturnValue.Y, parms.ReturnValue.Z};
}

} // namespace mevr::engine::skeleton
