#pragma once

#include "core/ik/Rigid.hpp"
#include "engine/XrToUnreal.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class UObject;
class USkeletalMeshComponent;
struct SFXName;

// A skeletal mesh's bones, read straight from engine memory. LE1 layouts, each checked against the
// code that uses them (research/14):
//   USkeletalMesh::RefSkeleton        +0xB4   TArray<FMeshBone>, 0x50 bytes each: Name +0x00,
//                                            ParentIndex +0x40 (execGetBoneName, execGetParentBoneIndex)
//   USkeletalMeshComponent::SpaceBases +0x2B0 TArray<FBoneAtom>, component space, 0x20 bytes each:
//                                            rotation (x, y, z, w), translation, scale
//                                            (UpdateSkelPose, GetBoneMatrix). The SDK says FMatrix: wrong.
//   UPrimitiveComponent::LocalToWorld  +0xC0  FMatrix
// Names can't come from the GetBoneNames native: through ProcessEvent its out-array parameter is
// never bound to the caller's parms (EX_NativeParm with a null result), so it returns nothing.
// LE2/LE3: layouts not verified; everything here answers "unsupported" there.
namespace mevr::engine::skeleton {

// This game's layouts are known (LE1).
bool Supported();
// Finds the natives used for cross-checks (MatchRefBone, GetBoneLocation, GetBoneNames). False
// until the script packages have loaded. Reading bones doesn't need them.
bool TryStartup();
void Shutdown();

// FBoneAtom as LE1 stores it.
struct BoneAtom {
    float qx, qy, qz, qw;
    float tx, ty, tz;
    float scale;
};
static_assert(sizeof(BoneAtom) == 32);

ik::Transform ToTransform(BoneAtom const& atom);
BoneAtom ToAtom(ik::Transform const& transform);

struct RefSkeleton {
    std::vector<std::string> names;  // UTF-8, e.g. "RightWrist"
    std::vector<std::uint64_t> keys;  // the raw names, for cheap comparisons
    std::vector<int> parents;        // -1 for the root
};

// The mesh asset's reference skeleton; nullopt if unsupported or the mesh has none.
std::optional<RefSkeleton> ReadRefSkeleton(USkeletalMeshComponent const* mesh);
// Number of bones in the mesh asset's skeleton (-1 if unsupported or no asset).
int RefBoneCount(USkeletalMeshComponent const* mesh);
// The raw name of bone `index` (0 if out of range): any thread, no allocation.
std::uint64_t RefBoneKey(USkeletalMeshComponent const* mesh, int index);
// The component-space pose (empty until the pose has been computed, or if unsupported).
std::span<BoneAtom> SpaceBases(USkeletalMeshComponent* mesh);
ik::Affine LocalToWorld(USkeletalMeshComponent const* mesh);
// The component this mesh takes its pose from (ParentAnimComponent), if any.
USkeletalMeshComponent* PoseParent(USkeletalMeshComponent const* mesh);

// The bone with this raw name in the mesh asset's skeleton (MatchRefBone from memory), or -1.
int FindBone(USkeletalMeshComponent const* mesh, std::uint64_t key);

// A socket of the mesh's asset (USkeletalMesh::Sockets +0x144, USkeletalMeshSocket: RelativeLocation
// +0x60, RelativeRotation +0x6C, SocketName +0x84, BoneName +0x8C; USkeletalMesh::FindSocket
// 0x5A8C70). `relative` places the socket in its bone's space the way the engine does
// (GetSocketWorldLocationAndRotation 0x584640: FRotationTranslationMatrix(RelativeRotation,
// RelativeLocation) * GetBoneMatrix(bone), no scale).
struct Socket {
    int bone = -1;
    std::uint64_t boneKey = 0;
    ik::Transform relative;
};
std::optional<Socket> FindSocket(USkeletalMeshComponent const* mesh, std::uint64_t socketName);
// Every socket of the mesh's asset whose bone resolves: its raw name and placement (empty if
// unsupported or none).
struct NamedSocket {
    std::uint64_t name = 0;
    Socket socket;
};
std::vector<NamedSocket> Sockets(USkeletalMeshComponent const* mesh);

// How `component` hangs on the mesh (USkeletalMeshComponent::Attachments +0x3B4, FAttachment 0x34
// bytes: Component, BoneName, RelativeLocation, RelativeRotation, RelativeScale), as
// AttachComponentToSocket (0x57A120) records a socket: its bone and offset. `relative` includes the
// scale only when it's uniform (`uniformScale`).
struct Attachment {
    int bone = -1;
    std::uint64_t boneKey = 0;
    ik::Transform relative;
    bool uniformScale = true;
};
std::optional<Attachment> FindAttachment(USkeletalMeshComponent const* mesh, UObject const* component);

// A UE3 rotator (Unreal units) as a rotation (FRotationMatrix's axes).
ik::Quat FromRotator(int pitch, int yaw, int roll);

enum class Space { World = 0, Component = 1 };
// From SpaceBases (and LocalToWorld for world space). Nullopt if the bone isn't posed.
std::optional<UnrealVector> BoneLocation(USkeletalMeshComponent* mesh, int index, Space space);
// The engine's own answer (MatchRefBone and GetBoneLocation through ProcessEvent): a cross-check
// of the layouts above. Game thread.
std::optional<UnrealVector> NativeBoneLocation(USkeletalMeshComponent* mesh, SFXName const& bone, Space space);
SFXName NameOf(std::uint64_t key);

// Why GetBoneNames returns nothing through ProcessEvent, as observed in this process: how many
// names it returned, its native and bytecode, and the EX_NativeParm handler. Game thread.
std::string GetBoneNamesDiagnostic(USkeletalMeshComponent* mesh);

} // namespace mevr::engine::skeleton
