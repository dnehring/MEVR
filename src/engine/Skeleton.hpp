#pragma once

#include "engine/XrToUnreal.hpp"

#include <optional>
#include <vector>

class USkeletalMeshComponent;
struct SFXName;

// Bone queries on a skeletal mesh component, through the engine's own script natives (called
// via ProcessEvent: the SDK's generated wrappers corrupt FunctionFlags). Game thread.
namespace mevr::engine::skeleton {

// Finds the natives. False until the script packages have loaded.
bool TryStartup();
void Shutdown();

std::vector<SFXName> BoneNames(USkeletalMeshComponent* mesh);

// World space goes through the engine's bounds-checked bone lookup. Component space indexes the
// mesh's pose array directly, so it's only answered once the pose covers every bone.
enum class Space { World = 0, Component = 1 };
std::optional<UnrealVector> BoneLocation(USkeletalMeshComponent* mesh, SFXName const& bone, Space space);

} // namespace mevr::engine::skeleton
