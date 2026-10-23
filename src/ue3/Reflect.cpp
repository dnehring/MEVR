// SPDX-License-Identifier: MIT
#include "ue3/Reflect.hpp"

#include <format>

namespace mevr::ue3 {
namespace {

// A field chain longer than this is not a field chain (a wrong layout or a freed struct).
constexpr int kMaxChain = 20000;
constexpr int kMaxDepth = 64;  // superclass and outer chains

struct KindByName {
    std::string_view name;
    Kind kind;
};
constexpr KindByName kKinds[] = {
    {"BoolProperty", Kind::Bool},         {"ByteProperty", Kind::Byte},
    {"IntProperty", Kind::Int},           {"FloatProperty", Kind::Float},
    {"NameProperty", Kind::Name},         {"StrProperty", Kind::Str},
    {"ObjectProperty", Kind::Object},     {"ClassProperty", Kind::Class},
    {"ComponentProperty", Kind::Component}, {"InterfaceProperty", Kind::Interface},
    {"StructProperty", Kind::Struct},     {"ArrayProperty", Kind::Array},
    {"DelegateProperty", Kind::Delegate}, {"MapProperty", Kind::Map},
    {"StringRefProperty", Kind::StringRef},
};

} // namespace

std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = names::Fold(c);
    }
    return out;
}

char const* KindName(Kind kind) {
    switch (kind) {
    case Kind::Bool: return "bool";
    case Kind::Byte: return "byte";
    case Kind::Int: return "int";
    case Kind::Float: return "float";
    case Kind::Name: return "name";
    case Kind::Str: return "string";
    case Kind::Object: return "object";
    case Kind::Class: return "class";
    case Kind::Component: return "component";
    case Kind::Interface: return "interface";
    case Kind::Struct: return "struct";
    case Kind::Array: return "array";
    case Kind::Delegate: return "delegate";
    case Kind::Map: return "map";
    case Kind::StringRef: return "stringref";
    case Kind::Unknown: break;
    }
    return "unknown";
}

std::int32_t Reflection::ObjectCount() const {
    return u_.objects != nullptr && u_.objects->data != nullptr ? u_.objects->count : 0;
}

Object* Reflection::ObjectAt(std::int32_t index) const {
    if (index < 0 || index >= ObjectCount()) {
        return nullptr;
    }
    return u_.objects->data[index];
}

std::string Reflection::NameText(Object const* object) const {
    return object != nullptr ? ToString(NameOf(object)) : std::string("None");
}

bool Reflection::NameIs(Object const* object, std::string_view text) const {
    return object != nullptr && names::Equals(u_.layout, u_.pools, NameOf(object), text);
}

std::string Reflection::PathName(Object const* object) const {
    if (object == nullptr) {
        return "None";
    }
    std::vector<Object const*> chain;
    for (Object const* o = object; o != nullptr && static_cast<int>(chain.size()) < kMaxDepth; o = OuterOf(o)) {
        chain.push_back(o);
    }
    std::string path;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (!path.empty()) {
            path += '.';
        }
        path += NameText(*it);
    }
    return path;
}

std::string Reflection::FullName(Object const* object) const {
    if (object == nullptr) {
        return "None";
    }
    return NameText(ClassOf(object)) + " " + PathName(object);
}

bool Reflection::IsChildOf(Struct const* s, Struct const* parent) const {
    if (parent == nullptr) {
        return false;
    }
    int depth = 0;
    for (Struct const* at = s; at != nullptr && depth < kMaxDepth; at = SuperOf(at), ++depth) {
        if (at == parent) {
            return true;
        }
    }
    return false;
}

bool Reflection::IsA(Object const* object, Class const* cls) const {
    return object != nullptr && cls != nullptr && IsChildOf(ClassOf(object), cls);
}

bool Reflection::IsAlive(Object const* object) const {
    if (object == nullptr) {
        return false;
    }
    std::int32_t const index = IndexOf(object);
    if (ObjectAt(index) != object) {
        return false;
    }
    return (FlagsOf(object) & u_.layout.pendingKillFlag) == 0;
}

Class* Reflection::ClassClass() {
    if (classClass_ != nullptr) {
        return classClass_;
    }
    std::int32_t const count = ObjectCount();
    for (std::int32_t i = 0; i < count; ++i) {
        Object* const object = ObjectAt(i);
        if (object != nullptr && ClassOf(object) == object && NameIs(object, "Class")) {
            classClass_ = static_cast<Class*>(object);
            break;
        }
    }
    return classClass_;
}

void Reflection::IndexClasses() {
    classes_.clear();
    Class* const classClass = ClassClass();
    std::int32_t const count = ObjectCount();
    indexedCount_ = count;
    if (classClass == nullptr) {
        return;
    }
    for (std::int32_t i = 0; i < count; ++i) {
        Object* const object = ObjectAt(i);
        if (object != nullptr && ClassOf(object) == classClass) {
            classes_.emplace(Lower(NameText(object)), static_cast<Class*>(object));
        }
    }
}

Class* Reflection::FindClassNamed(std::string_view name, std::string_view package) {
    std::string const key = Lower(name);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1 || indexedCount_ < 0) {
            if (pass == 1 && indexedCount_ == ObjectCount()) {
                break;  // nothing new loaded since the index was built
            }
            IndexClasses();
        }
        auto const [begin, end] = classes_.equal_range(key);
        Class* other = nullptr;
        for (auto it = begin; it != end; ++it) {
            if (package.empty() || NameIs(OuterOf(it->second), package)) {
                return it->second;
            }
            if (other == nullptr) {
                other = it->second;
            }
        }
        if (other != nullptr) {
            notes_.push_back(std::format("class {}.{} not found; using {}", package, name, PathName(other)));
            return other;
        }
    }
    return nullptr;
}

Class* Reflection::FindClass(std::string_view path) {
    std::size_t const dot = path.rfind('.');
    if (dot == std::string_view::npos) {
        return FindClassNamed(path, {});
    }
    return FindClassNamed(path.substr(dot + 1), path.substr(0, dot));
}

Class* Reflection::CoreClass(Class*& slot, std::string_view name) {
    if (slot == nullptr) {
        slot = FindClassNamed(name, "Core");
    }
    return slot;
}

Class* Reflection::FunctionClass() {
    return CoreClass(functionClass_, "Function");
}

Class* Reflection::PropertyClass() {
    return CoreClass(propertyClass_, "Property");
}

Function* Reflection::FindFunction(Struct const* owner, std::string_view name) {
    if (owner == nullptr || FunctionClass() == nullptr) {
        return nullptr;
    }
    int depth = 0;
    for (Struct const* s = owner; s != nullptr && depth < kMaxDepth; s = SuperOf(s), ++depth) {
        int n = 0;
        for (Field* f = ChildrenOf(s); f != nullptr && n < kMaxChain; f = NextOf(f), ++n) {
            if (NameIs(f, name) && IsA(f, functionClass_)) {
                return static_cast<Function*>(f);
            }
        }
    }
    return nullptr;
}

Function* Reflection::FindFunction(std::string_view path) {
    std::size_t const dot = path.rfind('.');
    if (dot == std::string_view::npos || FunctionClass() == nullptr) {
        return nullptr;
    }
    Class* const owner = FindClass(path.substr(0, dot));
    if (owner == nullptr) {
        return nullptr;
    }
    std::string_view const name = path.substr(dot + 1);
    int n = 0;
    for (Field* f = ChildrenOf(owner); f != nullptr && n < kMaxChain; f = NextOf(f), ++n) {
        if (NameIs(f, name) && IsA(f, functionClass_)) {
            return static_cast<Function*>(f);
        }
    }
    return nullptr;
}

std::vector<Function*> Reflection::FindFunctionsNamed(std::string_view name) {
    std::vector<Function*> found;
    Class* const functionClass = FunctionClass();
    if (functionClass == nullptr) {
        return found;
    }
    std::int32_t const count = ObjectCount();
    for (std::int32_t i = 0; i < count; ++i) {
        Object* const object = ObjectAt(i);
        if (object != nullptr && ClassOf(object) == functionClass && NameIs(object, name)) {
            found.push_back(static_cast<Function*>(object));
        }
    }
    return found;
}

Property* Reflection::FindProperty(Struct const* owner, std::string_view name) {
    if (owner == nullptr || PropertyClass() == nullptr) {
        return nullptr;
    }
    int depth = 0;
    for (Struct const* s = owner; s != nullptr && depth < kMaxDepth; s = SuperOf(s), ++depth) {
        int n = 0;
        for (Field* f = ChildrenOf(s); f != nullptr && n < kMaxChain; f = NextOf(f), ++n) {
            if (NameIs(f, name) && IsA(f, propertyClass_)) {
                return static_cast<Property*>(f);
            }
        }
    }
    return nullptr;
}

Kind Reflection::KindOf(Property const* property) {
    if (property == nullptr) {
        return Kind::Unknown;
    }
    Class const* const cls = ClassOf(property);
    auto const it = kinds_.find(cls);
    if (it != kinds_.end()) {
        return it->second;
    }
    // The class itself or the nearest Core.*Property it derives from (game-specific subclasses).
    Kind kind = Kind::Unknown;
    int depth = 0;
    for (Struct const* s = cls; s != nullptr && kind == Kind::Unknown && depth < kMaxDepth; s = SuperOf(s), ++depth) {
        for (KindByName const& k : kKinds) {
            if (NameIs(s, k.name)) {
                kind = k.kind;
                break;
            }
        }
    }
    kinds_.emplace(cls, kind);
    return kind;
}

std::optional<PropertyInfo> Reflection::Describe(Property const* property) {
    if (property == nullptr) {
        return std::nullopt;
    }
    Layout const& l = u_.layout;
    PropertyInfo info;
    info.property = const_cast<Property*>(property);
    info.offset = Read<std::int32_t>(property, l.propertyOffset);
    info.elementSize = Read<std::int32_t>(property, l.propertyElementSize);
    info.arrayDim = Read<std::int32_t>(property, l.propertyArrayDim);
    info.flags = Read<std::uint64_t>(property, l.propertyFlags);
    info.kind = KindOf(property);
    switch (info.kind) {
    case Kind::Bool:
        info.boolMask = Read<std::uint32_t>(property, l.propertyTyped);
        break;
    case Kind::Byte:
    case Kind::Object:
    case Kind::Class:
    case Kind::Component:
    case Kind::Interface:
    case Kind::Struct:
    case Kind::Array:
        info.typed = Read<Object*>(property, l.propertyTyped);
        break;
    default:
        break;
    }
    return info;
}

std::optional<PropertyInfo> Reflection::PropertyOf(Struct const* owner, std::string_view name) {
    if (owner == nullptr) {
        return std::nullopt;
    }
    PropertyClass();
    std::string key = std::format("{:x}|", reinterpret_cast<std::uintptr_t>(owner));
    key += Lower(name);
    auto const it = properties_.find(key);
    if (it != properties_.end()) {
        return it->second;
    }
    std::optional<PropertyInfo> info = Describe(FindProperty(owner, name));
    properties_.emplace(std::move(key), info);
    return info;
}

Struct* Reflection::StructOf(PropertyInfo const& info) {
    if (info.kind != Kind::Struct || info.typed == nullptr) {
        return nullptr;
    }
    // A script struct's class is Core.ScriptStruct, a Core.Struct.
    if (CoreClass(structClass_, "Struct") == nullptr || !IsChildOf(ClassOf(info.typed), structClass_)) {
        return nullptr;
    }
    return static_cast<Struct*>(info.typed);
}

std::vector<ParamSpec> Reflection::Parameters(Function const* f) {
    std::vector<ParamSpec> params;
    if (f == nullptr || PropertyClass() == nullptr) {
        return params;
    }
    int n = 0;
    for (Field* field = ChildrenOf(f); field != nullptr && n < kMaxChain; field = NextOf(field), ++n) {
        if (!IsA(field, propertyClass_)) {
            continue;
        }
        std::optional<PropertyInfo> const info = Describe(static_cast<Property*>(field));
        if (!info || (info->flags & property_flags::kParm) == 0) {
            continue;
        }
        ParamSpec spec;
        spec.name = NameText(field);
        spec.offset = info->offset;
        spec.size = info->elementSize * (info->arrayDim > 0 ? info->arrayDim : 1);
        spec.flags = info->flags;
        spec.boolMask = info->kind == Kind::Bool ? info->boolMask : 0u;
        spec.kind = info->kind;
        params.push_back(std::move(spec));
    }
    return params;
}

void Reflection::Forget() {
    classClass_ = functionClass_ = propertyClass_ = structClass_ = nullptr;
    classes_.clear();
    indexedCount_ = -1;
    kinds_.clear();
    properties_.clear();
}

std::vector<std::string> Reflection::TakeNotes() {
    std::vector<std::string> out;
    out.swap(notes_);
    return out;
}

} // namespace mevr::ue3
