// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"
#include "engine/XrToUnreal.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// A skeletal mesh's bones, read straight from engine memory. The fields are found through our
// reflection layer (research/34, stage 3: all four "native" ones are reflected); the self-check compares
// them with research/14's LE1 offsets (its `skeleton` rows):
//   USkeletalMesh::RefSkeleton        +0xB4   declared TArray<int>: native FMeshBone, 0x50 bytes each: Name
//                                            +0x00, ParentIndex +0x40 (execGetBoneName, execGetParentBoneIndex)
//   USkeletalMeshComponent::SpaceBases +0x2B0 declared TArray<Matrix>: FBoneAtom, component space, 0x20
//                                            bytes each: rotation (x, y, z, w), translation, scale
//                                            (UpdateSkelPose, GetBoneMatrix)
//   UPrimitiveComponent::LocalToWorld  +0xC0  FMatrix
// The two element layouts aren't in the reflection: they stay research/14's (LE1 only).
// Names can't come from the GetBoneNames native: through ProcessEvent its out-array parameter is
// never bound to the caller's parms (EX_NativeParm with a null result), so it returns nothing.
// LE2/LE3: layouts not verified; everything here answers "unsupported" there.
//
// Meshes are SkeletalMeshComponents as our reflection layer's pointers. The offsets are resolved once
// (TryStartup, game thread) and then only read, so the pose reads (RefBoneCount, RefBoneKey,
// SpaceBases) work from any thread.
namespace mevr::ue3 {
struct Object;
}

namespace mevr::engine::skeleton {

// This game's layouts are known (LE1).
bool Supported();
// Resolves the fields below through our reflection layer (once; what's missing is logged and stays
// off), and finds the natives used for cross-checks (MatchRefBone, GetBoneLocation, GetBoneNames).
// False until the script packages have loaded and the natives are found. Reading bones doesn't need
// them. Game thread.
bool TryStartup();
void Shutdown();

// The offsets this module reads, as TryStartup resolved them (-1: not found, or not resolved yet), for
// the self-check, which compares them with research/14's.
struct Fields {
    std::int32_t skeletalMesh = -1;         // SkeletalMeshComponent.SkeletalMesh
    std::int32_t spaceBases = -1;           // SkeletalMeshComponent.SpaceBases
    std::int32_t localToWorld = -1;         // PrimitiveComponent.LocalToWorld (through the component)
    std::int32_t parentAnimComponent = -1;  // SkeletalMeshComponent.ParentAnimComponent
    std::int32_t attachments = -1;          // SkeletalMeshComponent.Attachments
    std::int32_t refSkeleton = -1;          // SkeletalMesh.RefSkeleton
    std::int32_t sockets = -1;              // SkeletalMesh.Sockets
    std::int32_t socketName = -1;           // SkeletalMeshSocket.SocketName
    std::int32_t socketBone = -1;           // ...BoneName
    std::int32_t socketLocation = -1;       // ...RelativeLocation
    std::int32_t socketRotation = -1;       // ...RelativeRotation
    std::int32_t attachmentSize = -1;       // FAttachment, the Attachments array's element struct
    std::int32_t attachmentComponent = -1;  // ...its Component, BoneName, RelativeLocation, RelativeRotation, RelativeScale
    std::int32_t attachmentBone = -1;
    std::int32_t attachmentLocation = -1;
    std::int32_t attachmentRotation = -1;
    std::int32_t attachmentScale = -1;
};
Fields ResolvedFields();

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
std::optional<RefSkeleton> ReadRefSkeleton(ue3::Object const* mesh);
// Number of bones in the mesh asset's skeleton (-1 if unsupported or no asset).
int RefBoneCount(ue3::Object const* mesh);
// The raw name of bone `index` (0 if out of range): any thread, no allocation.
std::uint64_t RefBoneKey(ue3::Object const* mesh, int index);
// The component-space pose (empty until the pose has been computed, or if unsupported).
std::span<BoneAtom> SpaceBases(ue3::Object* mesh);
ik::Affine LocalToWorld(ue3::Object const* mesh);
// The component this mesh takes its pose from (ParentAnimComponent), if any.
ue3::Object* PoseParent(ue3::Object const* mesh);

// The bone with this raw name in the mesh asset's skeleton (MatchRefBone from memory), or -1.
int FindBone(ue3::Object const* mesh, std::uint64_t key);

// A socket of the mesh's asset (USkeletalMesh::Sockets; USkeletalMeshSocket: RelativeLocation,
// RelativeRotation, SocketName, BoneName; research/14 read them at +0x144, +0x60, +0x6C, +0x84, +0x8C;
// USkeletalMesh::FindSocket 0x5A8C70). `relative` places the socket in its bone's space the way the engine does
// (GetSocketWorldLocationAndRotation 0x584640: FRotationTranslationMatrix(RelativeRotation,
// RelativeLocation) * GetBoneMatrix(bone), no scale).
struct Socket {
    int bone = -1;
    std::uint64_t boneKey = 0;
    ik::Transform relative;
};
std::optional<Socket> FindSocket(ue3::Object const* mesh, std::uint64_t socketName);
// Every socket of the mesh's asset whose bone resolves: its raw name and placement (empty if
// unsupported or none).
struct NamedSocket {
    std::uint64_t name = 0;
    Socket socket;
};
std::vector<NamedSocket> Sockets(ue3::Object const* mesh);

// How `component` hangs on the mesh (USkeletalMeshComponent::Attachments, research/14's +0x3B4;
// FAttachment 0x34 bytes: Component, BoneName, RelativeLocation, RelativeRotation, RelativeScale), as
// AttachComponentToSocket (0x57A120) records a socket: its bone and offset. `relative` includes the
// scale only when it's uniform (`uniformScale`).
struct Attachment {
    int bone = -1;
    std::uint64_t boneKey = 0;
    ik::Transform relative;
    bool uniformScale = true;
};
std::optional<Attachment> FindAttachment(ue3::Object const* mesh, ue3::Object const* component);

// A UE3 rotator (Unreal units) as a rotation (FRotationMatrix's axes).
ik::Quat FromRotator(int pitch, int yaw, int roll);

enum class Space { World = 0, Component = 1 };
// From SpaceBases (and LocalToWorld for world space). Nullopt if the bone isn't posed.
std::optional<UnrealVector> BoneLocation(ue3::Object* mesh, int index, Space space);
// The engine's own answer (MatchRefBone and GetBoneLocation through ProcessEvent): a cross-check
// of the layouts above. Game thread.
// `bone` is the bone's raw name (RefSkeleton::keys).
std::optional<UnrealVector> NativeBoneLocation(ue3::Object* mesh, std::uint64_t bone, Space space);

// Why GetBoneNames returns nothing through ProcessEvent, as observed in this process: how many
// names it returned, its native and bytecode, and the EX_NativeParm handler. Game thread.
std::string GetBoneNamesDiagnostic(ue3::Object* mesh);

} // namespace mevr::engine::skeleton
