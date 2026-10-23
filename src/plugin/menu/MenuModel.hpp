#pragma once

#include <RmlUi/Core/DataModelHandle.h>

#include <map>
#include <string>

namespace Rml {
class Context;
}

// The main menu's data model "menu" (research/28): every value of core/vr/MenuSettings' schema bound by name
// (BindFunc; a number's "<key>_text" too), and the events the document sends:
//   toggle('key')            flips a flag
//   pick('key', 'value')     picks a chip
//   cmd('name'[, 'arg'])     a button (Backend::Command)
// plus the sliders' two-way data-value bindings. What a value means is the backend's business: the host's maps
// the keys to the settings files, Halcyon's config, the colour and the calibration (plugin/MainMenuSettings);
// the preview's keeps them in memory.
namespace mevr::menu {

struct Values {
    std::map<std::string, float> numbers;      // flags (0/1) and numbers
    std::map<std::string, std::string> texts;  // choices and texts

    float Number(std::string const& key) const;
    bool Flag(std::string const& key) const { return Number(key) != 0.0f; }
    std::string const& Text(std::string const& key) const;
};

class Backend {
public:
    virtual ~Backend() = default;
    // The settings as they are now, into `values` (keys it doesn't know keep what they have).
    virtual void Read(Values& values) = 0;
    // The menu changed `key` (its new value already in `values`). `final`: a slider let go, a chip, a toggle.
    virtual void Write(std::string const& key, Values const& values, bool final) = 0;
    // A button.
    virtual void Command(std::string const& name, std::string const& argument, Values& values) = 0;
};

class Model {
public:
    // Creates the data model in `context` (before the document loads). False: RmlUi refused it.
    bool Bind(Rml::Context* context, Backend* backend);
    // Re-reads the backend; dirties what changed.
    void Refresh();
    // The menu's own values (the open section...), changed by the host.
    void SetText(std::string const& key, std::string const& value);
    void SetNumber(std::string const& key, float value);
    Values const& Get() const { return values_; }
    // Something changed outside a binding (a backend command): re-read and redraw.
    void DirtyAll();
    // A value changed since the last call (the host re-renders when it did).
    bool TakeChanged();
    // The host is feeding the player's input to the context (mouse, keys): only then does a control that changes
    // its own value (a slider dragged) write the setting. RmlUi's sliders also report the value they were given,
    // snapped to their step and range, when the document shows it: that must never change a setting.
    void SetUserInput(bool on) { userInput_ = on; }

private:
    void Changed(std::string const& key);
    Rml::DataModelHandle handle_;
    Backend* backend_ = nullptr;
    Values values_;
    bool changed_ = true;
    bool userInput_ = false;
};

} // namespace mevr::menu
