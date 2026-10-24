// SPDX-License-Identifier: MIT
#include "ue3/Handles.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"

#include <cstring>

namespace mevr::ue3 {

Class* ClassRef::Get() {
    if (!tried_ && runtime::Ready()) {
        tried_ = true;
        cls_ = runtime::Get().FindClass(path_);
        runtime::FlushNotes();
        if (cls_ == nullptr) {
            log::Warn("reflect: class {} not found", path_);
        }
    }
    return cls_;
}

Function* FunctionRef::Get() {
    if (!tried_ && runtime::Ready()) {
        tried_ = true;
        function_ = runtime::Get().FindFunction(path_);
        runtime::FlushNotes();
        if (function_ == nullptr) {
            log::Warn("reflect: function {} not found", path_);
        }
    }
    return function_;
}

PropertyInfo const* FieldRef::Get() {
    if (!tried_ && runtime::Ready()) {
        Reflection& r = runtime::Get();
        Struct const* owner = nullptr;
        if (outer_ != nullptr) {
            PropertyInfo const* const outer = outer_->Get();
            owner = outer != nullptr ? r.StructOf(*outer) : nullptr;
            if (outer == nullptr) {
                return nullptr;  // the outer field already said why
            }
        } else {
            owner = r.FindClass(classPath_);
            runtime::FlushNotes();
        }
        tried_ = true;
        info_ = r.PropertyOf(owner, name_);
        if (!info_) {
            log::Warn("reflect: field {}.{} not found", outer_ != nullptr ? outer_->Name() : classPath_, name_);
        }
    }
    return info_ ? &*info_ : nullptr;
}

std::optional<bool> FieldRef::ReadBool(void const* base) {
    PropertyInfo const* const info = Get();
    if (info == nullptr || base == nullptr || info->kind != Kind::Bool || info->boolMask == 0) {
        return std::nullopt;
    }
    std::uint32_t word = 0;
    std::memcpy(&word, static_cast<std::uint8_t const*>(base) + info->offset, sizeof(word));
    return (word & info->boolMask) != 0;
}

bool FieldRef::WriteBool(void* base, bool value) {
    PropertyInfo const* const info = Get();
    if (info == nullptr || base == nullptr || info->kind != Kind::Bool || info->boolMask == 0) {
        return false;
    }
    auto* const word = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(base) + info->offset);
    *word = value ? (*word | info->boolMask) : (*word & ~info->boolMask);
    write_journal::Wrote(name_, word);  // the field's name: a literal from the handle's declaration
    return true;
}

bool IsA(Object const* object, ClassRef& cls) {
    Class* const c = cls.Get();
    return c != nullptr && object != nullptr && runtime::Get().IsA(object, c);
}

} // namespace mevr::ue3
