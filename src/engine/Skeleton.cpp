// SPDX-License-Identifier: MIT
#include "engine/Skeleton.hpp"

#include "core/Log.hpp"
#include "engine/GameAdapter.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>

namespace mevr::engine::skeleton {
namespace {

ue3::FunctionRef gGetBoneNames{"Engine.SkeletalMeshComponent.GetBoneNames"};
ue3::FunctionRef gGetBoneLocation{"Engine.SkeletalMeshComponent.GetBoneLocation"};
ue3::FunctionRef gMatchRefBone{"Engine.SkeletalMeshComponent.MatchRefBone"};

// The bones' element layouts aren't in the reflection (RefSkeleton is declared TArray<int>, SpaceBases
// TArray<Matrix>): research/14's, LE1 only.
#if defined(MEVR_GAME_LE1)
constexpr bool kSupported = true;
#else
constexpr bool kSupported = false;
#endif
constexpr std::size_t kMeshBoneStride = 0x50;       // sizeof(FMeshBone)
constexpr std::size_t kMeshBoneParentOffset = 0x40; // FMeshBone::ParentIndex
constexpr unsigned kExNativeParm = 0x29;            // EExprToken
constexpr unsigned kExLocalOutVariable = 0x48;

// Resolved once on the game thread (TryStartup), then only read: the UpdateSkelPose hook reads the
// pose through these from whatever thread it runs on, so nothing here resolves lazily.
Fields gFields;
std::atomic<bool> gFieldsReady{false};
bool gToldMissing = false;

Fields const* Ready() {
    return gFieldsReady.load(std::memory_order_acquire) ? &gFields : nullptr;
}

template <class T>
T Read(void const* base, std::int32_t offset) {
    return ue3::Reflection::Read<T>(base, static_cast<std::uint32_t>(offset));
}

// A property's offset if it's there with this kind and size (size 0: any), else -1; names what's missing.
std::int32_t Offset(ue3::Reflection& r, ue3::Struct const* owner, char const* name, std::initializer_list<ue3::Kind> kinds,
                    std::int32_t size, std::string& missing, std::optional<ue3::PropertyInfo>* out = nullptr) {
    std::optional<ue3::PropertyInfo> const info = r.PropertyOf(owner, name);
    bool kindOk = false;
    for (ue3::Kind kind : kinds) {
        kindOk = kindOk || (info && info->kind == kind);
    }
    if (!info || !kindOk || (size != 0 && info->elementSize * info->arrayDim != size)) {
        missing += std::format("{}{}", missing.empty() ? "" : ", ", name);
        return -1;
    }
    if (out != nullptr) {
        *out = info;
    }
    return info->offset;
}

void ResolveFields() {
    if (gFieldsReady.load(std::memory_order_acquire) || !ue3::runtime::Ready()) {
        return;
    }
    using ue3::Kind;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const component = r.FindClass("Engine.SkeletalMeshComponent");
    ue3::Class* const asset = r.FindClass("Engine.SkeletalMesh");
    ue3::Class* const socket = r.FindClass("Engine.SkeletalMeshSocket");
    ue3::runtime::FlushNotes();
    std::string missing;
    Fields f;
    f.skeletalMesh = Offset(r, component, "SkeletalMesh", {Kind::Object}, 8, missing);
    f.spaceBases = Offset(r, component, "SpaceBases", {Kind::Array}, 16, missing);
    f.localToWorld = Offset(r, component, "LocalToWorld", {Kind::Struct}, 64, missing);
    f.parentAnimComponent = Offset(r, component, "ParentAnimComponent", {Kind::Object, Kind::Component}, 8, missing);
    std::optional<ue3::PropertyInfo> attachments;
    f.attachments = Offset(r, component, "Attachments", {Kind::Array}, 16, missing, &attachments);
    f.refSkeleton = Offset(r, asset, "RefSkeleton", {Kind::Array}, 16, missing);
    f.sockets = Offset(r, asset, "Sockets", {Kind::Array}, 16, missing);
    f.socketName = Offset(r, socket, "SocketName", {Kind::Name}, 8, missing);
    f.socketBone = Offset(r, socket, "BoneName", {Kind::Name}, 8, missing);
    f.socketLocation = Offset(r, socket, "RelativeLocation", {Kind::Struct}, 12, missing);
    f.socketRotation = Offset(r, socket, "RelativeRotation", {Kind::Struct}, 12, missing);
    // FAttachment: the array's element struct.
    std::optional<ue3::PropertyInfo> const element =
        attachments ? r.Describe(static_cast<ue3::Property const*>(attachments->typed)) : std::nullopt;
    ue3::Struct* const entry = element ? r.StructOf(*element) : nullptr;
    if (entry != nullptr) {
        f.attachmentSize = element->elementSize;
        f.attachmentComponent = Offset(r, entry, "Component", {Kind::Object, Kind::Component}, 8, missing);
        f.attachmentBone = Offset(r, entry, "BoneName", {Kind::Name}, 8, missing);
        f.attachmentLocation = Offset(r, entry, "RelativeLocation", {Kind::Struct}, 12, missing);
        f.attachmentRotation = Offset(r, entry, "RelativeRotation", {Kind::Struct}, 12, missing);
        f.attachmentScale = Offset(r, entry, "RelativeScale", {Kind::Struct}, 12, missing);
    } else if (f.attachments >= 0) {
        missing += std::format("{}Attachments' element struct", missing.empty() ? "" : ", ");
    }
    if (!missing.empty() && !gToldMissing) {
        gToldMissing = true;
        log::Warn("skeleton: {} not found through our reflection layer: what reads them stays off", missing);
    }
    gFields = f;
    gFieldsReady.store(true, std::memory_order_release);
}

// The asset's RefSkeleton as raw bytes and a bone count.
struct RawBones {
    std::byte const* data = nullptr;
    int count = 0;
};

RawBones Bones(ue3::Object const* mesh) {
    Fields const* const f = Ready();
    if (!kSupported || f == nullptr || mesh == nullptr || f->skeletalMesh < 0 || f->refSkeleton < 0) {
        return {};
    }
    auto const* const asset = Read<ue3::Object const*>(mesh, f->skeletalMesh);
    if (asset == nullptr) {
        return {};
    }
    auto const array = Read<ue3::TArray<std::byte>>(asset, f->refSkeleton);  // native FMeshBone data
    if (array.data == nullptr || array.count <= 0 || array.count > 4096) {
        return {};
    }
    return {array.data, array.count};
}

std::uint64_t NameAt(RawBones const& bones, int index) {
    std::uint64_t name = 0;
    std::memcpy(&name, bones.data + static_cast<std::size_t>(index) * kMeshBoneStride, sizeof name);
    return name;
}

int ParentAt(RawBones const& bones, int index) {
    int parent = 0;
    std::memcpy(&parent, bones.data + static_cast<std::size_t>(index) * kMeshBoneStride + kMeshBoneParentOffset, sizeof parent);
    return parent;
}

ue3::TArray<ue3::Object*> SocketsOf(Fields const& f, ue3::Object const* mesh) {
    if (mesh == nullptr || f.skeletalMesh < 0 || f.sockets < 0 || f.socketName < 0) {
        return {};
    }
    auto const* const asset = Read<ue3::Object const*>(mesh, f.skeletalMesh);
    if (asset == nullptr) {
        return {};
    }
    auto const sockets = Read<ue3::TArray<ue3::Object*>>(asset, f.sockets);
    if (sockets.data == nullptr || sockets.count <= 0 || sockets.count > 4096) {
        return {};
    }
    return sockets;
}

} // namespace

bool Supported() {
    return kSupported;
}

bool TryStartup() {
    ResolveFields();
    return gGetBoneLocation.Get() != nullptr && gMatchRefBone.Get() != nullptr;
}

void Shutdown() {
    gFieldsReady.store(false, std::memory_order_release);
    gFields = {};
    gToldMissing = false;
    gGetBoneNames.Reset();
    gGetBoneLocation.Reset();
    gMatchRefBone.Reset();
}

Fields ResolvedFields() {
    Fields const* const f = Ready();
    return f != nullptr ? *f : Fields{};
}

ik::Transform ToTransform(BoneAtom const& atom) {
    return {{atom.qx, atom.qy, atom.qz, atom.qw}, {atom.tx, atom.ty, atom.tz}, atom.scale};
}

BoneAtom ToAtom(ik::Transform const& t) {
    return {static_cast<float>(t.rotation.x), static_cast<float>(t.rotation.y), static_cast<float>(t.rotation.z),
        static_cast<float>(t.rotation.w), static_cast<float>(t.translation.x), static_cast<float>(t.translation.y),
        static_cast<float>(t.translation.z), static_cast<float>(t.scale)};
}

std::optional<RefSkeleton> ReadRefSkeleton(ue3::Object const* mesh) {
    RawBones const bones = Bones(mesh);
    if (bones.data == nullptr) {
        return std::nullopt;
    }
    RefSkeleton skeleton;
    skeleton.names.reserve(bones.count);
    skeleton.keys.reserve(bones.count);
    skeleton.parents.reserve(bones.count);
    for (int i = 0; i < bones.count; ++i) {
        std::uint64_t const name = NameAt(bones, i);
        skeleton.names.push_back(ue3::runtime::NameText(name));
        skeleton.keys.push_back(name);
        // UE3 stores the root as its own parent (index 0).
        int const parent = ParentAt(bones, i);
        skeleton.parents.push_back(i == 0 ? -1 : parent);
    }
    return skeleton;
}

int RefBoneCount(ue3::Object const* mesh) {
    RawBones const bones = Bones(mesh);
    return bones.data != nullptr ? bones.count : -1;
}

std::uint64_t RefBoneKey(ue3::Object const* mesh, int index) {
    RawBones const bones = Bones(mesh);
    if (bones.data == nullptr || index < 0 || index >= bones.count) {
        return 0;
    }
    return NameAt(bones, index);
}

std::span<BoneAtom> SpaceBases(ue3::Object* mesh) {
    Fields const* const f = Ready();
    if (!kSupported || f == nullptr || mesh == nullptr || f->spaceBases < 0) {
        return {};
    }
    // Declared TArray<Matrix>: really FBoneAtom (see the header).
    auto const array = Read<ue3::TArray<BoneAtom>>(mesh, f->spaceBases);
    if (array.data == nullptr || array.count <= 0) {
        return {};
    }
    return {array.data, static_cast<std::size_t>(array.count)};
}

ik::Affine LocalToWorld(ue3::Object const* mesh) {
    ik::Affine affine;
    Fields const* const f = Ready();
    if (!kSupported || f == nullptr || mesh == nullptr || f->localToWorld < 0) {
        return affine;
    }
    // FMatrix: four FPlanes (X, Y, Z, W), row by row.
    float m[16];
    std::memcpy(m, static_cast<std::uint8_t const*>(static_cast<void const*>(mesh)) + f->localToWorld, sizeof m);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            affine.m[r][c] = m[r * 4 + c];
        }
    }
    return affine;
}

ue3::Object* PoseParent(ue3::Object const* mesh) {
    Fields const* const f = Ready();
    if (!kSupported || f == nullptr || mesh == nullptr || f->parentAnimComponent < 0) {
        return nullptr;
    }
    return Read<ue3::Object*>(mesh, f->parentAnimComponent);
}

int FindBone(ue3::Object const* mesh, std::uint64_t key) {
    RawBones const bones = Bones(mesh);
    for (int i = 0; i < bones.count; ++i) {
        if (NameAt(bones, i) == key) {
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

std::optional<Socket> FindSocket(ue3::Object const* mesh, std::uint64_t socketName) {
    Fields const* const f = Ready();
    if (!kSupported || f == nullptr || socketName == 0 || f->socketBone < 0 || f->socketLocation < 0 || f->socketRotation < 0) {
        return std::nullopt;
    }
    ue3::TArray<ue3::Object*> const sockets = SocketsOf(*f, mesh);
    for (ue3::Object const* socket : sockets) {
        if (socket == nullptr || Read<std::uint64_t>(socket, f->socketName) != socketName) {
            continue;
        }
        Socket out;
        out.boneKey = Read<std::uint64_t>(socket, f->socketBone);
        out.bone = FindBone(mesh, out.boneKey);
        if (out.bone < 0) {
            return std::nullopt;
        }
        auto const r = Read<ue3::Rotator>(socket, f->socketRotation);
        auto const t = Read<ue3::Vector>(socket, f->socketLocation);
        out.relative = {FromRotator(r.pitch, r.yaw, r.roll), {t.x, t.y, t.z}, 1.0};
        return out;
    }
    return std::nullopt;
}

std::vector<NamedSocket> Sockets(ue3::Object const* mesh) {
    std::vector<NamedSocket> out;
    Fields const* const f = Ready();
    if (!kSupported || f == nullptr) {
        return out;
    }
    for (ue3::Object const* socket : SocketsOf(*f, mesh)) {
        if (socket == nullptr) {
            continue;
        }
        std::uint64_t const name = Read<std::uint64_t>(socket, f->socketName);
        if (auto found = FindSocket(mesh, name)) {
            out.push_back({name, *found});
        }
    }
    return out;
}

std::optional<Attachment> FindAttachment(ue3::Object const* mesh, ue3::Object const* component) {
    Fields const* const f = Ready();
    if (!kSupported || f == nullptr || mesh == nullptr || component == nullptr || f->attachments < 0 || f->attachmentSize <= 0
        || f->attachmentComponent < 0 || f->attachmentBone < 0 || f->attachmentLocation < 0 || f->attachmentRotation < 0
        || f->attachmentScale < 0) {
        return std::nullopt;
    }
    auto const attachments = Read<ue3::TArray<std::uint8_t>>(mesh, f->attachments);
    if (attachments.data == nullptr || attachments.count <= 0 || attachments.count > 4096) {
        return std::nullopt;
    }
    for (int i = 0; i < attachments.count; ++i) {
        std::uint8_t const* const entry = attachments.data + static_cast<std::size_t>(i) * static_cast<std::size_t>(f->attachmentSize);
        if (Read<ue3::Object const*>(entry, f->attachmentComponent) != component) {
            continue;
        }
        Attachment out;
        out.boneKey = Read<std::uint64_t>(entry, f->attachmentBone);
        out.bone = FindBone(mesh, out.boneKey);
        if (out.bone < 0) {
            return std::nullopt;
        }
        auto const r = Read<ue3::Rotator>(entry, f->attachmentRotation);
        auto const t = Read<ue3::Vector>(entry, f->attachmentLocation);
        auto const s = Read<ue3::Vector>(entry, f->attachmentScale);
        // UE3 treats a zero scale as 1.
        double scale = 1.0;
        if (s.x != 0.0f || s.y != 0.0f || s.z != 0.0f) {
            scale = s.x;
            out.uniformScale = std::abs(s.x - s.y) < 1e-4f && std::abs(s.x - s.z) < 1e-4f;
        }
        out.relative = {FromRotator(r.pitch, r.yaw, r.roll), {t.x, t.y, t.z}, out.uniformScale ? scale : 1.0};
        return out;
    }
    return std::nullopt;
}

std::optional<UnrealVector> BoneLocation(ue3::Object* mesh, int index, Space space) {
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

std::optional<UnrealVector> NativeBoneLocation(ue3::Object* mesh, std::uint64_t key, Space space) {
    ue3::Function* const matchRefBone = gMatchRefBone.Get();
    ue3::Function* const getBoneLocation = gGetBoneLocation.Get();
    if (matchRefBone == nullptr || getBoneLocation == nullptr || mesh == nullptr) {
        return std::nullopt;
    }
    ue3::Name const bone = ue3::Name::FromKey(key);
    // Called through ProcessEvent with blocks built from the functions themselves (never a generated
    // wrapper: CLAUDE.md). Both natives return through Result, which ProcessEvent points into the block.
    ue3::Params match(ue3::runtime::Get(), matchRefBone);
    if (!match.Set("BoneName", bone) || !match.Set("ReturnValue", std::int32_t{-1})) {  // stays -1 if the engine rejects the call
        return std::nullopt;
    }
    ue3::runtime::Call(mesh, matchRefBone, match);
    std::int32_t const index = match.Return<std::int32_t>().value_or(-1);
    // GetBoneLocation indexes SpaceBases unchecked: only for a bone whose pose exists.
    if (index < 0 || index >= static_cast<int>(SpaceBases(mesh).size())) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), getBoneLocation);
    if (!parms.Set("BoneName", bone) || !parms.Set("Space", static_cast<std::int32_t>(space))) {
        return std::nullopt;
    }
    ue3::runtime::Call(mesh, getBoneLocation, parms);
    std::optional<ue3::Vector> const at = parms.Return<ue3::Vector>();
    if (!at) {
        return std::nullopt;
    }
    return UnrealVector{at->x, at->y, at->z};
}

std::string GetBoneNamesDiagnostic(ue3::Object* mesh) {
    ue3::Function* const getBoneNames = gGetBoneNames.Get();
    if (getBoneNames == nullptr || mesh == nullptr) {
        return "GetBoneNames not found";
    }
    ue3::Params parms(ue3::runtime::Get(), getBoneNames);
    ue3::runtime::Call(mesh, getBoneNames, parms);
    ue3::TArray<ue3::Name> names = parms.Get<ue3::TArray<ue3::Name>>("BoneNames").value_or(ue3::TArray<ue3::Name>{});
    int const returned = names.data != nullptr ? names.count : 0;
    if (names.data != nullptr) {
        ue3::memory::Free(names.data);  // the block owns what the native returned (research/34 section 2.1)
    }
    std::string text = std::format("GetBoneNames through ProcessEvent returned {} names (RefSkeleton has {})", returned,
        RefBoneCount(mesh));
    if (kSupported) {
        ue3::Reflection& r = ue3::runtime::Get();
        auto const base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto const func = reinterpret_cast<std::uintptr_t>(r.NativeFunc(getBoneNames));
        text += std::format("; native exe+0x{:X} (execGetBoneNames 0x594950 expected), flags 0x{:X}, iNative {}", func - base,
            r.FunctionFlags(getBoneNames), r.NativeIndex(getBoneNames));
        // The function's bytecode (UStruct.Script) isn't in our layout yet: not read since stage 3 (research/34).
        if (std::uint32_t const rva = adapter::NativesTableRva(); rva != 0) {
            auto const* const natives = reinterpret_cast<std::uintptr_t const*>(base + rva);
            text += std::format("; GNatives[EX_NativeParm] exe+0x{:X} (0x11F210: binds an out parm only for a non-null result), "
                                "GNatives[EX_LocalOutVariable] exe+0x{:X}",
                natives[kExNativeParm] - base, natives[kExLocalOutVariable] - base);
        }
    }
    return text;
}

} // namespace mevr::engine::skeleton
