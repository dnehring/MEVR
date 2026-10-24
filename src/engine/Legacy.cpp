// SPDX-License-Identifier: MIT
#include "engine/Legacy.hpp"

#if !defined(MEVR_GAME_LE1)

#include "engine/GameAdapterSdk.hpp"
#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <cstdint>
#include <new>
#include <string>
#include <string_view>

namespace mevr::engine::legacy {
namespace {

static_assert(sizeof(ue3::Vector) == sizeof(FVector) && sizeof(ue3::Rotator) == sizeof(FRotator));

UObject* Sdk(ue3::Object const* object) {
    return reinterpret_cast<UObject*>(const_cast<ue3::Object*>(object));
}

std::wstring Widen(std::string const& utf8) {
    int const size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    }
    return wide;
}

std::string Narrow(wchar_t const* chars) {
    int const size = WideCharToMultiByte(CP_UTF8, 0, chars, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string narrow(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, chars, -1, narrow.data(), size, nullptr, nullptr);
    return narrow;
}

// EObjectFlags. The SDK's operators truncate them to int, so test the 64-bit value directly.
constexpr std::uint64_t kTemplateFlags = 0x200 | 0x400;  // ClassDefaultObject | ArchetypeObject
constexpr std::uint64_t kPendingKill = 0x2000000000000000ull;

} // namespace

ue3::Object* LocalPlayerController() {
    return reinterpret_cast<ue3::Object*>(engine::LocalPlayerController());
}

ue3::Function* FindFunction(char const* path) {
    std::string_view const full(path);
    std::size_t const dot = full.rfind('.');
    std::string_view const name = dot == std::string_view::npos ? full : full.substr(dot + 1);
    std::wstring const wide(name.begin(), name.end());  // ASCII names
    std::string const fullName = std::string("Function ") + path;
    return reinterpret_cast<ue3::Function*>(engine::FindFunction(wide.c_str(), fullName.c_str()));
}

std::string FullName(ue3::Object const* object) {
    return engine::FullName(Sdk(object));
}

bool IsPlayerController(ue3::Object const* object) {
    return object != nullptr && Sdk(object)->IsA(APlayerController::StaticClass());
}

std::optional<std::uint8_t> GameMode(ue3::Object const* controller) {
    UObject* const pc = Sdk(controller);
    if (pc == nullptr || !pc->IsA(ABioPlayerController::StaticClass())) {
        return std::nullopt;
    }
    USFXGameModeManager const* const modes = engine::adapter::GameModes(static_cast<ABioPlayerController*>(pc));
    if (modes == nullptr) {
        return std::nullopt;
    }
    return modes->CurrentMode;
}

std::optional<ue3::Vector> PawnLocation(ue3::Object const* controller) {
    auto* const pc = static_cast<AController*>(Sdk(controller));
    if (pc == nullptr || pc->Pawn == nullptr) {
        return std::nullopt;
    }
    FVector const& at = engine::adapter::Location(pc->Pawn);
    return ue3::Vector{at.X, at.Y, at.Z};
}

ue3::Object* CameraOwner(ue3::Object const* camera) {
    return camera != nullptr ? reinterpret_cast<ue3::Object*>(static_cast<ACamera*>(Sdk(camera))->PCOwner) : nullptr;
}

float* CameraFov(ue3::Object* camera) {
    return camera != nullptr ? &static_cast<ACamera*>(Sdk(camera))->CameraCache.POV.FOV : nullptr;
}

ue3::Vector* ViewPointLocation(void* parms) {
    return reinterpret_cast<ue3::Vector*>(&static_cast<AController_eventGetPlayerViewPoint_Parms*>(parms)->out_Location);
}

ue3::Rotator* ViewPointRotation(void* parms) {
    return reinterpret_cast<ue3::Rotator*>(
        &engine::adapter::ViewPointRotation(*static_cast<AController_eventGetPlayerViewPoint_Parms*>(parms)));
}

float* FovAngleReturn(void* parms) {
    return &static_cast<APlayerController_eventGetFOVAngle_Parms*>(parms)->ReturnValue;
}

ue3::Rotator* AdjustedAimReturn(void* parms) {
    return reinterpret_cast<ue3::Rotator*>(&static_cast<ABioPlayerController_execGetAdjustedAimFor_Parms*>(parms)->ReturnValue);
}

std::string ConsoleCommand(ue3::Object* controller, ue3::Function* function, std::string const& command) {
    APlayerController_execConsoleCommand_Parms parms;  // zeroed
    new (&parms.Command) FString(Widen(command).c_str());  // the game's allocator
    parms.bWriteToLog = 0;
    // The native directly, never the SDK's wrapper.
    Sdk(controller)->ProcessEvent(reinterpret_cast<UFunction*>(function), &parms, nullptr);
    std::string output = Narrow(parms.ReturnValue.Chars());
    parms.Command.~FString();  // union members: the parms' destructor leaves them alone
    parms.ReturnValue.~FString();
    return output;
}

int RaiseReflectionCaptures(ue3::Function* setFrameRate, float rate, int begin, int limit, CaptureCounts& counts) {
    // The class has no subclasses (SDK), so an exact class match replaces IsA's superclass walk.
    UClass* const reflectClass = USceneCaptureReflectComponent::StaticClass();
    auto* const objects = UObject::GObjObjects;
    int const count = static_cast<int>(objects->Count());
    int const end = begin + limit < count ? begin + limit : count;
    for (int i = begin; i < end && reflectClass != nullptr; ++i) {
        UObject* const object = objects->GetData()[i];
        if (object == nullptr || object->Class != reflectClass) {
            continue;
        }
        auto const flags = static_cast<std::uint64_t>(object->ObjectFlags);
        if ((flags & (kTemplateFlags | kPendingKill)) != 0) {
            continue;
        }
        auto* const capture = static_cast<USceneCaptureReflectComponent*>(object);
        ++counts.seen;
        if (capture->FrameRate >= rate) {
            continue;
        }
        // Called directly: the SDK's generated wrapper corrupts FunctionFlags (CLAUDE.md).
        USceneCaptureComponent_execSetFrameRate_Parms parms;
        parms.NewFrameRate = rate;
        capture->ProcessEvent(reinterpret_cast<UFunction*>(setFrameRate), &parms, nullptr);
        if (capture->FrameRate >= rate) {
            ++counts.raised;
        } else {
            ++counts.failed;
        }
    }
    counts.done = end >= count;
    return end;
}

} // namespace mevr::engine::legacy

#endif  // !MEVR_GAME_LE1
