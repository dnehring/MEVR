#include "engine/Skeleton.hpp"

#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>

namespace mevr::engine::skeleton {
namespace {

constexpr char const* kGetBoneNames = "Function Engine.SkeletalMeshComponent.GetBoneNames";
constexpr char const* kGetBoneLocation = "Function Engine.SkeletalMeshComponent.GetBoneLocation";
constexpr char const* kMatchRefBone = "Function Engine.SkeletalMeshComponent.MatchRefBone";

UFunction* gGetBoneNames = nullptr;
UFunction* gGetBoneLocation = nullptr;
UFunction* gMatchRefBone = nullptr;

#if defined(SDK_TARGET_LE1)
constexpr bool kSupported = true;
constexpr std::size_t kMeshBoneStride = 0x50;       // sizeof(FMeshBone)
constexpr std::size_t kMeshBoneParentOffset = 0x40; // FMeshBone::ParentIndex
constexpr std::uintptr_t kGNativesRva = 0x16DD3C0;  // BUILTIN_GNATIVES_RVA
constexpr unsigned kExNativeParm = 0x29;            // EExprToken
constexpr unsigned kExLocalOutVariable = 0x48;
#else
constexpr bool kSupported = false;
constexpr std::size_t kMeshBoneStride = 0x50;  // unused: Bones() finds nothing
constexpr std::size_t kMeshBoneParentOffset = 0x40;
#endif

std::uint64_t Key(SFXName const& name) {
    std::uint64_t key = 0;
    std::memcpy(&key, &name, sizeof key);
    return key;
}

// The asset's RefSkeleton as raw bytes and a bone count.
struct RawBones {
    std::byte const* data = nullptr;
    int count = 0;
};

RawBones Bones(USkeletalMeshComponent const* mesh) {
#if defined(SDK_TARGET_LE1)
    if (mesh == nullptr || mesh->SkeletalMesh == nullptr) {
        return {};
    }
    auto const& array = mesh->SkeletalMesh->RefSkeleton;  // declared TArray<int>: native FMeshBone data
    auto const* const data = reinterpret_cast<std::byte const*>(array.GetData());
    int const count = static_cast<int>(array.Count());
    if (data == nullptr || count <= 0 || count > 4096) {
        return {};
    }
    return {data, count};
#else
    (void)mesh;
    return {};
#endif
}

SFXName NameAt(RawBones const& bones, int index) {
    SFXName name{};
    std::memcpy(&name, bones.data + static_cast<std::size_t>(index) * kMeshBoneStride, sizeof name);
    return name;
}

int ParentAt(RawBones const& bones, int index) {
    int parent = 0;
    std::memcpy(&parent, bones.data + static_cast<std::size_t>(index) * kMeshBoneStride + kMeshBoneParentOffset, sizeof parent);
    return parent;
}

} // namespace

bool Supported() {
    return kSupported;
}

bool TryStartup() {
    gGetBoneNames = FindFunction(L"GetBoneNames", kGetBoneNames);
    gGetBoneLocation = FindFunction(L"GetBoneLocation", kGetBoneLocation);
    gMatchRefBone = FindFunction(L"MatchRefBone", kMatchRefBone);
    return gGetBoneLocation != nullptr && gMatchRefBone != nullptr;
}

void Shutdown() {
    gGetBoneNames = nullptr;
    gGetBoneLocation = nullptr;
    gMatchRefBone = nullptr;
}

ik::Transform ToTransform(BoneAtom const& atom) {
    return {{atom.qx, atom.qy, atom.qz, atom.qw}, {atom.tx, atom.ty, atom.tz}, atom.scale};
}

BoneAtom ToAtom(ik::Transform const& t) {
    return {static_cast<float>(t.rotation.x), static_cast<float>(t.rotation.y), static_cast<float>(t.rotation.z),
        static_cast<float>(t.rotation.w), static_cast<float>(t.translation.x), static_cast<float>(t.translation.y),
        static_cast<float>(t.translation.z), static_cast<float>(t.scale)};
}

std::optional<RefSkeleton> ReadRefSkeleton(USkeletalMeshComponent const* mesh) {
    RawBones const bones = Bones(mesh);
    if (bones.data == nullptr) {
        return std::nullopt;
    }
    RefSkeleton skeleton;
    skeleton.names.reserve(bones.count);
    skeleton.keys.reserve(bones.count);
    skeleton.parents.reserve(bones.count);
    for (int i = 0; i < bones.count; ++i) {
        SFXName const name = NameAt(bones, i);
        skeleton.names.push_back(ToString(name));
        skeleton.keys.push_back(Key(name));
        // UE3 stores the root as its own parent (index 0).
        int const parent = ParentAt(bones, i);
        skeleton.parents.push_back(i == 0 ? -1 : parent);
    }
    return skeleton;
}

int RefBoneCount(USkeletalMeshComponent const* mesh) {
    RawBones const bones = Bones(mesh);
    return bones.data != nullptr ? bones.count : -1;
}

std::uint64_t RefBoneKey(USkeletalMeshComponent const* mesh, int index) {
    RawBones const bones = Bones(mesh);
    if (bones.data == nullptr || index < 0 || index >= bones.count) {
        return 0;
    }
    return Key(NameAt(bones, index));
}

std::span<BoneAtom> SpaceBases(USkeletalMeshComponent* mesh) {
#if defined(SDK_TARGET_LE1)
    if (mesh == nullptr) {
        return {};
    }
    auto& array = mesh->SpaceBases;  // declared TArray<FMatrix>: really FBoneAtom (see header)
    auto* const data = reinterpret_cast<BoneAtom*>(array.GetData());
    int const count = static_cast<int>(array.Count());
    if (data == nullptr || count <= 0) {
        return {};
    }
    return {data, static_cast<std::size_t>(count)};
#else
    (void)mesh;
    return {};
#endif
}

ik::Affine LocalToWorld(USkeletalMeshComponent const* mesh) {
    ik::Affine affine;
#if defined(SDK_TARGET_LE1)
    FMatrix const& m = mesh->LocalToWorld;
    FPlane const* const rows[4] = {&m.XPlane, &m.YPlane, &m.ZPlane, &m.WPlane};
    for (int r = 0; r < 4; ++r) {
        affine.m[r][0] = rows[r]->X;
        affine.m[r][1] = rows[r]->Y;
        affine.m[r][2] = rows[r]->Z;
        affine.m[r][3] = rows[r]->W;
    }
#else
    (void)mesh;
#endif
    return affine;
}

ik::Affine LocalToWorld(ue3::Object const* mesh) {
    return LocalToWorld(reinterpret_cast<USkeletalMeshComponent const*>(mesh));
}

USkeletalMeshComponent* PoseParent(USkeletalMeshComponent const* mesh) {
#if defined(SDK_TARGET_LE1)
    return mesh != nullptr ? mesh->ParentAnimComponent : nullptr;
#else
    (void)mesh;
    return nullptr;
#endif
}

int FindBone(USkeletalMeshComponent const* mesh, std::uint64_t key) {
    RawBones const bones = Bones(mesh);
    for (int i = 0; i < bones.count; ++i) {
        if (Key(NameAt(bones, i)) == key) {
            return i;
        }
    }
    return -1;
}

ik::Quat FromRotator(int pitch, int yaw, int roll) {
    Basis const axes = ToBasis(UnrealRotation{pitch, yaw, roll});
    return ik::FromAxes({axes.forward.x, axes.forward.y, axes.forward.z}, {axes.right.x, axes.right.y, axes.right.z},
        {axes.up.x, axes.up.y, axes.up.z});
}

#if defined(SDK_TARGET_LE1)
// Offsets each confirmed in the code that uses them (research/14, "Weapon and muzzle").
static_assert(offsetof(USkeletalMesh, Sockets) == 0x144);
static_assert(offsetof(USkeletalMeshSocket, RelativeLocation) == 0x60);
static_assert(offsetof(USkeletalMeshSocket, RelativeRotation) == 0x6C);
static_assert(offsetof(USkeletalMeshSocket, RelativeScale) == 0x78);
static_assert(offsetof(USkeletalMeshSocket, SocketName) == 0x84);
static_assert(offsetof(USkeletalMeshSocket, BoneName) == 0x8C);
static_assert(offsetof(USkeletalMeshComponent, Attachments) == 0x3B4);
static_assert(sizeof(FAttachment) == 0x34);
#endif

std::optional<Socket> FindSocket(USkeletalMeshComponent const* mesh, std::uint64_t socketName) {
#if defined(SDK_TARGET_LE1)
    if (mesh == nullptr || mesh->SkeletalMesh == nullptr || socketName == 0) {
        return std::nullopt;
    }
    auto const& sockets = mesh->SkeletalMesh->Sockets;
    int const count = static_cast<int>(sockets.Count());
    if (sockets.GetData() == nullptr || count <= 0 || count > 4096) {
        return std::nullopt;
    }
    for (int i = 0; i < count; ++i) {
        USkeletalMeshSocket const* const socket = sockets.GetData()[i];
        if (socket == nullptr || Key(socket->SocketName) != socketName) {
            continue;
        }
        Socket out;
        out.boneKey = Key(socket->BoneName);
        out.bone = FindBone(mesh, out.boneKey);
        if (out.bone < 0) {
            return std::nullopt;
        }
        FRotator const& r = socket->RelativeRotation;
        FVector const& t = socket->RelativeLocation;
        out.relative = {FromRotator(r.Pitch, r.Yaw, r.Roll), {t.X, t.Y, t.Z}, 1.0};
        return out;
    }
    return std::nullopt;
#else
    (void)mesh;
    (void)socketName;
    return std::nullopt;
#endif
}

std::vector<NamedSocket> Sockets(USkeletalMeshComponent const* mesh) {
    std::vector<NamedSocket> out;
#if defined(SDK_TARGET_LE1)
    if (mesh == nullptr || mesh->SkeletalMesh == nullptr) {
        return out;
    }
    auto const& sockets = mesh->SkeletalMesh->Sockets;
    int const count = static_cast<int>(sockets.Count());
    if (sockets.GetData() == nullptr || count <= 0 || count > 4096) {
        return out;
    }
    for (int i = 0; i < count; ++i) {
        USkeletalMeshSocket const* const socket = sockets.GetData()[i];
        if (socket == nullptr) {
            continue;
        }
        if (auto found = FindSocket(mesh, Key(socket->SocketName))) {
            out.push_back({Key(socket->SocketName), *found});
        }
    }
#else
    (void)mesh;
#endif
    return out;
}

std::optional<Attachment> FindAttachment(USkeletalMeshComponent const* mesh, UObject const* component) {
#if defined(SDK_TARGET_LE1)
    if (mesh == nullptr || component == nullptr) {
        return std::nullopt;
    }
    auto const& attachments = mesh->Attachments;
    int const count = static_cast<int>(attachments.Count());
    if (attachments.GetData() == nullptr || count <= 0 || count > 4096) {
        return std::nullopt;
    }
    for (int i = 0; i < count; ++i) {
        FAttachment const& entry = attachments.GetData()[i];
        if (static_cast<UObject const*>(entry.Component) != component) {
            continue;
        }
        Attachment out;
        out.boneKey = Key(entry.BoneName);
        out.bone = FindBone(mesh, out.boneKey);
        if (out.bone < 0) {
            return std::nullopt;
        }
        FRotator const& r = entry.RelativeRotation;
        FVector const& t = entry.RelativeLocation;
        FVector const& s = entry.RelativeScale;
        // UE3 treats a zero scale as 1.
        double scale = 1.0;
        if (s.X != 0.0f || s.Y != 0.0f || s.Z != 0.0f) {
            scale = s.X;
            out.uniformScale = std::abs(s.X - s.Y) < 1e-4f && std::abs(s.X - s.Z) < 1e-4f;
        }
        out.relative = {FromRotator(r.Pitch, r.Yaw, r.Roll), {t.X, t.Y, t.Z}, out.uniformScale ? scale : 1.0};
        return out;
    }
    return std::nullopt;
#else
    (void)mesh;
    (void)component;
    return std::nullopt;
#endif
}

std::optional<UnrealVector> BoneLocation(USkeletalMeshComponent* mesh, int index, Space space) {
    std::span<BoneAtom> const pose = SpaceBases(mesh);
    if (index < 0 || static_cast<std::size_t>(index) >= pose.size() || static_cast<int>(pose.size()) != RefBoneCount(mesh)) {
        return std::nullopt;
    }
    ik::Vec3 at{pose[index].tx, pose[index].ty, pose[index].tz};
    if (space == Space::World) {
        at = ik::TransformPoint(LocalToWorld(mesh), at);
    }
    return UnrealVector{at.x, at.y, at.z};
}

std::optional<UnrealVector> NativeBoneLocation(USkeletalMeshComponent* mesh, SFXName const& bone, Space space) {
    if (gGetBoneLocation == nullptr || gMatchRefBone == nullptr || mesh == nullptr) {
        return std::nullopt;
    }
    // Called directly: the SDK's generated wrappers corrupt FunctionFlags (CLAUDE.md). Both
    // natives return through Result, which ProcessEvent points into the parms, so they work.
    USkeletalMeshComponent_execMatchRefBone_Parms match;
    match.BoneName = bone;
    match.ReturnValue = -1;  // stays -1 if the engine rejects the call
    mesh->ProcessEvent(gMatchRefBone, &match, nullptr);
    // GetBoneLocation indexes SpaceBases unchecked: only for a bone whose pose exists.
    if (match.ReturnValue < 0 || match.ReturnValue >= static_cast<int>(SpaceBases(mesh).size())) {
        return std::nullopt;
    }
    USkeletalMeshComponent_execGetBoneLocation_Parms parms;
    parms.BoneName = bone;
    parms.Space = static_cast<int>(space);
    mesh->ProcessEvent(gGetBoneLocation, &parms, nullptr);
    return UnrealVector{parms.ReturnValue.X, parms.ReturnValue.Y, parms.ReturnValue.Z};
}

SFXName NameOf(std::uint64_t key) {
    SFXName name{};
    std::memcpy(&name, &key, sizeof name);
    return name;
}

std::string GetBoneNamesDiagnostic(USkeletalMeshComponent* mesh) {
    if (gGetBoneNames == nullptr || mesh == nullptr) {
        return "GetBoneNames not found";
    }
    USkeletalMeshComponent_execGetBoneNames_Parms parms;
    mesh->ProcessEvent(gGetBoneNames, &parms, nullptr);
    int const returned = static_cast<int>(parms.BoneNames.Count());
    parms.BoneNames.~TArray();  // the parms union doesn't free it
    std::string text = std::format("GetBoneNames through ProcessEvent returned {} names (RefSkeleton has {})", returned,
        RefBoneCount(mesh));
#if defined(SDK_TARGET_LE1)
    auto const base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto const func = reinterpret_cast<std::uintptr_t>(gGetBoneNames->Func);
    text += std::format("; native exe+0x{:X} (execGetBoneNames 0x594950 expected), flags 0x{:X}, iNative {}", func - base,
        static_cast<unsigned>(gGetBoneNames->FunctionFlags), gGetBoneNames->iNative);
    // The native's own bytecode: what ProcessEvent's frame steps through for its parameters.
    auto const& script = gGetBoneNames->Script;
    std::string bytes;
    for (int i = 0; i < static_cast<int>(script.Count()) && i < 12; ++i) {
        bytes += std::format("{:02x} ", script.GetData()[i]);
    }
    text += std::format("; bytecode {}({} bytes; 0x29 = EX_NativeParm)", bytes, script.Count());
    auto const* const natives = reinterpret_cast<std::uintptr_t const*>(base + kGNativesRva);
    text += std::format("; GNatives[EX_NativeParm] exe+0x{:X} (0x11F210: binds an out parm only for a non-null result), "
                        "GNatives[EX_LocalOutVariable] exe+0x{:X}",
        natives[kExNativeParm] - base, natives[kExLocalOutVariable] - base);
#endif
    return text;
}

} // namespace mevr::engine::skeleton
