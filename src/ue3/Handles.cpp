// SPDX-License-Identifier: MIT
#include "ue3/Handles.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"

#include <cstring>

namespace mevr::ue3 {

bool detail::Resolvable(Reach reach) {
    return reach == Reach::EveryGame ? runtime::Up() : runtime::Ready();
}

Class* ClassRef::Get() {
    if (!tried_ && detail::Resolvable(reach_)) {
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
    if (!tried_ && detail::Resolvable(reach_)) {
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
    if (!tried_ && detail::Resolvable(Reaches())) {
        Reflection& r = runtime::Get();
        Struct const* owner = nullptr;
        if (outer_ != nullptr) {
            PropertyInfo const* const outer = outer_->Get();
            if (outer == nullptr) {
                return nullptr;  // the outer field already said why
            }
            // A struct member, or a member of an array's struct elements.
            PropertyInfo const* const element = outer->kind == Kind::Array ? outer_->Inner() : outer;
            owner = element != nullptr ? r.StructOf(*element) : nullptr;
        } else if (function_ != nullptr) {
            owner = function_->Get();
            if (owner == nullptr) {
                tried_ = true;
                return nullptr;  // the function already said why
            }
        } else {
            owner = r.FindClass(classPath_);
            owner_ = owner;
            runtime::FlushNotes();
        }
        tried_ = true;
        info_ = r.PropertyOf(owner, name_);
        if (info_ && function_ != nullptr && (info_->flags & property_flags::kParm) == 0) {
            info_.reset();  // a local of the function, not a parameter
        }
        if (!info_) {
            log::Warn("reflect: {} {}.{} not found", function_ != nullptr ? "parameter" : "field",
                outer_ != nullptr ? outer_->Name() : function_ != nullptr ? function_->Path() : classPath_, name_);
        }
    }
    return info_ ? &*info_ : nullptr;
}

PropertyInfo const* FieldRef::Inner() {
    PropertyInfo const* const info = Get();
    if (info == nullptr || info->kind != Kind::Array || info->typed == nullptr) {
        return nullptr;
    }
    if (!inner_) {
        inner_ = runtime::Get().Describe(static_cast<Property const*>(info->typed));
        if (!inner_ || inner_->elementSize <= 0) {
            inner_.reset();
            return nullptr;
        }
    }
    return &*inner_;
}

std::optional<Elements> FieldRef::ElementsOf(void const* base, std::int32_t maxCount) {
    PropertyInfo const* const inner = Inner();
    TArray<std::uint8_t> const* const array = inner != nullptr ? In<TArray<std::uint8_t>>(base) : nullptr;
    if (array == nullptr || array->count < 0 || array->count > maxCount || (array->data == nullptr && array->count > 0)) {
        return std::nullopt;
    }
    return Elements{array->data, array->count, inner->elementSize};
}

bool FieldRef::Fits(void const* base) {
    if (outer_ != nullptr || function_ != nullptr || owner_ == nullptr || base == nullptr) {
        return true;
    }
    struct Check {
        Object const* object;
        Struct const* owner;
        bool fits;
        Class const* cls;
    } check{static_cast<Object const*>(base), owner_, false, nullptr};
    // Guarded: a kept pointer can be memory the engine gave back (the caller's own read would have faulted there anyway).
    bool const read = runtime::Guarded([](void* context) {
        auto* const c = static_cast<Check*>(context);
        Reflection& r = runtime::Get();
        c->cls = r.ClassOf(c->object);
        c->fits = r.IsChildOf(c->cls, c->owner);
    }, &check);
    if (read && check.fits) {
        return true;
    }
    if (!toldMisfit_) {
        toldMisfit_ = true;
        log::Warn("reflect: field {}.{} refused on 0x{:X} ({}): not an object of that class (a pointer kept after the engine freed "
                  "its object, the memory now another object's?)",
            classPath_, name_, reinterpret_cast<std::uintptr_t>(base),
            read && check.cls != nullptr ? runtime::FullName(check.cls) : std::string("unreadable"));
    }
    return false;
}

std::optional<bool> FieldRef::ReadBool(void const* base) {
    PropertyInfo const* const info = Get();
    if (info == nullptr || base == nullptr || info->kind != Kind::Bool || info->boolMask == 0 || !Fits(base)) {
        return std::nullopt;
    }
    std::uint32_t word = 0;
    std::memcpy(&word, static_cast<std::uint8_t const*>(base) + info->offset, sizeof(word));
    return (word & info->boolMask) != 0;
}

bool FieldRef::WriteBool(void* base, bool value) {
    PropertyInfo const* const info = Get();
    if (info == nullptr || base == nullptr || info->kind != Kind::Bool || info->boolMask == 0 || !Fits(base)) {
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
