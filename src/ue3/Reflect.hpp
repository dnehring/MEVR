// SPDX-License-Identifier: MIT
#pragma once

// Our UE3 reflection (research/34): classes, functions and properties found at run time through
// the engine's own reflection data (GObjects, the name pools and the UField chains), so the mod
// carries no class layouts of its own. Pure over memory: give it a Universe (the layout plus where
// GObjects and the name pools are) and it reads; ue3/Runtime builds the game's Universe, the
// tests build synthetic ones.
//
// Not thread-safe: lookups fill caches. Use it on the game thread (or before the hooks go live).
// Raw reads trust the layout; ue3::runtime validates it once before anything else runs.

#include "ue3/Layout.hpp"
#include "ue3/Names.hpp"
#include "ue3/Types.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mevr::ue3 {

struct Universe {
    Layout layout;
    TArray<Object*> const* objects = nullptr;  // GObjects
    names::Pools pools;
};

// A property's type, from its class's name (Core.BoolProperty...).
enum class Kind : std::uint8_t {
    Unknown,
    Bool,
    Byte,
    Int,
    Float,
    Name,
    Str,
    Object,
    Class,
    Component,
    Interface,
    Struct,
    Array,
    Delegate,
    Map,
    StringRef,
};
char const* KindName(Kind kind);

struct PropertyInfo {
    Property* property = nullptr;
    std::int32_t offset = 0;
    std::uint32_t boolMask = 0;  // Kind::Bool: the bit in the uint32 at `offset`
    std::int32_t elementSize = 0;
    std::int32_t arrayDim = 1;
    std::uint64_t flags = 0;
    Kind kind = Kind::Unknown;
    // The typed subclass's pointer: the struct (Struct), the class (Object, Class, Component), the
    // inner property (Array), the enum (Byte; may be null).
    Object* typed = nullptr;
};

// A parameter of a UFunction, as the parameter block holds it.
struct ParamSpec {
    std::string name;
    std::int32_t offset = 0;
    std::int32_t size = 0;  // elementSize * arrayDim
    std::uint64_t flags = 0;
    std::uint32_t boolMask = 0;  // non-zero for a bool parameter
    Kind kind = Kind::Unknown;
};

class Reflection {
public:
    Reflection() = default;
    explicit Reflection(Universe const& universe) : u_(universe) {}

    Universe const& universe() const { return u_; }
    Layout const& layout() const { return u_.layout; }

    // ---- raw object header -------------------------------------------------------------------
    std::int32_t ObjectCount() const;
    Object* ObjectAt(std::int32_t index) const;
    std::int32_t IndexOf(Object const* object) const { return Read<std::int32_t>(object, u_.layout.objectIndex); }
    std::uint64_t FlagsOf(Object const* object) const { return Read<std::uint64_t>(object, u_.layout.objectFlags); }
    Object* OuterOf(Object const* object) const { return Read<Object*>(object, u_.layout.objectOuter); }
    Name NameOf(Object const* object) const { return Read<Name>(object, u_.layout.objectName); }
    Class* ClassOf(Object const* object) const { return Read<Class*>(object, u_.layout.objectClass); }
    Struct* SuperOf(Struct const* s) const { return Read<Struct*>(s, u_.layout.fieldSuper); }
    Field* NextOf(Field const* f) const { return Read<Field*>(f, u_.layout.fieldNext); }
    Field* ChildrenOf(Struct const* s) const { return Read<Field*>(s, u_.layout.structChildren); }

    // ---- names -------------------------------------------------------------------------------
    std::string ToString(Name name) const { return names::ToString(u_.layout, u_.pools, name); }
    std::string NameText(Object const* object) const;  // "None" for null
    bool NameIs(Object const* object, std::string_view text) const;
    // "Class Engine.Actor", "Function Engine.Actor.SetRotation": the class name, then the outer
    // chain joined with '.' (the engine itself prints ':' after a top-level object's name).
    std::string FullName(Object const* object) const;
    std::string PathName(Object const* object) const;

    // ---- types -------------------------------------------------------------------------------
    bool IsA(Object const* object, Class const* cls) const;
    bool IsChildOf(Struct const* s, Struct const* parent) const;
    bool IsAlive(Object const* object) const;  // a live GObjects entry, not pending kill
    // The nearest outer of `object` (not the object itself) that IsA `cls`, or nullptr: e.g. the World an
    // actor's level belongs to (Actor -> Level -> World). Reads only, no caches: any thread.
    Object* OuterOfA(Object const* object, Class const* cls) const;

    // The bootstrap: Core.Class is the class whose class is itself.
    Class* ClassClass();
    // "Core.Function", "Core.Property", "Core.ScriptStruct", "Core.Struct".
    Class* FunctionClass();
    Class* PropertyClass();

    // "Package.Name" or just "Name". With a package and no class in it, a class of that name in
    // another package is returned (and noted: Notes()). nullptr if none.
    Class* FindClass(std::string_view path);
    // The function declared on `owner` or the nearest superclass.
    Function* FindFunction(Struct const* owner, std::string_view name);
    // "Engine.Actor.SetRotation": declared on exactly that class.
    Function* FindFunction(std::string_view path);
    // Every function of that name, on any class (e.g. all PlayerTick overrides). Walks GObjects.
    std::vector<Function*> FindFunctionsNamed(std::string_view name);
    // The property declared on `owner` or the nearest superclass (also a function's parameters).
    Property* FindProperty(Struct const* owner, std::string_view name);

    Kind KindOf(Property const* property);
    std::optional<PropertyInfo> Describe(Property const* property);
    // Cached per (owner, name, case-insensitively).
    std::optional<PropertyInfo> PropertyOf(Struct const* owner, std::string_view name);
    // A struct property's script struct (validated: it must be a Core.Struct).
    Struct* StructOf(PropertyInfo const& info);

    // ---- functions ---------------------------------------------------------------------------
    std::uint32_t FunctionFlags(Function const* f) const { return Read<std::uint32_t>(f, u_.layout.functionFlags); }
    std::uint16_t NativeIndex(Function const* f) const { return Read<std::uint16_t>(f, u_.layout.functionNative); }
    std::uint16_t ParmsSize(Function const* f) const { return Read<std::uint16_t>(f, u_.layout.functionParmsSize); }
    std::uint16_t ReturnOffset(Function const* f) const { return Read<std::uint16_t>(f, u_.layout.functionReturnOffset); }
    void* NativeFunc(Function const* f) const { return Read<void*>(f, u_.layout.functionFunc); }
    std::int32_t PropertySize(Struct const* s) const { return Read<std::int32_t>(s, u_.layout.structPropertySize); }
    // The parameters in declaration order (CPF_Parm), the return value included.
    std::vector<ParamSpec> Parameters(Function const* f);

    // Drops every cache (e.g. after GObjects was found again).
    void Forget();
    // Things worth logging once (a class found in another package...). Cleared by TakeNotes.
    std::vector<std::string> TakeNotes();

    template <class T>
    static T Read(void const* base, std::uint32_t offset) {
        T value{};
        std::memcpy(&value, static_cast<std::uint8_t const*>(base) + offset, sizeof(T));
        return value;
    }

private:
    Class* FindClassNamed(std::string_view name, std::string_view package);
    void IndexClasses();
    Class* CoreClass(Class*& slot, std::string_view name);

    Universe u_;
    Class* classClass_ = nullptr;
    Class* functionClass_ = nullptr;
    Class* propertyClass_ = nullptr;
    Class* structClass_ = nullptr;
    // Classes by lower-case name; rebuilt when a lookup misses and GObjects grew.
    std::unordered_multimap<std::string, Class*> classes_;
    std::int32_t indexedCount_ = -1;
    std::unordered_map<Class const*, Kind> kinds_;
    std::unordered_map<std::string, std::optional<PropertyInfo>> properties_;
    std::vector<std::string> notes_;
};

// Lower-case ASCII copy (cache keys).
std::string Lower(std::string_view text);

} // namespace mevr::ue3
