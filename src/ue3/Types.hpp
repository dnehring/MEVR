#pragma once

// Our own UE3 core types (research/34): just the shapes the engine's memory has, written from our
// own analysis (Ghidra over the decrypted exe, research/34 section 2). Engine objects are never
// constructed or copied here: the tag types below only name pointers into the game's memory, and
// every field is read at an offset from ue3::Layout or found through the engine's reflection.

#include <cstdint>
#include <cstring>

namespace mevr::ue3 {

// Pointer tags for engine objects. Never instantiated; the inheritance only lets a Class* pass
// where an Object* is wanted.
struct Object {
    Object() = delete;
};
struct Field : Object {};
struct Struct : Field {};
struct Class : Struct {};
struct Function : Struct {};
struct Property : Field {};

// A name as the engine stores it inside objects: an index into the name pools and a number
// ("Foo_3" is {Foo, 4}; 0 means no number). Names compare by value, case-insensitively by text.
struct Name {
    std::int32_t index = 0;
    std::int32_t number = 0;

    std::uint64_t Key() const {
        std::uint64_t key = 0;
        std::memcpy(&key, this, sizeof(key));
        return key;
    }
    friend bool operator==(Name a, Name b) { return a.index == b.index && a.number == b.number; }
};
static_assert(sizeof(Name) == 8);

// The engine's dynamic array: data, count, capacity. Read-only views here: anything that grows or
// frees one must go through the engine's allocator (research/34, "memory").
template <class T>
struct TArray {
    T* data = nullptr;
    std::int32_t count = 0;
    std::int32_t max = 0;

    std::int32_t Count() const { return count; }
    T* begin() const { return data; }
    T* end() const { return data != nullptr ? data + count : nullptr; }
    bool Valid(std::int32_t i) const { return data != nullptr && i >= 0 && i < count; }
};
static_assert(sizeof(TArray<int>) == 16);

// A string: a TArray of UTF-16 code units including the terminating zero (count 0 when empty).
struct FString : TArray<wchar_t> {
    wchar_t const* Chars() const { return data != nullptr && count > 0 ? data : L""; }
};
static_assert(sizeof(FString) == 16);

// Script math structs (Core.Object.Vector, Rotator, Vector2D). The self-check confirms their size
// and member offsets through reflection.
struct Vector {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
struct Vector2D {
    float x = 0.0f;
    float y = 0.0f;
};
// Unreal rotation units: 65536 = 360 degrees.
struct Rotator {
    std::int32_t pitch = 0;
    std::int32_t yaw = 0;
    std::int32_t roll = 0;
};
static_assert(sizeof(Vector) == 12 && sizeof(Rotator) == 12 && sizeof(Vector2D) == 8);

} // namespace mevr::ue3
