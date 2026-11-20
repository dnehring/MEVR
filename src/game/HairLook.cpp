// SPDX-License-Identifier: MIT
#include "game/HairLook.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Reflect.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace mevr::game::hair_look {
namespace {

// Our reflection layer (research/34); engine/ReflectCheck has a row for each.
ue3::FunctionRef gGetMaterial{"Engine.MeshComponent.GetMaterial", ue3::kEveryGame};
ue3::FunctionRef gSetMaterial{"Engine.MeshComponent.SetMaterial", ue3::kEveryGame};
ue3::FunctionRef gCreateInstance{"Engine.MeshComponent.CreateAndSetMaterialInstanceConstant", ue3::kEveryGame};
ue3::FunctionRef gSetScalar{"Engine.MaterialInstanceConstant.SetScalarParameterValue", ue3::kEveryGame};
ue3::FunctionRef gSetVector{"Engine.MaterialInstanceConstant.SetVectorParameterValue", ue3::kEveryGame};
ue3::FunctionRef gGetScalar{"Engine.MaterialInterface.GetScalarParameterValue", ue3::kEveryGame};
ue3::FunctionRef gGetVector{"Engine.MaterialInterface.GetVectorParameterValue", ue3::kEveryGame};
ue3::FieldRef gParent{"Engine.MaterialInstance", "Parent", ue3::kEveryGame};
ue3::FieldRef gParameterName{"Engine.MaterialExpressionParameter", "ParameterName", ue3::kEveryGame};
ue3::FieldRef gScalarDefault{"Engine.MaterialExpressionScalarParameter", "DefaultValue", ue3::kEveryGame};
ue3::FieldRef gVectorDefault{"Engine.MaterialExpressionVectorParameter", "DefaultValue", ue3::kEveryGame};
ue3::ClassRef gScalarClass{"Engine.MaterialExpressionScalarParameter", ue3::kEveryGame};
ue3::ClassRef gVectorClass{"Engine.MaterialExpressionVectorParameter", ue3::kEveryGame};
ue3::ClassRef gInstanceClass{"Engine.MaterialInstance", ue3::kEveryGame};
ue3::ClassRef gConstantClass{"Engine.MaterialInstanceConstant", ue3::kEveryGame};

struct Color {
    float r = 0, g = 0, b = 0, a = 0;
};

struct Param {
    std::string text;
    ue3::Name name{};
    bool vector = false;
    float scalarDefault = 0;
    Color vectorDefault;
};

std::unordered_map<ue3::Object const*, std::vector<Param>> gParams;  // master material -> its parameters
std::unordered_map<ue3::Object*, ue3::Object*> gInstances;            // component -> its own instance (ours)
std::unordered_map<ue3::Object*, std::string> gApplied;               // component -> the look applied
std::unordered_set<std::string> gWarned;

bool Lower(std::string const& a, std::string const& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

ue3::Object* MaterialOf(ue3::Object* component) {
    if (component == nullptr || gGetMaterial.Get() == nullptr) {
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gGetMaterial.Get());
    if (!p.Valid() || !p.Set<std::int32_t>("ElementIndex", 0)) {
        return nullptr;
    }
    ue3::runtime::Call(component, gGetMaterial.Get(), p);
    return p.Return<ue3::Object*>().value_or(nullptr);
}

// The chain from the component's material up to the master material (the last).
std::vector<ue3::Object*> Chain(ue3::Object* material) {
    std::vector<ue3::Object*> chain;
    ue3::Reflection& r = ue3::runtime::Get();
    for (ue3::Object* m = material; m != nullptr && chain.size() < 16; ) {
        chain.push_back(m);
        if (gInstanceClass.Get() == nullptr || !r.IsA(m, gInstanceClass.Get())) {
            break;
        }
        m = gParent.Read<ue3::Object*>(m).value_or(nullptr);
    }
    return chain;
}

// The master material's parameters, from its MaterialExpression*Parameter objects (their outer is the material).
std::vector<Param> const& ParamsOf(ue3::Object* master) {
    auto it = gParams.find(master);
    if (it != gParams.end()) {
        return it->second;
    }
    std::vector<Param> params;
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const prefix = r.PathName(master) + ".";
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o)) {
            continue;
        }
        bool const scalar = gScalarClass.Get() != nullptr && r.IsA(o, gScalarClass.Get());
        bool const vector = !scalar && gVectorClass.Get() != nullptr && r.IsA(o, gVectorClass.Get());
        if (!scalar && !vector) {
            continue;
        }
        std::string const path = r.PathName(o);
        if (path.rfind(prefix, 0) != 0) {
            continue;
        }
        Param p;
        p.name = gParameterName.Read<ue3::Name>(o).value_or(ue3::Name{});
        p.text = r.ToString(p.name);
        p.vector = vector;
        if (scalar) {
            p.scalarDefault = gScalarDefault.Read<float>(o).value_or(0.0f);
        } else {
            p.vectorDefault = gVectorDefault.Read<Color>(o).value_or(Color{});
        }
        if (p.name.index != 0 && std::none_of(params.begin(), params.end(), [&](Param const& q) { return Lower(q.text, p.text); })) {
            params.push_back(p);
        }
    }
    std::sort(params.begin(), params.end(), [](Param const& a, Param const& b) { return a.text < b.text; });
    return gParams.emplace(master, std::move(params)).first->second;
}

Param const* FindParam(ue3::Object* component, std::string const& text) {
    std::vector<ue3::Object*> const chain = Chain(MaterialOf(component));
    if (chain.empty()) {
        return nullptr;
    }
    for (Param const& p : ParamsOf(chain.back())) {
        if (Lower(p.text, text)) {
            return &p;
        }
    }
    return nullptr;
}

// The component's own instance (created once; again if its material was replaced since).
ue3::Object* OwnInstance(ue3::Object* component) {
    ue3::Object* const current = MaterialOf(component);
    auto it = gInstances.find(component);
    if (it != gInstances.end() && it->second == current && ue3::runtime::IsAlive(current)) {
        return current;
    }
    if (gCreateInstance.Get() == nullptr) {
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gCreateInstance.Get());
    if (!p.Valid() || !p.Set<std::int32_t>("ElementIndex", 0)) {
        return nullptr;
    }
    ue3::runtime::Call(component, gCreateInstance.Get(), p);
    ue3::Object* const instance = p.Return<ue3::Object*>().value_or(nullptr);
    if (instance != nullptr) {
        gInstances[component] = instance;
        log::Info("hair look: {:#x} has its own material instance {:#x} (parent {})", reinterpret_cast<std::uintptr_t>(component),
            reinterpret_cast<std::uintptr_t>(instance), current != nullptr ? ue3::runtime::Get().PathName(current) : std::string("none"));
    }
    return instance;
}

bool SetOn(ue3::Object* instance, Param const& param, std::vector<double> const& values) {
    if (param.vector) {
        if (values.size() < 3 || gSetVector.Get() == nullptr) {
            return false;
        }
        Color c{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]),
                values.size() > 3 ? static_cast<float>(values[3]) : param.vectorDefault.a};
        ue3::Params p(ue3::runtime::Get(), gSetVector.Get());
        return p.Valid() && p.Set("ParameterName", param.name) && p.Set("Value", c) && ue3::runtime::Call(instance, gSetVector.Get(), p);
    }
    if (values.empty() || gSetScalar.Get() == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), gSetScalar.Get());
    return p.Valid() && p.Set("ParameterName", param.name) && p.Set("Value", static_cast<float>(values[0])) &&
           ue3::runtime::Call(instance, gSetScalar.Get(), p);
}

std::string ValueOf(ue3::Object* material, Param const& param) {
    if (param.vector) {
        ue3::Params p(ue3::runtime::Get(), gGetVector.Get());
        if (gGetVector.Get() == nullptr || !p.Valid() || !p.Set("ParameterName", param.name)) {
            return "?";
        }
        ue3::runtime::Call(material, gGetVector.Get(), p);
        if (!p.ReturnBool().value_or(false)) {
            return "(default)";
        }
        Color const c = p.Get<Color>("OutValue").value_or(Color{});
        return std::format("({:.3f}, {:.3f}, {:.3f}, {:.3f})", c.r, c.g, c.b, c.a);
    }
    ue3::Params p(ue3::runtime::Get(), gGetScalar.Get());
    if (gGetScalar.Get() == nullptr || !p.Valid() || !p.Set("ParameterName", param.name)) {
        return "?";
    }
    ue3::runtime::Call(material, gGetScalar.Get(), p);
    if (!p.ReturnBool().value_or(false)) {
        return "(default)";
    }
    return std::format("{:.3f}", p.Get<float>("OutValue").value_or(0.0f));
}

} // namespace

std::string Describe(ue3::Object* component) {
    if (!ue3::runtime::Up() || component == nullptr) {
        return "no hair component";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const material = MaterialOf(component);
    std::vector<ue3::Object*> const chain = Chain(material);
    if (chain.empty()) {
        return "the hair has no material";
    }
    std::string out = "material chain:";
    for (ue3::Object* m : chain) {
        out += std::format("\n  {}{}", r.PathName(m), gInstances.contains(component) && gInstances[component] == m ? " (our instance)" : "");
    }
    std::vector<Param> const& params = ParamsOf(chain.back());
    out += std::format("\nparameters of {} ({}):", r.NameText(chain.back()), params.size());
    for (Param const& p : params) {
        std::string const def = p.vector ? std::format("({:.3f}, {:.3f}, {:.3f}, {:.3f})", p.vectorDefault.r, p.vectorDefault.g,
                                                       p.vectorDefault.b, p.vectorDefault.a)
                                         : std::format("{:.3f}", p.scalarDefault);
        out += std::format("\n  {} {}: default {}, now {}", p.vector ? "vector" : "scalar", p.text, def, ValueOf(material, p));
    }
    return out;
}

std::string Set(ue3::Object* component, std::string const& name, std::vector<double> const& values) {
    if (!ue3::runtime::Up() || component == nullptr) {
        return "no hair component";
    }
    Param const* const param = FindParam(component, name);
    if (param == nullptr) {
        return "the hair's master material has no parameter " + name + " (mevr hair material lists them)";
    }
    ue3::Object* const instance = OwnInstance(component);
    if (instance == nullptr) {
        return "couldn't give the hair its own material instance";
    }
    if (!SetOn(instance, *param, values)) {
        return std::format("setting {} failed ({} wants {})", param->text, param->vector ? "a vector" : "a scalar", param->vector ? "r g b [a]" : "one value");
    }
    gApplied.erase(component);  // a by-hand change: the look is applied again only if it changes
    return std::format("{} = {} on {:#x}'s own instance", param->text, ValueOf(instance, *param), reinterpret_cast<std::uintptr_t>(component));
}

bool Apply(ue3::Object* component, std::string const& look) {
    if (look.empty() || component == nullptr || !ue3::runtime::Up()) {
        return true;
    }
    auto it = gApplied.find(component);
    if (it != gApplied.end() && it->second == look && gInstances.contains(component) && MaterialOf(component) == gInstances[component]) {
        return true;
    }
    ue3::Object* const instance = OwnInstance(component);
    if (instance == nullptr) {
        return false;
    }
    bool ok = true;
    std::stringstream entries(look);
    std::string entry;
    int set = 0;
    while (std::getline(entries, entry, ';')) {
        auto const eq = entry.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string name = entry.substr(0, eq);
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c) != 0; }), name.end());
        std::vector<double> values;
        std::stringstream parts(entry.substr(eq + 1));
        std::string part;
        while (std::getline(parts, part, ',')) {
            if (auto v = ini::Number(part)) {
                values.push_back(*v);
            }
        }
        Param const* const param = FindParam(component, name);
        if (param == nullptr || !SetOn(instance, *param, values)) {
            ok = false;
            if (gWarned.insert(name).second) {
                log::Warn("hair look: {} isn't a parameter of this hair's material, or its value is the wrong shape", name);
            }
            continue;
        }
        ++set;
    }
    gApplied[component] = look;
    log::Info("hair look: {} parameter(s) set on {:#x}: {}", set, reinterpret_cast<std::uintptr_t>(component), look);
    return ok;
}

void PutBack(ue3::Object* component) {
    auto it = gInstances.find(component);
    if (it == gInstances.end()) {
        return;
    }
    if (ue3::runtime::IsAlive(component) && MaterialOf(component) == it->second && gSetMaterial.Get() != nullptr) {
        ue3::Params p(ue3::runtime::Get(), gSetMaterial.Get());
        if (p.Valid() && p.Set<std::int32_t>("ElementIndex", 0) && p.Set<ue3::Object*>("Material", nullptr)) {
            ue3::runtime::Call(component, gSetMaterial.Get(), p);  // none: the mesh's own material again
        }
    }
    gInstances.erase(it);
    gApplied.erase(component);
}

void Forget() {
    gParams.clear();
    gInstances.clear();
    gApplied.clear();
}

} // namespace mevr::game::hair_look
