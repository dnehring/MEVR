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

// Only answered for a bone this mesh has (MatchRefBone) whose pose has been computed (its index is
// within SpaceBases): otherwise the engine returns the origin, or reads past the pose array.
enum class Space { World = 0, Component = 1 };
std::optional<UnrealVector> BoneLocation(USkeletalMeshComponent* mesh, SFXName const& bone, Space space);

} // namespace mevr::engine::skeleton
