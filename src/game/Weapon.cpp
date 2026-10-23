#include "game/Weapon.hpp"

#include "core/Log.hpp"
#include "core/ik/ArmPose.hpp"
#include "engine/BonePose.hpp"
#include "engine/Objects.hpp"
#include "engine/Skeleton.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mevr::game::weapon {
namespace {

std::string gStatus = "off";
std::string gLoggedStatus;

void SetStatus(std::string status) {
    if (status != gLoggedStatus) {
        log::Info("weapon: {}", status);
        gLoggedStatus = status;
    }
    gStatus = std::move(status);
}

#if defined(SDK_TARGET_LE1)

// Each confirmed in the code that reads it (research/14, "Weapon and muzzle").
static_assert(offsetof(APawn, Weapon) == 0x4A8);
static_assert(offsetof(AWeapon, Mesh) == 0x394);
static_assert(offsetof(ABioWeapon, SkelMeshComp) == 0x3E8);
static_assert(offsetof(ABioWeaponRanged, m_MuzzleSocketName) == 0x490);
// bIsZoomed is bit 0x4 of the bitfield word at 0x548 (the SDK; GetPlayerAimError 0xC59920 tests it):
// the word follows this float.
static_assert(offsetof(ABioWeaponRanged, m_fMinVFXDecalIntensity) == 0x544);

constexpr char const* kGetMuzzleLoc = "Function SFXGame.BioWeaponRanged.GetMuzzleLoc";
constexpr char const* kGetSocketWorld = "Function Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation";

// Our muzzle and the native GetMuzzleLoc must agree this closely (Unreal units).
constexpr double kVerifyTolerance = 1.0;
// A weapon whose pose isn't there yet is retried for this many ticks before giving up.
constexpr int kVerifyAttempts = 240;
// The barrel must point within this of the direction from the weapon's origin (its grip) to the
// muzzle; without such a direction (a muzzle at the origin), within this of the mesh's own X.
constexpr double kBarrelMaxDegrees = 60.0;
constexpr double kAxesMaxDegrees = 45.0;
// Grip measurement: the hand holds still when it moved and turned less than this since the last
// tick; a measurement is settled after this many still ticks that agree with each other.
constexpr double kStillUnits = 0.5;
constexpr double kStillDegrees = 1.0;
constexpr int kSettleTicks = 5;
// A settled measurement this far from the kept grip replaces it.
constexpr double kRegripUnits = 1.0;
constexpr double kRegripDegrees = 2.0;
constexpr unsigned kLoggedRegrips = 3;
// Follow check: the muzzle counts as moving above this (units per tick), as still below the
// second; the verdict comes after this many moving ticks with the pose written.
constexpr double kMovingUnits = 1.0;
constexpr double kStillMuzzleUnits = 0.1;
constexpr int kFollowSamples = 40;

constexpr char const* kGetRangedWeaponType = "Function SFXGame.BioWeaponRanged.GetRangedWeaponType";
constexpr char const* kGetPlayerAimError = "Function SFXGame.BioWeaponRanged.GetPlayerAimError";

UFunction* gGetMuzzleLoc = nullptr;
UFunction* gGetSocketWorld = nullptr;
UFunction* gGetRangedWeaponType = nullptr;
UFunction* gGetPlayerAimError = nullptr;  // aimed fire for scoped shots (game/sights)

// A weapon-mesh socket for the off hand, by name (lowercase substrings).
constexpr char const* kOffHandSocketWords[] = {"lefthand", "left_hand", "lhand", "lhik", "offhand", "off_hand", "support",
    "foregrip", "fore_grip", "handguard", "grip_l", "l_grip"};

// A world transform as its origin and unit axes.
struct Frame {
    ik::Vec3 origin;
    ik::Vec3 x;
    ik::Vec3 y;
    ik::Vec3 z;
};

ik::Vec3 V(engine::UnrealVector const& v) {
    return {v.x, v.y, v.z};
}

ik::Vec3 V(FVector const& v) {
    return {v.X, v.Y, v.Z};
}

engine::UnrealVector U(ik::Vec3 const& v) {
    return {v.x, v.y, v.z};
}

std::string Text(ik::Vec3 const& v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

std::string Text3(ik::Vec3 const& v) {
    return std::format("({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z);
}

double Degrees(ik::Vec3 const& a, ik::Vec3 const& b) {
    double const lengths = ik::Length(a) * ik::Length(b);
    if (!(lengths > 0.0)) {
        return 180.0;
    }
    return std::acos(std::clamp(ik::Dot(a, b) / lengths, -1.0, 1.0)) * 180.0 / std::numbers::pi;
}

std::uint64_t Key(SFXName const& name) {
    std::uint64_t key = 0;
    std::memcpy(&key, &name, sizeof key);
    return key;
}

UObject const* CurrentWorld() {
    return GWorld != nullptr ? *GWorld : nullptr;
}

// `local` (a mesh's component space) through the mesh's LocalToWorld.
Frame WorldFrame(ik::Affine const& localToWorld, ik::Transform const& local) {
    auto const axis = [&](ik::Vec3 v) {
        return ik::Normalize(ik::TransformDirection(localToWorld, ik::Rotate(local.rotation, v)), v);
    };
    return {ik::TransformPoint(localToWorld, local.translation), axis({1.0, 0.0, 0.0}), axis({0.0, 1.0, 0.0}),
        axis({0.0, 0.0, 1.0})};
}

// The socket in its mesh's component space, from the mesh's current pose. Nullopt if the pose
// isn't computed or the skeleton changed.
std::optional<ik::Transform> SocketInMesh(USkeletalMeshComponent* mesh, engine::skeleton::Socket const& socket) {
    std::span<engine::skeleton::BoneAtom const> const pose = engine::skeleton::SpaceBases(mesh);
    if (socket.bone < 0 || static_cast<std::size_t>(socket.bone) >= pose.size() ||
        static_cast<int>(pose.size()) != engine::skeleton::RefBoneCount(mesh) ||
        engine::skeleton::RefBoneKey(mesh, socket.bone) != socket.boneKey) {
        return std::nullopt;
    }
    // GetBoneMatrix (0x583540): the bone atom (rotation, translation, scale); the socket's offset
    // comes first (GetSocketWorldLocationAndRotation, 0x584640).
    return ik::Compose(socket.relative, engine::skeleton::ToTransform(pose[socket.bone]));
}

std::optional<Frame> MuzzleFrame(USkeletalMeshComponent* mesh, engine::skeleton::Socket const& socket) {
    auto const local = SocketInMesh(mesh, socket);
    if (!local) {
        return std::nullopt;
    }
    return WorldFrame(engine::skeleton::LocalToWorld(mesh), *local);
}

enum class Axis { Socket, Mesh };

Muzzle ToMuzzle(Frame const& muzzle, ik::Affine const& meshToWorld, Axis axis) {
    ik::Vec3 barrel = muzzle.x;
    ik::Vec3 up = muzzle.z;
    if (axis == Axis::Mesh) {
        barrel = ik::Normalize(ik::TransformDirection(meshToWorld, {1.0, 0.0, 0.0}), {1.0, 0.0, 0.0});
        up = ik::Normalize(ik::TransformDirection(meshToWorld, {0.0, 0.0, 1.0}), {0.0, 0.0, 1.0});
    }
    up = ik::Normalize(up - barrel * ik::Dot(up, barrel), {0.0, 0.0, 1.0});
    return {U(muzzle.origin), U(barrel), U(up), U(ik::TransformPoint(meshToWorld, {0.0, 0.0, 0.0}))};
}

// This tick's pointers, read from the local player. All live objects.
struct Live {
    APawn* pawn = nullptr;
    USkeletalMeshComponent* body = nullptr;
    ABioWeaponRanged* weapon = nullptr;
    USkeletalMeshComponent* mesh = nullptr;
};

std::optional<Live> LivePointers(std::string& why) {
    APlayerController* const controller = engine::LocalPlayerController();
    APawn* const pawn = controller != nullptr ? controller->Pawn : nullptr;
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass()) || pawn->Mesh == nullptr || pawn->Mesh->SkeletalMesh == nullptr) {
        why = "no player body";
        return std::nullopt;
    }
    AWeapon* const weapon = pawn->Weapon;
    if (weapon == nullptr) {
        why = "no weapon";
        return std::nullopt;
    }
    if (!weapon->IsA(ABioWeaponRanged::StaticClass())) {
        why = std::format("{} isn't a ranged weapon", engine::FullName(weapon));
        return std::nullopt;
    }
    auto* const ranged = static_cast<ABioWeaponRanged*>(weapon);
    USkeletalMeshComponent* const mesh = ranged->SkelMeshComp;
    if (mesh == nullptr || mesh->SkeletalMesh == nullptr) {
        why = std::format("{} has no skeletal mesh", engine::FullName(weapon));
        return std::nullopt;
    }
    return Live{pawn, pawn->Mesh, ranged, mesh};
}

// The weapon looked at last: compared by address only, everything else re-read live.
struct Resolved {
    ABioWeaponRanged const* weapon = nullptr;
    USkeletalMeshComponent const* mesh = nullptr;
    UObject const* asset = nullptr;
    UObject const* world = nullptr;
    std::uint64_t socketName = 0;
    engine::skeleton::Socket socket;
    Axis axis = Axis::Socket;
    bool verified = false;
    bool failed = false;
    int attempts = 0;
    std::string name;
    int rangedType = -1;  // RangedType
    std::optional<engine::skeleton::Socket> offHand;  // a socket for the off hand (support grip)
};
std::optional<Resolved> gResolved;

// The body's right hand, per skeleton asset.
struct Body {
    UObject const* asset = nullptr;
    int boneCount = 0;
    int hand = -1;
    std::vector<int> parents;
    std::vector<std::string> names;
};
std::optional<Body> gBody;

// Grips measured before, per weapon mesh asset, body asset and holding bone, in this world: a gun drawn
// again is aimed at once instead of after the hand has held still for a moment (2026-10-22: a left grip
// squeezed in that gap, right after a draw, found no gun to hold). A still measurement that differs
// replaces it as before. Cleared when the world changes (assets can move).
struct CachedGrip {
    UObject const* weaponAsset = nullptr;
    UObject const* bodyAsset = nullptr;
    int bone = -1;
    Grip grip;
};
std::array<CachedGrip, 8> gGripCache{};
std::size_t gGripCacheNext = 0;
UObject const* gGripCacheWorld = nullptr;

bool gHeld = false;
int gHeldBone = -1;
std::optional<Grip> gGrip;
std::optional<Grip> gLastMeasure;
std::optional<Frame> gLastHand;
int gStillTicks = 0;
unsigned gRegrips = 0;

struct Follow {
    std::optional<ik::Vec3> lastExpected;
    std::uint64_t lastWrites = 0;
    int moving = 0;
    double sumMoved = 0.0;
    double sumNow = 0.0;
    double maxNow = 0.0;
    double sumPrev = 0.0;
    int still = 0;
    double sumStill = 0.0;
    bool logged = false;
    // Heartbeat window.
    int windowMoving = 0;
    double windowSumNow = 0.0;
    double windowMaxNow = 0.0;
    double windowSumPrev = 0.0;
};
Follow gFollow;

std::string BoneName(Body const& body, int bone) {
    return bone >= 0 && static_cast<std::size_t>(bone) < body.names.size() ? std::format("{}({})", body.names[bone], bone)
                                                                              : std::format("#{}", bone);
}

Body const* BodySkeleton(USkeletalMeshComponent* body) {
    UObject const* const asset = body->SkeletalMesh;
    int const count = engine::skeleton::RefBoneCount(body);
    if (gBody && gBody->asset == asset && gBody->boneCount == count) {
        return &*gBody;
    }
    gBody.reset();
    auto bones = engine::skeleton::ReadRefSkeleton(body);
    if (!bones) {
        return nullptr;
    }
    auto const chain = ik::FindArmChain(bones->names, bones->parents, 1);
    gBody = Body{asset, count, chain ? chain->hand : -1, std::move(bones->parents), std::move(bones->names)};
    return &*gBody;
}

// `bone` is `ancestor` or hangs below it.
bool Under(Body const& body, int bone, int ancestor) {
    int const count = static_cast<int>(body.parents.size());
    for (int b = bone, guard = 0; b >= 0 && b < count && guard < 512; b = body.parents[b], ++guard) {
        if (b == ancestor) {
            return true;
        }
    }
    return false;
}

void ResetGripMeasurement() {
    gLastMeasure.reset();
    gLastHand.reset();
    gStillTicks = 0;
}

void RememberGrip(UObject const* world, UObject const* weaponAsset, UObject const* bodyAsset, int bone, Grip const& grip) {
    if (world != gGripCacheWorld) {
        gGripCache = {};
        gGripCacheWorld = world;
    }
    for (CachedGrip& cached : gGripCache) {
        if (cached.weaponAsset == weaponAsset && cached.bodyAsset == bodyAsset && cached.bone == bone) {
            cached.grip = grip;
            return;
        }
    }
    gGripCache[gGripCacheNext] = {weaponAsset, bodyAsset, bone, grip};
    gGripCacheNext = (gGripCacheNext + 1) % gGripCache.size();
}

std::optional<Grip> RecallGrip(UObject const* world, UObject const* weaponAsset, UObject const* bodyAsset, int bone) {
    if (world != gGripCacheWorld || weaponAsset == nullptr || bodyAsset == nullptr) {
        return std::nullopt;
    }
    for (CachedGrip const& cached : gGripCache) {
        if (cached.weaponAsset == weaponAsset && cached.bodyAsset == bodyAsset && cached.bone == bone) {
            return cached.grip;
        }
    }
    return std::nullopt;
}

void Forget() {
    gResolved.reset();
    gHeld = false;
    gHeldBone = -1;
    gGrip.reset();
    gRegrips = 0;
    ResetGripMeasurement();
    gFollow = {};
}

// Diagnostic, once per weapon: whether a native with struct out parameters (OutLocation,
// OutRotation) returns them through ProcessEvent, and the engine's socket rotation against ours.
void LogSocketNative(USkeletalMeshComponent* mesh, SFXName const& socket, Frame const& ours) {
    if (gGetSocketWorld == nullptr) {
        return;
    }
    USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms parms;
    parms.InSocketName = socket;
    parms.Space = 0;  // world
    mesh->ProcessEvent(gGetSocketWorld, &parms, nullptr);
    ik::Vec3 const location = V(parms.OutLocation);
    engine::Basis const axes = engine::ToBasis(engine::UnrealRotation{parms.OutRotation.Pitch, parms.OutRotation.Yaw, parms.OutRotation.Roll});
    bool const unbound = location.x == 0.0 && location.y == 0.0 && location.z == 0.0;
    log::Info("weapon: GetSocketWorldLocationAndRotation through ProcessEvent returned {}, location {} ({:.2f} units from ours), "
              "rotation X {:.1f} degrees and Z {:.1f} degrees from ours{}",
        parms.ReturnValue != 0, Text(location), ik::Length(location - ours.origin), Degrees(V(axes.forward), ours.x),
        Degrees(V(axes.up), ours.z), unbound ? " (out parameters not bound through ProcessEvent)" : "");
}

char const* TypeName(int type) {
    switch (type) {
    case 0: return "pistol";
    case 1: return "shotgun";
    case 2: return "assault rifle";
    case 3: return "sniper rifle";
    default: return "unknown type";
    }
}

// Once per weapon, for the support grip (game/support_grip): its ranged type (the native
// GetRangedWeaponType, a plain return value through ProcessEvent), every socket of its mesh, and
// which one, if any, is named for the off hand.
void DescribeForSupport(Live const& live, Resolved& r) {
    if (gGetRangedWeaponType != nullptr) {
        ABioWeaponRanged_execGetRangedWeaponType_Parms parms;
        parms.ReturnValue = 0xFF;
        live.weapon->ProcessEvent(gGetRangedWeaponType, &parms, nullptr);  // directly: never the SDK's wrapper
        r.rangedType = parms.ReturnValue <= 3 ? parms.ReturnValue : -1;
    }
    std::string list;
    std::string chosen;
    for (auto const& named : engine::skeleton::Sockets(live.mesh)) {
        std::string const name = engine::ToString(engine::skeleton::NameOf(named.name));
        list += std::format("{}{} on {} at {}", list.empty() ? "" : "; ", name,
            engine::ToString(engine::skeleton::NameOf(named.socket.boneKey)), Text(named.socket.relative.translation));
        if (named.name == r.socketName || r.offHand) {
            continue;
        }
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (char const* word : kOffHandSocketWords) {
            if (lower.find(word) != std::string::npos) {
                r.offHand = named.socket;
                chosen = name;
                break;
            }
        }
    }
    log::Info("weapon: {} ({}); mesh sockets: {}; off-hand socket: {}", r.name, TypeName(r.rangedType),
        list.empty() ? std::string("none") : list, chosen.empty() ? std::string("none by name") : chosen);
}

// Checks the weapon's muzzle against the engine's and picks the barrel axis (logged). Leaves
// `r` pending if the weapon's pose isn't there yet.
void Verify(Live const& live, Resolved& r) {
    ++r.attempts;
    std::string const socketText = engine::ToString(live.weapon->m_MuzzleSocketName);
    auto const socket = engine::skeleton::FindSocket(live.mesh, r.socketName);
    if (!socket) {
        r.failed = true;
        log::Warn("weapon: {}: muzzle socket '{}' not found on {}: shots and the hand keep the controller's aim", r.name,
            socketText, engine::FullName(live.mesh->SkeletalMesh));
        return;
    }
    r.socket = *socket;
    auto const frame = MuzzleFrame(live.mesh, *socket);
    if (!frame) {
        if (r.attempts >= kVerifyAttempts) {
            r.failed = true;
            log::Warn("weapon: {}: its pose never appeared; shots and the hand keep the controller's aim", r.name);
        }
        return;
    }
    if (gGetMuzzleLoc == nullptr) {
        r.failed = true;
        log::Warn("weapon: BioWeaponRanged.GetMuzzleLoc not found: can't verify the muzzle; no weapon aim");
        return;
    }
    // The engine's own answer (a plain return value, which ProcessEvent passes back). Called
    // directly: the SDK's wrappers corrupt FunctionFlags (CLAUDE.md).
    ABioWeaponRanged_execGetMuzzleLoc_Parms parms;
    float const unset = std::numeric_limits<float>::max();
    parms.ReturnValue = {unset, unset, unset};
    live.weapon->ProcessEvent(gGetMuzzleLoc, &parms, nullptr);
    ik::Vec3 const native = V(parms.ReturnValue);
    double const error = ik::Length(native - frame->origin);
    bool const match = parms.ReturnValue.X != unset && error <= kVerifyTolerance;
    ik::Vec3 const offset = socket->relative.translation;
    log::Info("weapon: {} (mesh {}): muzzle socket '{}' on bone {}({}), offset {}: muzzle {} vs GetMuzzleLoc {}: {} ({:.2f} units)",
        r.name, engine::FullName(live.mesh->SkeletalMesh), socketText,
        engine::ToString(engine::skeleton::NameOf(socket->boneKey)), socket->bone, Text(offset), Text(frame->origin), Text(native),
        match ? "match" : "MISMATCH", error);
    if (!match) {
        r.failed = true;
        log::Warn("weapon: {}: muzzle not verified; shots and the hand keep the controller's aim", r.name);
        return;
    }
    LogSocketNative(live.mesh, live.weapon->m_MuzzleSocketName, *frame);

    // The barrel: the socket's X, unless that points away from the muzzle's side of the weapon.
    ik::Affine const meshToWorld = engine::skeleton::LocalToWorld(live.mesh);
    ik::Vec3 const origin = ik::TransformPoint(meshToWorld, {0.0, 0.0, 0.0});
    ik::Vec3 const meshX = ik::Normalize(ik::TransformDirection(meshToWorld, {1.0, 0.0, 0.0}), {1.0, 0.0, 0.0});
    ik::Vec3 const toMuzzle = frame->origin - origin;
    double const socketAngle = Degrees(frame->x, toMuzzle);
    double const meshAngle = Degrees(meshX, toMuzzle);
    double const apart = Degrees(frame->x, meshX);
    bool const reference = ik::Length(toMuzzle) >= 2.0;
    char const* choice = nullptr;
    if (reference ? socketAngle <= kBarrelMaxDegrees : apart <= kAxesMaxDegrees) {
        r.axis = Axis::Socket;
        choice = "the muzzle socket's X axis";
    } else if (reference && meshAngle <= kBarrelMaxDegrees) {
        r.axis = Axis::Mesh;
        choice = "the weapon mesh's X axis (the socket's points elsewhere)";
    }
    log::Info("weapon: barrel: {} (socket X {:.0f} and mesh X {:.0f} degrees from the origin-to-muzzle direction, {:.0f} degrees "
              "apart; the muzzle is {:.1f} units from the weapon's origin)",
        choice != nullptr ? choice : "NONE fits", socketAngle, meshAngle, apart, ik::Length(toMuzzle));
    if (choice == nullptr) {
        r.failed = true;
        log::Warn("weapon: {}: no barrel axis; shots and the hand keep the controller's aim", r.name);
        return;
    }
    if (static_cast<UObject*>(live.weapon->Mesh) != static_cast<UObject*>(live.mesh)) {
        log::Info("weapon: {}: its attached mesh ({}) isn't its muzzle mesh; held only if the muzzle mesh hangs on the hand",
            r.name, engine::FullName(live.weapon->Mesh));
    }
    r.verified = true;
    DescribeForSupport(live, r);
}

// The grip: muzzle, barrel and up in the right hand bone's axes, measured while the hand holds
// still (so a weapon updated before or after the hand reads the same) and kept once it settles.
void MeasureGrip(Live const& live, Body const& body, Muzzle const& muzzle) {
    std::span<engine::skeleton::BoneAtom const> const pose = engine::skeleton::SpaceBases(live.body);
    if (body.hand < 0 || static_cast<std::size_t>(body.hand) >= pose.size()) {
        return;
    }
    ik::Transform const hand = engine::skeleton::ToTransform(pose[body.hand]);
    ik::Affine const bodyToWorld = engine::skeleton::LocalToWorld(live.body);
    auto const worldToBody = ik::InverseAffine(bodyToWorld);
    if (!worldToBody) {
        return;
    }
    Frame const handWorld = WorldFrame(bodyToWorld, hand);
    bool const still = gLastHand && ik::Length(handWorld.origin - gLastHand->origin) < kStillUnits &&
        Degrees(handWorld.x, gLastHand->x) < kStillDegrees && Degrees(handWorld.z, gLastHand->z) < kStillDegrees;
    gLastHand = handWorld;

    ik::Quat const toHand = ik::Conjugate(hand.rotation);
    double const inverseScale = hand.scale > 1e-6 ? 1.0 / hand.scale : 1.0;
    ik::Vec3 const muzzleInBody = ik::TransformPoint(*worldToBody, V(muzzle.location));
    Grip grip;
    grip.barrel = ik::Normalize(ik::Rotate(toHand, ik::TransformDirection(*worldToBody, V(muzzle.barrel))), {1.0, 0.0, 0.0});
    ik::Vec3 const up = ik::Normalize(ik::Rotate(toHand, ik::TransformDirection(*worldToBody, V(muzzle.up))), {0.0, 0.0, 1.0});
    grip.up = ik::Normalize(up - grip.barrel * ik::Dot(up, grip.barrel), {0.0, 0.0, 1.0});
    grip.muzzle = ik::Rotate(toHand, muzzleInBody - hand.translation) * inverseScale;

    auto const sameGrip = [](Grip const& a, Grip const& b, double units, double degrees) {
        return ik::Length(a.muzzle - b.muzzle) < units && Degrees(a.barrel, b.barrel) < degrees && Degrees(a.up, b.up) < degrees;
    };
    bool const agrees = gLastMeasure && sameGrip(grip, *gLastMeasure, kStillUnits, kStillDegrees);
    gLastMeasure = grip;
    gStillTicks = still && agrees ? gStillTicks + 1 : 0;
    if (gStillTicks < kSettleTicks) {
        return;
    }
    std::string const text = std::format("barrel {}, up {}, muzzle {} ({:.1f} units from the {} bone)", Text3(grip.barrel),
        Text3(grip.up), Text(grip.muzzle), ik::Length(grip.muzzle), BoneName(body, body.hand));
    if (!gGrip) {
        gGrip = grip;
        log::Info("weapon: grip measured in the right hand's axes: {}", text);
    } else if (!sameGrip(grip, *gGrip, kRegripUnits, kRegripDegrees)) {
        if (++gRegrips <= kLoggedRegrips) {
            log::Info("weapon: grip moved by {:.1f} units, {:.1f} degrees; now {}", ik::Length(grip.muzzle - gGrip->muzzle),
                Degrees(grip.barrel, gGrip->barrel), text);
        }
        gGrip = grip;
    } else {
        return;
    }
    if (gResolved) {
        RememberGrip(gResolved->world, gResolved->asset, body.asset, gHeldBone, *gGrip);
    }
}

// Does the weapon follow the written hand in the same frame? The muzzle where the engine put it
// (the weapon mesh's LocalToWorld) against where the body's current pose (our write, if any) and
// the attachment put it, this tick and last tick. Only ticks whose pose our writer wrote count.
void CheckFollow(Live const& live, Body const& body, engine::skeleton::Attachment const& attachment, Frame const& actual) {
    std::span<engine::skeleton::BoneAtom const> const pose = engine::skeleton::SpaceBases(live.body);
    auto const socket = SocketInMesh(live.mesh, gResolved->socket);
    if (static_cast<std::size_t>(attachment.bone) >= pose.size() || !socket) {
        return;
    }
    // The attachment's offset first, then its bone (scale dropped) [verify: the log's still error].
    ik::Transform bone = engine::skeleton::ToTransform(pose[attachment.bone]);
    bone.scale = 1.0;
    ik::Vec3 const expected = ik::TransformPoint(engine::skeleton::LocalToWorld(live.body),
        ik::Apply(ik::Compose(attachment.relative, bone), socket->translation));
    std::uint64_t const writes = engine::bone_pose::TotalWrites();
    bool const written = writes != gFollow.lastWrites;
    gFollow.lastWrites = writes;
    std::optional<ik::Vec3> const last = gFollow.lastExpected;
    gFollow.lastExpected = expected;
    if (!written || !last) {
        return;
    }
    double const now = ik::Length(actual.origin - expected);
    double const previous = ik::Length(actual.origin - *last);
    double const moved = ik::Length(expected - *last);
    if (moved < kStillMuzzleUnits) {
        ++gFollow.still;
        gFollow.sumStill += now;
    } else if (moved >= kMovingUnits) {
        ++gFollow.moving;
        gFollow.sumMoved += moved;
        gFollow.sumNow += now;
        gFollow.maxNow = std::max(gFollow.maxNow, now);
        gFollow.sumPrev += previous;
        ++gFollow.windowMoving;
        gFollow.windowSumNow += now;
        gFollow.windowMaxNow = std::max(gFollow.windowMaxNow, now);
        gFollow.windowSumPrev += previous;
    }
    if (gFollow.logged || gFollow.moving < kFollowSamples) {
        return;
    }
    gFollow.logged = true;
    double const meanNow = gFollow.sumNow / gFollow.moving;
    double const meanPrev = gFollow.sumPrev / gFollow.moving;
    double const meanStill = gFollow.still > 0 ? gFollow.sumStill / gFollow.still : -1.0;
    double const allowed = std::max(0.5, (meanStill >= 0.0 ? meanStill : 0.0) + 0.5);
    char const* const verdict = meanNow <= allowed ? "follows the written hand in the same frame"
        : meanPrev < 0.5 * meanNow                 ? "LAGS one tick behind the written hand"
                                                   : "does NOT follow the written hand";
    log::Info("weapon: follow check, attached to {} ({} moving ticks, {:.1f} units per tick): muzzle vs the written hand + "
              "attachment {:.2f} avg / {:.2f} max units, vs last tick's hand {:.2f}; holding still {:.2f} ({} ticks): {}",
        BoneName(body, attachment.bone), gFollow.moving, gFollow.sumMoved / gFollow.moving, meanNow, gFollow.maxNow, meanPrev,
        meanStill, gFollow.still, verdict);
}

#endif  // SDK_TARGET_LE1

} // namespace

bool TryStartup() {
#if defined(SDK_TARGET_LE1)
    gGetMuzzleLoc = engine::FindFunction(L"GetMuzzleLoc", kGetMuzzleLoc);
    gGetSocketWorld = engine::FindFunction(L"GetSocketWorldLocationAndRotation", kGetSocketWorld);
    gGetRangedWeaponType = engine::FindFunction(L"GetRangedWeaponType", kGetRangedWeaponType);
    gGetPlayerAimError = engine::FindFunction(L"GetPlayerAimError", kGetPlayerAimError);
    // Research: where the natives behind the fire path live, to tie the code reading (TickFire's
    // body at 0xC621E0, GetMuzzleLoc's at 0xC59870) to the script functions.
    auto const base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto const where = [base](UFunction* function) {
        return function != nullptr ? std::format("exe+0x{:X}", reinterpret_cast<std::uintptr_t>(function->Func) - base)
                                   : std::string("not found");
    };
    log::Info("weapon: natives: BioWeaponRanged.GetMuzzleLoc {}, BioWeaponRanged.TickFire {}, BioPawn.GetWeaponStartTraceLocation {}, "
              "SkeletalMeshComponent.GetSocketWorldLocationAndRotation {}",
        where(gGetMuzzleLoc), where(engine::FindFunction(L"TickFire", "Function SFXGame.BioWeaponRanged.TickFire")),
        where(engine::FindFunction(L"GetWeaponStartTraceLocation", "Function SFXGame.BioPawn.GetWeaponStartTraceLocation")),
        where(gGetSocketWorld));
    return gGetMuzzleLoc != nullptr;
#else
    return false;
#endif
}

void Tick(bool wanted) {
#if defined(SDK_TARGET_LE1)
    if (!wanted) {
        if (gResolved) {
            Forget();
        }
        SetStatus("off");
        return;
    }
    std::string why;
    auto const live = LivePointers(why);
    if (!live) {
        if (gResolved) {
            Forget();
        }
        SetStatus(why);
        return;
    }
    std::uint64_t const socketName = Key(live->weapon->m_MuzzleSocketName);
    UObject const* const world = CurrentWorld();
    if (!gResolved || gResolved->weapon != live->weapon || gResolved->mesh != live->mesh ||
        gResolved->asset != live->mesh->SkeletalMesh || gResolved->socketName != socketName || gResolved->world != world) {
        Forget();
        Resolved r;
        r.weapon = live->weapon;
        r.mesh = live->mesh;
        r.asset = live->mesh->SkeletalMesh;
        r.world = world;
        r.socketName = socketName;
        r.name = engine::FullName(live->weapon);
        gResolved = std::move(r);
    }
    if (!gResolved->verified) {
        if (!gResolved->failed) {
            Verify(*live, *gResolved);
        }
        if (!gResolved->verified) {
            SetStatus(std::format("{}: {}", gResolved->name, gResolved->failed ? "not usable (see above)" : "waiting for its pose"));
            return;
        }
    }
    Body const* const body = BodySkeleton(live->body);
    auto const attachment = engine::skeleton::FindAttachment(live->body, live->mesh);
    bool const held = body != nullptr && body->hand >= 0 && attachment && Under(*body, attachment->bone, body->hand);
    int const heldBone = held ? attachment->bone : -1;
    if (held != gHeld || heldBone != gHeldBone) {
        if (heldBone != gHeldBone && gGrip) {
            gGrip.reset();  // another bone: another grip
        }
        ResetGripMeasurement();
        gFollow.lastExpected.reset();
        if (held) {
            log::Info("weapon: {} in the right hand: its mesh hangs on {} under {}{}", gResolved->name, BoneName(*body, heldBone),
                BoneName(*body, body->hand), attachment->uniformScale ? "" : " (non-uniform attachment scale)");
            if (!gGrip) {
                if (auto const cached = RecallGrip(gResolved->world, gResolved->asset, body->asset, heldBone)) {
                    gGrip = cached;
                    log::Info("weapon: {}: grip as measured the last time it was held (aimed at once; a still hand re-checks it)",
                        gResolved->name);
                }
            }
        } else {
            std::string const where = !attachment ? "its mesh isn't attached to the body"
                : body == nullptr                 ? "body skeleton unreadable"
                : body->hand < 0                  ? "no right hand in the body's skeleton"
                                                  : std::format("attached to {}", BoneName(*body, attachment->bone));
            log::Info("weapon: {} not in the right hand ({})", gResolved->name, where);
        }
    }
    gHeld = held;
    gHeldBone = heldBone;
    if (!held) {
        SetStatus(std::format("{}: not in the right hand", gResolved->name));
        return;
    }
    auto const frame = MuzzleFrame(live->mesh, gResolved->socket);
    if (!frame) {
        SetStatus(std::format("{}: no pose", gResolved->name));
        return;
    }
    Muzzle const muzzle = ToMuzzle(*frame, engine::skeleton::LocalToWorld(live->mesh), gResolved->axis);
    MeasureGrip(*live, *body, muzzle);
    CheckFollow(*live, *body, *attachment, *frame);
    SetStatus(std::format("{}: in the right hand, grip {}", gResolved->name, gGrip ? "measured" : "being measured (hold still)"));
#else
    if (wanted) {
        SetStatus("LE1 only");
    }
#endif
}

std::optional<Muzzle> Current() {
#if defined(SDK_TARGET_LE1)
    if (!gResolved || !gResolved->verified || !gHeld) {
        return std::nullopt;
    }
    std::string why;
    auto const live = LivePointers(why);
    if (!live || live->weapon != gResolved->weapon || live->mesh != gResolved->mesh || live->mesh->SkeletalMesh != gResolved->asset) {
        return std::nullopt;
    }
    // Still in the hand: it can be put away between two ticks.
    auto const attachment = engine::skeleton::FindAttachment(live->body, live->mesh);
    if (!attachment || attachment->bone != gHeldBone) {
        return std::nullopt;
    }
    auto const frame = MuzzleFrame(live->mesh, gResolved->socket);
    if (!frame) {
        return std::nullopt;
    }
    return ToMuzzle(*frame, engine::skeleton::LocalToWorld(live->mesh), gResolved->axis);
#else
    return std::nullopt;
#endif
}

std::optional<Grip> RightHandGrip() {
#if defined(SDK_TARGET_LE1)
    if (!gResolved || !gResolved->verified || !gHeld) {
        return std::nullopt;
    }
    return gGrip;
#else
    return std::nullopt;
#endif
}

std::uintptr_t Key() {
#if defined(SDK_TARGET_LE1)
    return gResolved && gResolved->verified && gHeld ? reinterpret_cast<std::uintptr_t>(gResolved->weapon) : 0;
#else
    return 0;
#endif
}

RangedType Type() {
#if defined(SDK_TARGET_LE1)
    if (gResolved && gResolved->verified && gResolved->rangedType >= 0 && gResolved->rangedType <= 3) {
        return static_cast<RangedType>(gResolved->rangedType);
    }
#endif
    return RangedType::Unknown;
}

std::optional<bool> Zoomed() {
#if defined(SDK_TARGET_LE1)
    std::string why;
    auto const live = LivePointers(why);
    if (!live) {
        return std::nullopt;
    }
    return live->weapon->bIsZoomed != 0;
#else
    return std::nullopt;
#endif
}

#if defined(SDK_TARGET_LE1)
namespace {
// GetPlayerAimError (0xC59920): lerp(Stats' aim-error range picked by bIsZoomed, accuracy). A native with
// a plain float in and out, so ProcessEvent passes both (research/14). Never the SDK's wrapper.
std::optional<float> CallAimError(ABioWeaponRanged* weapon, float accuracy) {
    if (gGetPlayerAimError == nullptr) {
        return std::nullopt;
    }
    ABioWeaponRanged_execGetPlayerAimError_Parms parms;
    parms.Accuracy = accuracy;
    weapon->ProcessEvent(gGetPlayerAimError, &parms, nullptr);
    return std::isfinite(parms.ReturnValue) ? std::optional<float>(parms.ReturnValue) : std::nullopt;
}

ABioWeaponRanged* HeldRanged(UObject* weapon) {
    std::string why;
    auto const live = LivePointers(why);
    if (!live || weapon == nullptr || weapon != live->weapon) {
        return nullptr;
    }
    return live->weapon;
}
} // namespace
#endif

std::optional<float> AimError(UObject* weapon, float accuracy) {
#if defined(SDK_TARGET_LE1)
    if (ABioWeaponRanged* const held = HeldRanged(weapon)) {
        return CallAimError(held, accuracy);
    }
#else
    (void)weapon;
    (void)accuracy;
#endif
    return std::nullopt;
}

std::optional<float> ZoomedAimError(UObject* weapon, float accuracy) {
#if defined(SDK_TARGET_LE1)
    ABioWeaponRanged* const held = HeldRanged(weapon);
    if (held == nullptr || held->bIsZoomed != 0) {
        return std::nullopt;
    }
    held->bIsZoomed = 1;  // for this one call: put back before anything else runs on this thread
    std::optional<float> const zoomed = CallAimError(held, accuracy);
    held->bIsZoomed = 0;
    return zoomed;
#else
    (void)weapon;
    (void)accuracy;
    return std::nullopt;
#endif
}

std::optional<ik::Vec3> OffHandSocketInHand() {
#if defined(SDK_TARGET_LE1)
    if (!gResolved || !gResolved->verified || !gHeld || !gResolved->offHand || !gBody || gBody->hand < 0) {
        return std::nullopt;
    }
    std::string why;
    auto const live = LivePointers(why);
    if (!live || live->weapon != gResolved->weapon || live->mesh != gResolved->mesh) {
        return std::nullopt;
    }
    auto const local = SocketInMesh(live->mesh, *gResolved->offHand);
    std::span<engine::skeleton::BoneAtom const> const pose = engine::skeleton::SpaceBases(live->body);
    if (!local || static_cast<std::size_t>(gBody->hand) >= pose.size()) {
        return std::nullopt;
    }
    ik::Vec3 const world = ik::TransformPoint(engine::skeleton::LocalToWorld(live->mesh), local->translation);
    auto const worldToBody = ik::InverseAffine(engine::skeleton::LocalToWorld(live->body));
    if (!worldToBody) {
        return std::nullopt;
    }
    ik::Transform const hand = engine::skeleton::ToTransform(pose[gBody->hand]);
    double const inverseScale = hand.scale > 1e-6 ? 1.0 / hand.scale : 1.0;
    return ik::Rotate(ik::Conjugate(hand.rotation), ik::TransformPoint(*worldToBody, world) - hand.translation) * inverseScale;
#else
    return std::nullopt;
#endif
}

std::string const& Status() {
    return gStatus;
}

void LogStats() {
#if defined(SDK_TARGET_LE1)
    if (gStatus == "off") {
        return;
    }
    std::string follow;
    if (gFollow.windowMoving > 0) {
        follow = std::format("; follow error {:.2f} avg / {:.2f} max units over {} moving ticks (vs last tick's hand {:.2f})",
            gFollow.windowSumNow / gFollow.windowMoving, gFollow.windowMaxNow, gFollow.windowMoving,
            gFollow.windowSumPrev / gFollow.windowMoving);
    }
    log::Info("    weapon: {}{}", gStatus, follow);
    gFollow.windowMoving = 0;
    gFollow.windowSumNow = gFollow.windowMaxNow = gFollow.windowSumPrev = 0.0;
#endif
}

void Shutdown() {
#if defined(SDK_TARGET_LE1)
    Forget();
    gBody.reset();
    gGetMuzzleLoc = nullptr;
    gGetSocketWorld = nullptr;
    gGetRangedWeaponType = nullptr;
    gGetPlayerAimError = nullptr;
#endif
    gStatus = "off";
    gLoggedStatus.clear();
}

} // namespace mevr::game::weapon
