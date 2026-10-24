// SPDX-License-Identifier: MIT
#pragma once

// Cached handles on the running game's reflection (research/34): declare them once (usually at
// namespace scope), resolve on first use, and read fields through them. A handle that can't be
// resolved stays null and says so once in the log; callers then stand down. Game thread only.
//
//   ue3::ClassRef gBioPawn{"SFXGame.BioPawn"};
//   ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType"};
//   if (ue3::IsA(pawn, gBioPawn)) { std::uint8_t const* type = gCoverType.In<std::uint8_t>(pawn); ... }

#include "ue3/Runtime.hpp"

#include <cstdint>
#include <optional>

namespace mevr::ue3 {

// A class by "Package.Name".
class ClassRef {
public:
    explicit constexpr ClassRef(char const* path) : path_(path) {}
    Class* Get();
    char const* Path() const { return path_; }
    void Reset() { cls_ = nullptr, tried_ = false; }

private:
    char const* path_;
    Class* cls_ = nullptr;
    bool tried_ = false;
};

// A function declared on exactly that class: "Engine.Actor.SetRotation".
class FunctionRef {
public:
    explicit constexpr FunctionRef(char const* path) : path_(path) {}
    Function* Get();
    char const* Path() const { return path_; }
    void Reset() { function_ = nullptr, tried_ = false; }

private:
    char const* path_;
    Function* function_ = nullptr;
    bool tried_ = false;
};

// The elements of an array field (a TArray of structs or pointers), with the element's size from
// the array's inner property: `Elements` of `FieldRef{"Engine.AnimTree", "SkelControlLists"}`, then
// the members through `FieldRef{lists, "ControlHead"}` on each `At(i)`.
struct Elements {
    std::uint8_t* data = nullptr;
    std::int32_t count = 0;
    std::int32_t stride = 0;
    void* At(std::int32_t i) const { return data != nullptr && i >= 0 && i < count ? data + static_cast<std::ptrdiff_t>(i) * stride : nullptr; }
};

// A property of a class (its own or inherited), of a script struct reached through a struct
// property (`FieldRef{"Engine.Actor", "Location"}`, then `FieldRef{location, "X"}`) or through an
// array of structs (`FieldRef{lists, "ControlHead"}`: a member of each element), or a parameter of
// a function (`FieldRef{gGetFovAngle, "ReturnValue"}`: In<float>(parms) inside a hook's block).
class FieldRef {
public:
    constexpr FieldRef(char const* classPath, char const* name) : classPath_(classPath), name_(name) {}
    constexpr FieldRef(FieldRef& outer, char const* name) : outer_(&outer), name_(name) {}
    constexpr FieldRef(FunctionRef& function, char const* name) : function_(&function), name_(name) {}

    PropertyInfo const* Get();
    char const* Name() const { return name_; }
    void Reset() { info_.reset(), inner_.reset(), tried_ = false; }

    // The field inside `object` (or inside a struct value at `base` for a struct member), or
    // nullptr: unresolved, a null object, or a size that isn't sizeof(T).
    template <class T>
    T* In(void const* base) {
        PropertyInfo const* const info = Get();
        if (info == nullptr || base == nullptr || info->kind == Kind::Bool
            || info->elementSize * info->arrayDim < static_cast<std::int32_t>(sizeof(T))) {
            return nullptr;
        }
        return reinterpret_cast<T*>(static_cast<std::uint8_t*>(const_cast<void*>(base)) + info->offset);
    }
    // The value, or nullopt.
    template <class T>
    std::optional<T> Read(void const* base) {
        T const* const p = In<T>(base);
        return p != nullptr ? std::optional<T>(*p) : std::nullopt;
    }
    // Where a struct field's value starts (for its members' FieldRefs), or nullptr.
    void* Address(void const* base) { return In<std::uint8_t>(base); }
    // Element `index` of a static array field (`T Foo[4]`), or nullptr: unresolved, out of range, or an
    // element size that isn't sizeof(T).
    template <class T>
    T* At(void const* base, std::int32_t index) {
        PropertyInfo const* const info = Get();
        if (info == nullptr || base == nullptr || info->kind == Kind::Bool || index < 0 || index >= info->arrayDim
            || info->elementSize != static_cast<std::int32_t>(sizeof(T))) {
            return nullptr;
        }
        return reinterpret_cast<T*>(static_cast<std::uint8_t*>(const_cast<void*>(base)) + info->offset) + index;
    }

    // Bool fields (bitfields: the property's mask in the uint32 at its offset).
    std::optional<bool> ReadBool(void const* base);
    bool WriteBool(void* base, bool value);

    // An array field's elements, or nullopt: unresolved, not an array, or an empty or implausible
    // array (more than `maxCount` elements, a null buffer with elements).
    std::optional<Elements> ElementsOf(void const* base, std::int32_t maxCount = 4096);
    // The array's inner property (its element), resolved with the field.
    PropertyInfo const* Inner();

private:
    FieldRef* outer_ = nullptr;
    FunctionRef* function_ = nullptr;
    char const* classPath_ = nullptr;
    char const* name_;
    std::optional<PropertyInfo> info_;
    std::optional<PropertyInfo> inner_;  // Kind::Array: the element's property
    bool tried_ = false;
};

// IsA against a handle's class (false if it doesn't resolve).
bool IsA(Object const* object, ClassRef& cls);
// The object if it IsA that class, else nullptr.
inline Object* As(Object* object, ClassRef& cls) {
    return IsA(object, cls) ? object : nullptr;
}

} // namespace mevr::ue3
