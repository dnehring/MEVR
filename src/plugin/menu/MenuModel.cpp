// SPDX-License-Identifier: MIT
#include "plugin/menu/MenuModel.hpp"

#include "core/vr/MenuSettings.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Variant.h>

#include <cmath>

namespace mevr::menu {
namespace {

namespace ms = vr::menu_settings;

std::string const kEmpty;

} // namespace

float Values::Number(std::string const& key) const {
    auto const it = numbers.find(key);
    return it != numbers.end() ? it->second : 0.0f;
}

std::string const& Values::Text(std::string const& key) const {
    auto const it = texts.find(key);
    return it != texts.end() ? it->second : kEmpty;
}

bool Model::Bind(Rml::Context* context, Backend* backend) {
    if (context == nullptr) {
        return false;
    }
    backend_ = backend;
    Rml::DataModelConstructor constructor = context->CreateDataModel("menu");
    if (!constructor) {
        return false;
    }
    for (ms::Field const& field : ms::Schema()) {
        std::string const key = field.key;
        switch (field.type) {
        case ms::Type::Flag:
            values_.numbers[key] = field.number;
            constructor.BindFunc(
                key, [this, key](Rml::Variant& out) { out = values_.Flag(key); },
                [this, key](Rml::Variant const& in) {
                    values_.numbers[key] = in.Get<bool>() ? 1.0f : 0.0f;
                    Changed(key);
                    if (backend_ != nullptr && !ms::Find(key)->local) {
                        backend_->Write(key, values_, true);
                    }
                });
            break;
        case ms::Type::Number: {
            values_.numbers[key] = field.number;
            constructor.BindFunc(
                key, [this, key](Rml::Variant& out) { out = values_.Number(key); },
                [this, key](Rml::Variant const& in) {
                    ms::Field const* f = ms::Find(key);
                    float const value = ms::Clamp(*f, in.Get<float>(f->number));
                    if (!userInput_ || std::fabs(value - values_.Number(key)) < 1e-6f) {
                        // The slider echoing what it was given (snapped to its step and range when the document
                        // shows it): the setting keeps its own value.
                        return;
                    }
                    values_.numbers[key] = value;
                    Changed(key);
                    if (backend_ != nullptr && !f->local) {
                        backend_->Write(key, values_, false);
                    }
                });
            ms::Format const format = field.format;
            constructor.BindFunc(key + "_text", [this, key, format](Rml::Variant& out) {
                out = Rml::String(ms::FormatValue(format, values_.Number(key)));
            });
            break;
        }
        case ms::Type::Choice:
            // Assignable from the document too (data-event-click="section = 'display'").
            values_.texts[key] = field.text;
            constructor.BindFunc(
                key, [this, key](Rml::Variant& out) { out = Rml::String(values_.Text(key)); },
                [this, key](Rml::Variant const& in) {
                    std::string const value = in.Get<Rml::String>();
                    if (values_.Text(key) == value) {
                        return;
                    }
                    values_.texts[key] = value;
                    Changed(key);
                    if (backend_ != nullptr && !ms::Find(key)->local) {
                        backend_->Write(key, values_, true);
                    }
                });
            break;
        case ms::Type::Text:
            values_.texts[key] = field.text;
            constructor.BindFunc(key, [this, key](Rml::Variant& out) { out = Rml::String(values_.Text(key)); });
            break;
        }
    }
    constructor.BindEventCallback("toggle", [this](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& args) {
        if (args.empty()) {
            return;
        }
        std::string const key = args[0].Get<Rml::String>();
        ms::Field const* f = ms::Find(key);
        if (f == nullptr || f->type != ms::Type::Flag) {
            return;
        }
        values_.numbers[key] = values_.Flag(key) ? 0.0f : 1.0f;
        Changed(key);
        if (backend_ != nullptr && !f->local) {
            backend_->Write(key, values_, true);
        }
    });
    constructor.BindEventCallback("pick", [this](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& args) {
        if (args.size() < 2) {
            return;
        }
        std::string const key = args[0].Get<Rml::String>();
        std::string const value = args[1].Get<Rml::String>();
        ms::Field const* f = ms::Find(key);
        if (f == nullptr || f->type != ms::Type::Choice || values_.Text(key) == value) {
            return;
        }
        values_.texts[key] = value;
        Changed(key);
        if (backend_ != nullptr && !f->local) {
            backend_->Write(key, values_, true);
        }
    });
    constructor.BindEventCallback("cmd", [this](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& args) {
        if (args.empty() || backend_ == nullptr) {
            return;
        }
        std::string const name = args[0].Get<Rml::String>();
        std::string const argument = args.size() > 1 ? args[1].Get<Rml::String>() : std::string();
        backend_->Command(name, argument, values_);
        DirtyAll();
    });
    handle_ = constructor.GetModelHandle();
    Refresh();
    return true;
}

void Model::Refresh() {
    if (backend_ == nullptr) {
        return;
    }
    Values before = values_;
    backend_->Read(values_);
    for (auto const& [key, value] : values_.numbers) {
        auto const it = before.numbers.find(key);
        if (it == before.numbers.end() || it->second != value) {
            Changed(key);
        }
    }
    for (auto const& [key, value] : values_.texts) {
        auto const it = before.texts.find(key);
        if (it == before.texts.end() || it->second != value) {
            Changed(key);
        }
    }
}

void Model::SetText(std::string const& key, std::string const& value) {
    if (values_.Text(key) != value) {
        values_.texts[key] = value;
        Changed(key);
    }
}

void Model::SetNumber(std::string const& key, float value) {
    if (values_.Number(key) != value) {
        values_.numbers[key] = value;
        Changed(key);
    }
}

void Model::DirtyAll() {
    Refresh();
    if (handle_) {
        handle_.DirtyAllVariables();
    }
    changed_ = true;
}

bool Model::TakeChanged() {
    bool const changed = changed_;
    changed_ = false;
    return changed;
}

void Model::Changed(std::string const& key) {
    changed_ = true;
    if (!handle_) {
        return;
    }
    handle_.DirtyVariable(key);
    ms::Field const* f = ms::Find(key);
    if (f != nullptr && f->type == ms::Type::Number) {
        handle_.DirtyVariable(key + "_text");
    }
}

} // namespace mevr::menu
