// SPDX-License-Identifier: MIT
#pragma once

// A UFunction's parameter block, built from the function's own parameter properties (offsets,
// sizes, bool masks) instead of a generated *_Parms struct. Zeroed on construction; values go in
// and out by parameter name (case-insensitive), with the size checked against the type.
//
//   ue3::Params p(reflection, fn);
//   p.Set("NewRotation", rotation);
//   ue3::runtime::CallProcessEvent(pawn, fn, p);
//   bool ok = p.ReturnBool().value_or(false);

#include "ue3/Reflect.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace mevr::ue3 {

class Params {
public:
    Params() = default;
    // From a parameter list and the function's ParmsSize (the tests build these by hand).
    Params(std::vector<ParamSpec> specs, std::int32_t parmsSize);
    Params(Reflection& reflection, Function const* function);

    // False if the function had no parameter information (null function).
    bool Valid() const { return valid_; }
    void* Data() { return buffer_.get(); }
    void const* Data() const { return buffer_.get(); }
    std::int32_t Size() const { return size_; }
    std::vector<ParamSpec> const& Specs() const { return specs_; }

    ParamSpec const* Find(std::string_view name) const;
    ParamSpec const* ReturnSpec() const;

    // False (nothing written) if there's no such parameter or its size isn't sizeof(T).
    template <class T>
    bool Set(std::string_view name, T const& value) {
        ParamSpec const* const spec = Find(name);
        if (spec == nullptr || spec->size != static_cast<std::int32_t>(sizeof(T)) || spec->boolMask != 0) {
            return false;
        }
        std::memcpy(Bytes() + spec->offset, &value, sizeof(T));
        return true;
    }
    template <class T>
    std::optional<T> Get(std::string_view name) const {
        return Read<T>(Find(name));
    }
    template <class T>
    std::optional<T> Return() const {
        return Read<T>(ReturnSpec());
    }

    bool SetBool(std::string_view name, bool value);
    std::optional<bool> GetBool(std::string_view name) const;
    // A bool return value as UE3 tests it: the whole 32-bit word non-zero (natives write a UBOOL), not only
    // the mask bit; nullopt if there's no bool return value.
    std::optional<bool> ReturnBool() const;

private:
    template <class T>
    std::optional<T> Read(ParamSpec const* spec) const {
        if (spec == nullptr || spec->size != static_cast<std::int32_t>(sizeof(T)) || spec->boolMask != 0) {
            return std::nullopt;
        }
        T value{};
        std::memcpy(&value, Bytes() + spec->offset, sizeof(T));
        return value;
    }
    std::optional<bool> ReadBool(ParamSpec const* spec) const;
    std::uint8_t* Bytes() const { return reinterpret_cast<std::uint8_t*>(buffer_.get()); }
    void Allocate(std::int32_t parmsSize);

    std::vector<ParamSpec> specs_;
    std::unique_ptr<std::uint64_t[]> buffer_;  // 8-byte aligned, as the engine's own blocks
    std::int32_t size_ = 0;
    bool valid_ = false;
};

} // namespace mevr::ue3
