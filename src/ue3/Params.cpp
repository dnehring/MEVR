// SPDX-License-Identifier: MIT
#include "ue3/Params.hpp"

#include <algorithm>

namespace mevr::ue3 {
namespace {

// ProcessEvent copies ParmsSize bytes in and out; a little slack costs nothing and keeps a
// parameter that ends exactly at the block's end safe if ParmsSize was rounded down.
constexpr std::int32_t kSlack = 16;

} // namespace

Params::Params(std::vector<ParamSpec> specs, std::int32_t parmsSize) : specs_(std::move(specs)) {
    std::int32_t end = parmsSize;
    for (ParamSpec const& spec : specs_) {
        end = std::max(end, spec.offset + spec.size);
    }
    Allocate(end);
    valid_ = true;
}

Params::Params(Reflection& reflection, Function const* function) {
    if (function == nullptr) {
        Allocate(0);
        return;
    }
    specs_ = reflection.Parameters(function);
    std::int32_t end = reflection.ParmsSize(function);
    for (ParamSpec const& spec : specs_) {
        end = std::max(end, spec.offset + spec.size);
    }
    Allocate(end);
    valid_ = true;
}

void Params::Allocate(std::int32_t parmsSize) {
    size_ = std::max(parmsSize, 0);
    std::size_t const words = (static_cast<std::size_t>(size_ + kSlack) + 7) / 8;
    buffer_ = std::make_unique<std::uint64_t[]>(words);  // value-initialized: zeroed
}

ParamSpec const* Params::Find(std::string_view name) const {
    for (ParamSpec const& spec : specs_) {
        if (names::EqualsNoCase(spec.name, name)) {
            return &spec;
        }
    }
    return nullptr;
}

ParamSpec const* Params::ReturnSpec() const {
    for (ParamSpec const& spec : specs_) {
        if ((spec.flags & property_flags::kReturnParm) != 0) {
            return &spec;
        }
    }
    return nullptr;
}

bool Params::SetBool(std::string_view name, bool value) {
    ParamSpec const* const spec = Find(name);
    if (spec == nullptr || spec->boolMask == 0) {
        return false;
    }
    std::uint32_t word = 0;
    std::memcpy(&word, Bytes() + spec->offset, sizeof(word));
    word = value ? (word | spec->boolMask) : (word & ~spec->boolMask);
    std::memcpy(Bytes() + spec->offset, &word, sizeof(word));
    return true;
}

std::optional<bool> Params::ReadBool(ParamSpec const* spec) const {
    if (spec == nullptr || spec->boolMask == 0) {
        return std::nullopt;
    }
    std::uint32_t word = 0;
    std::memcpy(&word, Bytes() + spec->offset, sizeof(word));
    return (word & spec->boolMask) != 0;
}

std::optional<bool> Params::GetBool(std::string_view name) const {
    return ReadBool(Find(name));
}

std::optional<bool> Params::ReturnBool() const {
    // A native writes its bool result as a whole UBOOL (*(UBOOL*)Result = ...), and script tests the word for
    // non-zero: any bit counts, not just the property's mask (the generated *_Parms blocks read it the same way).
    ParamSpec const* const spec = ReturnSpec();
    if (spec == nullptr || spec->boolMask == 0) {
        return std::nullopt;
    }
    std::uint32_t word = 0;
    std::memcpy(&word, Bytes() + spec->offset, sizeof(word));
    return word != 0;
}

} // namespace mevr::ue3
