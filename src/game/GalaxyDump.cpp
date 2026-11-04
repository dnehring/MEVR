// SPDX-License-Identifier: MIT
// `mevr galaxy dump` (game/GalaxyDump.hpp).

#include "game/GalaxyDump.hpp"

#include "core/Ini.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/Log.hpp"
#include "game/Player.hpp"
#include "engine/ProcessEvent.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Reflect.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::galaxy_dump {

namespace {

namespace mem = ue3::memory;

std::string Esc(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char const c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

std::string Num(float v) { return std::isfinite(v) ? std::format("{:.7g}", v) : std::string("null"); }

template <typename T>
T ReadAt(std::uint8_t const* p) {
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

// The newest live instance (the highest object index: an idle one from an earlier opening may linger).
ue3::Object* FindLive(ue3::Reflection& r, std::string_view classPath) {
    ue3::Class* const cls = r.FindClass(classPath);
    if (cls == nullptr) {
        return nullptr;
    }
    ue3::Object* found = nullptr;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsA(o, cls) && r.IsAlive(o) && !r.NameText(o).starts_with("Default__")) {
            found = o;
        }
    }
    return found;
}

// Calls `fn` on `target` with parameters set by `set`; `get` reads the results. False if the function or the call failed.
bool CallOn(ue3::Object* target, std::string_view fn, std::function<void(ue3::Params&)> const& set,
            std::function<void(ue3::Params&)> const& get) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const f = r.FindFunction(r.ClassOf(target), fn);
    if (f == nullptr) {
        return false;
    }
    ue3::Params parms(r, f);
    if (!parms.Valid()) {
        return false;
    }
    if (set) {
        set(parms);
    }
    if (!ue3::runtime::Call(target, f, parms)) {
        return false;
    }
    if (get) {
        get(parms);
    }
    return true;
}

// An FString the engine handed back: its text, then freed.
std::string TakeString(ue3::FString string) {
    std::string const text = string.count > 0 && string.count < 65536 ? mem::Narrow(string) : std::string();
    mem::FreeString(string);
    return text;
}

class Dumper {
public:
    explicit Dumper(ue3::Object* game) : r_(ue3::runtime::Get()), game_(game) {}

    // A string reference's text (cached).
    std::string Text(std::int32_t ref) {
        if (game_ == nullptr || ref <= 0) {
            return {};
        }
        std::string text;
        CallOn(game_, "GetSimpleString",
            [&](ue3::Params& p) {
                p.Set("StrRef", ref);
                p.SetBool("bParse", false);
            },
            [&](ue3::Params& p) { text = TakeString(p.Return<ue3::FString>().value_or(ue3::FString{})); });
        ++texts_;
        return text;
    }

    std::string ObjectRef(ue3::Object* o) {
        if (o == nullptr) {
            return "null";
        }
        return ue3::runtime::IsAlive(o) ? Esc(r_.FullName(o)) : Esc(std::format("<gone 0x{:X}>", reinterpret_cast<std::uintptr_t>(o)));
    }

    std::string Value(ue3::PropertyInfo const& info, std::uint8_t const* p, int depth) {
        switch (info.kind) {
        case ue3::Kind::Bool: return (ReadAt<std::uint32_t>(p) & info.boolMask) != 0 ? "true" : "false";
        case ue3::Kind::Byte: return std::to_string(ReadAt<std::uint8_t>(p));
        case ue3::Kind::Int: return std::to_string(ReadAt<std::int32_t>(p));
        case ue3::Kind::StringRef: {
            std::int32_t const ref = ReadAt<std::int32_t>(p);
            return std::format("{{\"ref\": {}, \"text\": {}}}", ref, Esc(Text(ref)));
        }
        case ue3::Kind::Float: return Num(ReadAt<float>(p));
        case ue3::Kind::Name: return Esc(r_.ToString(ReadAt<ue3::Name>(p)));
        case ue3::Kind::Str: {
            ue3::FString const s = ReadAt<ue3::FString>(p);
            return Esc(s.count > 0 && s.count < 65536 ? mem::Narrow(s) : std::string());
        }
        case ue3::Kind::Object:
        case ue3::Kind::Class:
        case ue3::Kind::Component:
        case ue3::Kind::Interface: return ObjectRef(ReadAt<ue3::Object*>(p));
        case ue3::Kind::Struct: {
            ue3::Struct* const s = r_.StructOf(info);
            std::string const name = s != nullptr ? r_.NameText(s) : std::string();
            if (name == "Vector") {
                auto const v = ReadAt<ue3::Vector>(p);
                return std::format("[{}, {}, {}]", Num(v.x), Num(v.y), Num(v.z));
            }
            if (name == "Rotator") {
                auto const v = ReadAt<ue3::Rotator>(p);
                return std::format("[{}, {}, {}]", v.pitch, v.yaw, v.roll);
            }
            if (name == "Vector2D") {
                auto const v = ReadAt<ue3::Vector2D>(p);
                return std::format("[{}, {}]", Num(v.x), Num(v.y));
            }
            if (s != nullptr && depth < 3) {
                return Fields(s, nullptr, p, depth + 1);
            }
            return Esc(std::format("<struct {}>", name));
        }
        case ue3::Kind::Array: {
            auto const header = ReadAt<ue3::TArray<std::uint8_t>>(p);
            auto const inner = r_.Describe(reinterpret_cast<ue3::Property*>(info.typed));
            if (!inner || header.count < 0 || header.count > 100000) {
                return Esc(std::format("<array of {}>", header.count));
            }
            std::string out = "[";
            int const shown = std::min(header.count, 200);
            for (int i = 0; i < shown; ++i) {
                out += (i == 0 ? "" : ", ") + Value(*inner, header.data + static_cast<size_t>(i) * inner->elementSize, depth + 1);
            }
            if (shown < header.count) {
                out += std::format(", \"... {} in all\"", header.count);
            }
            return out + "]";
        }
        default: return Esc(std::format("<{}>", ue3::KindName(info.kind)));
        }
    }

    // A struct's or class's properties as a JSON object, up the chain until (not including) `stopAt`.
    std::string Fields(ue3::Struct* type, ue3::Struct* stopAt, std::uint8_t const* base, int depth) {
        std::string out = "{";
        bool first = true;
        for (ue3::Struct* s = type; s != nullptr && s != stopAt; s = r_.SuperOf(s)) {
            for (ue3::Field* f = r_.ChildrenOf(s); f != nullptr; f = r_.NextOf(f)) {
                if (!r_.IsA(f, r_.PropertyClass())) {
                    continue;
                }
                auto const info = r_.Describe(reinterpret_cast<ue3::Property*>(f));
                if (!info) {
                    continue;
                }
                out += std::format("{}{}: {}", first ? "" : ", ", Esc(r_.NameText(f)), Value(*info, base + info->offset, depth));
                first = false;
            }
        }
        return out + "}";
    }

    // An actor (or any object): its name, class, place, and its class's own properties down to `stopAt`'s.
    std::string Object(ue3::Object* o, ue3::Struct* stopAt) {
        if (o == nullptr || !ue3::runtime::IsAlive(o)) {
            return ObjectRef(o);
        }
        std::string out = std::format("{{\"name\": {}, \"class\": {}", Esc(r_.FullName(o)), Esc(r_.NameText(r_.ClassOf(o))));
        if (auto const at = player::Location(o)) {
            out += std::format(", \"location\": [{}, {}, {}]", Num(at->x), Num(at->y), Num(at->z));
        }
        if (auto const rot = player::Rotation(o)) {
            out += std::format(", \"rotation\": [{}, {}, {}]", rot->pitch, rot->yaw, rot->roll);
        }
        for (char const* name : {"DrawScale", "DrawScale3D", "bHidden", "Tag"}) {
            if (auto const info = r_.PropertyOf(r_.ClassOf(o), name)) {
                out += std::format(", {}: {}", Esc(name), Value(*info, reinterpret_cast<std::uint8_t const*>(o) + info->offset, 0));
            }
        }
        out += ", \"props\": " + Fields(r_.ClassOf(o), stopAt, reinterpret_cast<std::uint8_t const*>(o), 0);
        return out + "}";
    }

    // A Bio2DA through its own getters: columns, rows and every cell with its type.
    std::string Table(ue3::Object* table, std::string* summary) {
        if (table == nullptr || !ue3::runtime::IsAlive(table)) {
            return "null";
        }
        std::int32_t rows = 0, columns = 0;
        CallOn(table, "GetNumRows", nullptr, [&](ue3::Params& p) { rows = p.Return<std::int32_t>().value_or(0); });
        CallOn(table, "GetNumColumns", nullptr, [&](ue3::Params& p) { columns = p.Return<std::int32_t>().value_or(0); });
        rows = std::clamp(rows, 0, 5000);
        columns = std::clamp(columns, 0, 200);
        std::vector<std::string> names;
        std::string out = std::format("{{\"name\": {}, \"rows\": {}, \"columns\": [", Esc(r_.FullName(table)), rows);
        for (std::int32_t c = 0; c < columns; ++c) {
            std::string name;
            CallOn(table, "GetColumnName", [&](ue3::Params& p) { p.Set("nColumn", c); },
                   [&](ue3::Params& p) { name = r_.ToString(p.Return<ue3::Name>().value_or(ue3::Name{})); });
            names.push_back(name);
            out += (c == 0 ? "" : ", ") + Esc(name);
        }
        out += "], \"data\": [";
        int cells = 0;
        for (std::int32_t row = 0; row < rows; ++row) {
            std::string rowName;
            std::int32_t rowNumber = -1;
            CallOn(table, "GetRowName", [&](ue3::Params& p) { p.Set("nRowIndex", row); },
                   [&](ue3::Params& p) { rowName = r_.ToString(p.Return<ue3::Name>().value_or(ue3::Name{})); });
            CallOn(table, "GetRowNumber", [&](ue3::Params& p) { p.Set("nRowIndex", row); },
                   [&](ue3::Params& p) { rowNumber = p.Return<std::int32_t>().value_or(-1); });
            out += std::format("{}{{\"row\": {}, \"label\": {}, \"cells\": {{", row == 0 ? "\n  " : ",\n  ", rowNumber, Esc(rowName));
            bool first = true;
            for (std::int32_t c = 0; c < columns; ++c) {
                // Every getter that accepts the cell: an int getter truncates a float (0.37 reads 0), so all of them.
                std::string cell;
                bool ok = false;
                auto const at = [&](ue3::Params& p) { p.Set("nRow", row); p.Set("nColumn", c); };
                CallOn(table, "GetFloatEntryII", at, [&](ue3::Params& p) {
                    if (p.ReturnBool().value_or(false)) {
                        cell += std::format("{}\"float\": {}", ok ? ", " : "", Num(p.Get<float>("fEntry").value_or(0.0f)));
                        ok = true;
                    }
                });
                CallOn(table, "GetIntEntryII", at, [&](ue3::Params& p) {
                    if (p.ReturnBool().value_or(false)) {
                        std::int32_t const v = p.Get<std::int32_t>("nEntry").value_or(0);
                        std::string const lower = ue3::Lower(names[static_cast<size_t>(c)]);
                        bool const textual = lower.find("name") != std::string::npos || lower.find("label") != std::string::npos ||
                                             lower.find("desc") != std::string::npos || lower.find("title") != std::string::npos;
                        cell += std::format("{}\"int\": {}", ok ? ", " : "", v);
                        if (textual && v > 0) {
                            cell += std::format(", \"text\": {}", Esc(Text(v)));
                        }
                        ok = true;
                    }
                });
                CallOn(table, "GetNameEntryII", at, [&](ue3::Params& p) {
                    if (p.ReturnBool().value_or(false)) {
                        cell += std::format("{}\"name\": {}", ok ? ", " : "", Esc(r_.ToString(p.Get<ue3::Name>("nEntry").value_or(ue3::Name{}))));
                        ok = true;
                    }
                });
                CallOn(table, "GetStringEntryII", at, [&](ue3::Params& p) {
                    std::string const text = TakeString(p.Get<ue3::FString>("sEntry").value_or(ue3::FString{}));
                    if (p.ReturnBool().value_or(false)) {
                        cell += std::format("{}\"string\": {}", ok ? ", " : "", Esc(text));
                        ok = true;
                    }
                });
                cell = "{" + cell + "}";
                if (ok) {
                    out += std::format("{}{}: {}", first ? "" : ", ", Esc(names[static_cast<size_t>(c)]), cell);
                    first = false;
                    ++cells;
                }
            }
            out += "}}";
        }
        out += "]}";
        if (summary != nullptr) {
            *summary += std::format(" {} {}x{} ({} cells);", r_.NameText(table), rows, columns, cells);
        }
        return out;
    }

    int Texts() const { return texts_; }

private:
    ue3::Reflection& r_;
    ue3::Object* game_ = nullptr;
    int texts_ = 0;
};

std::filesystem::path OutputFolder() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR" / L"galaxy";
}

// The game's window: this process's largest visible top-level window.
HWND GameWindow() {
    struct Search {
        DWORD process = 0;
        HWND best = nullptr;
        long area = 0;
    } search{GetCurrentProcessId(), nullptr, 0};
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            auto* const s = reinterpret_cast<Search*>(context);
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            RECT rc{};
            if (process == s->process && IsWindowVisible(window) && GetClientRect(window, &rc)) {
                long const area = (rc.right - rc.left) * (rc.bottom - rc.top);
                if (area > s->area) {
                    s->area = area;
                    s->best = window;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.best;
}

std::optional<std::pair<int, int>> GameWindowClient() {
    HWND const window = GameWindow();
    RECT rc{};
    if (window == nullptr || !GetClientRect(window, &rc)) {
        return std::nullopt;
    }
    return std::pair<int, int>{rc.right - rc.left, rc.bottom - rc.top};
}

ue3::Object* ReadObject(ue3::Reflection& r, ue3::Object* o, std::string_view field) {
    if (o == nullptr) {
        return nullptr;
    }
    auto const info = r.PropertyOf(r.ClassOf(o), field);
    if (!info) {
        return nullptr;
    }
    return ReadAt<ue3::Object*>(reinterpret_cast<std::uint8_t const*>(o) + info->offset);
}

bool gTrace = false;
int gTraced = 0;

// A traced call's parameters: bytes, ints, floats, strings and arrays of strings.
void OnFlashCall(ue3::Object* context, ue3::Function* function, void* parms, void* /*result*/) {
    if (!gTrace || context == nullptr || function == nullptr || parms == nullptr || ++gTraced > 2000) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const cls = r.NameText(r.ClassOf(context));
    std::string args;
    for (ue3::ParamSpec const& p : r.Parameters(function)) {
        auto const* at = static_cast<std::uint8_t const*>(parms) + p.offset;
        std::string value;
        switch (p.kind) {
        case ue3::Kind::Byte: value = std::to_string(ReadAt<std::uint8_t>(at)); break;
        case ue3::Kind::Int: value = std::to_string(ReadAt<std::int32_t>(at)); break;
        case ue3::Kind::Float: value = Num(ReadAt<float>(at)); break;
        case ue3::Kind::Str: {
            auto const s = ReadAt<ue3::FString>(at);
            value = Esc(s.count > 0 && s.count < 4096 ? mem::Narrow(s) : std::string());
            break;
        }
        case ue3::Kind::Array: {
            auto const a = ReadAt<ue3::TArray<ue3::FString>>(at);  // the handlers' arguments are arrays of strings
            value = "[";
            for (int i = 0; i < std::min(a.count, 24); ++i) {
                ue3::FString const& s = a.data[i];
                value += (i == 0 ? "" : ", ") + Esc(s.count > 0 && s.count < 4096 ? mem::Narrow(s) : std::string());
            }
            value += a.count > 24 ? std::format(", ... {} in all]", a.count) : "]";
            break;
        }
        default: value = std::format("<{}>", ue3::KindName(p.kind));
        }
        args += std::format("{}{}={}", args.empty() ? "" : ", ", p.name, value);
    }
    log::Info("galaxy trace: {}.{}({})", cls, r.NameText(function), args);
}

} // namespace

void TryStartup() {
    if (!ue3::runtime::Ready()) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    int added = 0;
    for (char const* path : {"Engine.BioSFHandler.HandleEvent", "Engine.BioSFHandler.HandleExternalEvent"}) {
        if (ue3::Function* const f = r.FindFunction(path)) {
            engine::process_event::AddPostHandler(f, &OnFlashCall);
            ++added;
        }
    }
    log::Info("galaxy trace: {} of 2 Flash handler functions watched (inert until `mevr galaxy trace on`)", added);
}

std::string Trace(bool on) {
    gTrace = on;
    gTraced = 0;
    return std::format("galaxy trace {}", on ? "on: the Flash handlers' calls go to the log as `galaxy trace:`" : "off");
}

std::string Input(int event, float value) {
    if (!ue3::runtime::Init()) {
        return "galaxy input: the reflection layer isn't ready";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const handler = FindLive(r, "SFXGame.BioSFHandler_GalaxyMap");
    if (handler == nullptr) {
        return "galaxy input: no live BioSFHandler_GalaxyMap (is the map open?)";
    }
    bool const called = CallOn(handler, "HandleInputEvent",
        [&](ue3::Params& p) {
            p.Set("Event", static_cast<std::uint8_t>(event));
            p.Set("fValue", value);
        },
        nullptr);
    return std::format("galaxy input: {}.HandleInputEvent({}, {}) called {}", r.FullName(handler), event, value, called);
}

namespace {
ue3::Object* MapPanel(ue3::Reflection& r) {
    return ReadObject(r, FindLive(r, "SFXGame.BioSFHandler_GalaxyMap"), "oPanel");
}
} // namespace

std::string Aim(int index) {
    if (!ue3::runtime::Init()) {
        return "galaxy aim: the reflection layer isn't ready";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const behavior = FindLive(r, "SFXGame.BioCameraBehaviorGalaxy");
    ue3::Object* const crosshair = ReadObject(r, behavior, "m_pCrossHairObject");
    auto const info = behavior != nullptr ? r.PropertyOf(r.ClassOf(behavior), "m_pScaleformObjects") : std::nullopt;
    if (crosshair == nullptr || !info) {
        return "galaxy aim: no map camera or cross-hair";
    }
    auto const array = ReadAt<ue3::TArray<ue3::Object*>>(reinterpret_cast<std::uint8_t const*>(behavior) + info->offset);
    if (index < 0 || index >= array.count || array.data[index] == nullptr || !ue3::runtime::IsAlive(array.data[index])) {
        return std::format("galaxy aim: no object {}", index);
    }
    auto const at = player::Location(array.data[index]);
    if (!at) {
        return "galaxy aim: the object has no place";
    }
    bool moved = false;
    CallOn(crosshair, "SetLocation", [&](ue3::Params& p) { p.Set("NewLocation", *at); },
        [&](ue3::Params& p) { moved = p.ReturnBool().value_or(false); });
    return std::format("galaxy aim: the cross-hair to {} at ({:.1f}, {:.1f}, {:.1f}): SetLocation returned {}", r.NameText(array.data[index]),
                       at->x, at->y, at->z, moved);
}

std::string Invoke(std::string const& method) {
    if (!ue3::runtime::Init()) {
        return "galaxy invoke: the reflection layer isn't ready";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const panel = MapPanel(r);
    if (panel == nullptr) {
        return "galaxy invoke: no map panel";
    }
    mem::OwnedString name(mem::Widen(method));
    std::string result;
    bool const called = CallOn(panel, "InvokeMethodReturn", [&](ue3::Params& p) { p.Set("sMethodName", name.Get()); },
        [&](ue3::Params& p) { result = TakeString(p.Return<ue3::FString>().value_or(ue3::FString{})); });
    return std::format("galaxy invoke: {}() called {}, returned '{}'", method, called, result);
}

std::string Variable(std::string const& path) {
    if (!ue3::runtime::Init()) {
        return "galaxy var: the reflection layer isn't ready";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const panel = MapPanel(r);
    if (panel == nullptr) {
        return "galaxy var: no map panel";
    }
    mem::OwnedString name(mem::Widen(path));
    bool available = false;
    CallOn(panel, "IsAvailable", [&](ue3::Params& p) { p.Set("sPath", name.Get()); },
        [&](ue3::Params& p) { available = p.ReturnBool().value_or(false); });
    std::string value;
    CallOn(panel, "GetVariableString", [&](ue3::Params& p) { p.Set("sPath", name.Get()); },
        [&](ue3::Params& p) { value = TakeString(p.Return<ue3::FString>().value_or(ue3::FString{})); });
    return std::format("galaxy var: {} available {}, '{}'", path, available, value);
}

std::string Movie() {
    if (!ue3::runtime::Init()) {
        return "galaxy movie: the reflection layer isn't ready";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const handler = FindLive(r, "SFXGame.BioSFHandler_GalaxyMap");
    ue3::Object* const panel = ReadObject(r, handler, "oPanel");
    ue3::Object* const instance = ReadObject(r, panel, "MovieRef");
    ue3::Object* movie = ReadObject(r, instance, "MovieInfo");  // the GFxMovie's GFxMovieInfo holds the SWF
    if (movie == nullptr) {  // not linked at run time: the package's own asset
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n && movie == nullptr; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o != nullptr && r.IsAlive(o) && r.FullName(o) == "GFxMovieInfo GUI_SF_GalaxyMap.GalaxyMap") {
                movie = o;
            }
        }
    }
    if (movie == nullptr || !ue3::runtime::IsAlive(movie)) {
        return std::format("galaxy movie: no movie (handler {}, panel {})", ue3::runtime::FullName(handler), ue3::runtime::FullName(panel));
    }
    auto const info = r.PropertyOf(r.ClassOf(movie), "RawData");
    if (!info) {
        return "galaxy movie: " + r.FullName(movie) + " has no RawData";
    }
    auto const bytes = ReadAt<ue3::TArray<std::uint8_t>>(reinterpret_cast<std::uint8_t const*>(movie) + info->offset);
    if (bytes.data == nullptr || bytes.count <= 0 || bytes.count > 64 * 1024 * 1024) {
        return std::format("galaxy movie: {}'s RawData is empty ({} bytes)", r.FullName(movie), bytes.count);
    }
    std::filesystem::path const folder = OutputFolder();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::filesystem::path const path = folder / L"movie.gfx";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<char const*>(bytes.data), bytes.count);
    out.close();
    return std::format("galaxy movie: wrote {} bytes of {} to {} (starts '{}{}{}')", bytes.count, r.FullName(movie), ini::Utf8(path),
                       static_cast<char>(bytes.data[0]), static_cast<char>(bytes.data[1]), static_cast<char>(bytes.data[2]));
}

std::string Click(float u, float v) {
    HWND const window = GameWindow();
    RECT rc{};
    if (window == nullptr || !GetClientRect(window, &rc) || !(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)) {
        return "galaxy click: no game window, or (u, v) outside 0..1";
    }
    int const x = static_cast<int>(u * static_cast<float>(rc.right - rc.left));
    int const y = static_cast<int>(v * static_cast<float>(rc.bottom - rc.top));
    POINT screen{x, y};
    ClientToScreen(window, &screen);
    BOOL const moved = SetCursorPos(screen.x, screen.y);
    LPARAM const at = MAKELPARAM(x, y);
    PostMessageW(window, WM_MOUSEMOVE, 0, at);
    PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessageW(window, WM_LBUTTONUP, 0, at);
    std::string const line = std::format("galaxy click: ({:.3f}, {:.3f}) = client pixel ({}, {}) of {}x{}, screen ({}, {}); SetCursorPos {}",
                                         u, v, x, y, rc.right - rc.left, rc.bottom - rc.top, screen.x, screen.y, moved ? "ok" : "failed");
    log::Info("{}", line);
    return line;
}

std::string Pick(int index, bool click) {
    if (!ue3::runtime::Init()) {
        return "galaxy pick: the reflection layer isn't ready";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const controller = player::Controller();
    ue3::Object* const behavior = FindLive(r, "SFXGame.BioCameraBehaviorGalaxy");
    auto const pov = controller != nullptr ? player::CameraPov(player::Camera(controller)) : std::nullopt;
    auto const client = GameWindowClient();
    if (behavior == nullptr || !pov || pov->location == nullptr || pov->rotation == nullptr || pov->fov == nullptr || !client) {
        return "galaxy pick: no map camera, player camera or game window";
    }
    constexpr float kUnitToRad = 6.2831853f / 65536.0f;
    vr::galaxy_holo::GameCamera camera;
    camera.location[0] = pov->location->x;
    camera.location[1] = pov->location->y;
    camera.location[2] = pov->location->z;
    camera.pitch = static_cast<float>(pov->rotation->pitch) * kUnitToRad;
    camera.yaw = static_cast<float>(pov->rotation->yaw) * kUnitToRad;
    camera.roll = static_cast<float>(pov->rotation->roll) * kUnitToRad;
    camera.fovXDeg = *pov->fov;
    camera.aspect = static_cast<float>(client->first) / static_cast<float>(std::max(1, client->second));
    camera.valid = 1;
    auto const info = r.PropertyOf(r.ClassOf(behavior), "m_pScaleformObjects");
    if (!info) {
        return "galaxy pick: no m_pScaleformObjects";
    }
    auto const array = ReadAt<ue3::TArray<ue3::Object*>>(reinterpret_cast<std::uint8_t const*>(behavior) + info->offset);
    std::string out = std::format("galaxy pick: camera ({:.0f}, {:.0f}, {:.0f}) pitch {:.1f} yaw {:.1f} fov {:.1f}, window {}x{}", camera.location[0],
        camera.location[1], camera.location[2], camera.pitch / kUnitToRad * 360.0f / 65536.0f, camera.yaw / kUnitToRad * 360.0f / 65536.0f,
        camera.fovXDeg, client->first, client->second);
    for (char const* field : {"m_pCrossHairObject", "m_pSelectedObject", "m_pCenterObject"}) {
        ue3::Object* const o = ReadObject(r, behavior, field);
        auto const at = o != nullptr && ue3::runtime::IsAlive(o) ? player::Location(o) : std::nullopt;
        if (at) {
            float const p[3] = {at->x, at->y, at->z};
            auto const uv = vr::galaxy_holo::ToGameScreen(camera, p);
            out += std::format("\n  {}: {} at ({:.1f}, {:.1f}, {:.1f}){}", field, r.NameText(o), at->x, at->y, at->z,
                uv ? std::format(", screen ({:.3f}, {:.3f})", (*uv)[0], (*uv)[1]) : std::string());
        } else {
            out += std::format("\n  {}: none", field);
        }
    }
    {
        // The camera behaviour's virtual Tick (slot 0x2D8, what execTick calls) and the handler's, as exe offsets, for Ghidra.
        auto const module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto const slot = [&](ue3::Object* o, std::size_t offset) {
            if (o == nullptr) {
                return std::uintptr_t{0};
            }
            auto const* const vtable = *reinterpret_cast<std::uintptr_t const* const*>(o);
            return vtable[offset / sizeof(std::uintptr_t)] - module;
        };
        out += std::format("\n  behaviour Tick (vtable +0x2D8) at exe+0x{:X}, vtable at exe+0x{:X}", slot(behavior, 0x2D8),
            *reinterpret_cast<std::uintptr_t const*>(behavior) - module);
    }
    if (auto const state = r.PropertyOf(r.ClassOf(behavior), "m_nCurrentState")) {
        out += std::format("\n  state {}", ReadAt<std::int32_t>(reinterpret_cast<std::uint8_t const*>(behavior) + state->offset));
    }
    for (int i = 0; i < array.count; ++i) {
        if (index >= 0 && i != index) {
            continue;
        }
        ue3::Object* const o = array.data[i];
        auto const at = o != nullptr && ue3::runtime::IsAlive(o) ? player::Location(o) : std::nullopt;
        if (!at) {
            out += std::format("\n  [{}] gone", i);
            continue;
        }
        float const p[3] = {at->x, at->y, at->z};
        auto const uv = vr::galaxy_holo::ToGameScreen(camera, p);
        out += std::format("\n  [{}] {} at ({:.0f}, {:.0f}, {:.0f}): {}", i, r.NameText(o), at->x, at->y, at->z,
            uv ? std::format("screen ({:.3f}, {:.3f})", (*uv)[0], (*uv)[1]) : std::string("behind the camera"));
        if (click && i == index && uv) {
            out += "\n  " + Click((*uv)[0], (*uv)[1]);
        }
    }
    return out;
}

std::string Dump(std::string const& tag) {
    if (!ue3::runtime::Init()) {
        return "galaxy dump: the reflection layer isn't ready";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const controller = player::Controller();
    ue3::Object* const behavior = FindLive(r, "SFXGame.BioCameraBehaviorGalaxy");
    if (behavior == nullptr) {
        return "galaxy dump: no live BioCameraBehaviorGalaxy (is the galaxy map open?)";
    }
    ue3::Object* const worldInfo = ReadObject(r, controller, "WorldInfo");
    ue3::Object* const game = ReadObject(r, worldInfo, "Game");
    Dumper d(game);
    auto const started = std::chrono::steady_clock::now();
    ue3::Struct* const actorClass = r.FindClass("Engine.Actor");
    ue3::Struct* const behaviorStop = r.FindClass("SFXGame.BioCameraBehavior");

    std::string json = "{\n";
    json += std::format("\"tag\": {},\n", Esc(tag));
    json += std::format("\"gameMode\": {},\n", controller != nullptr ? static_cast<int>(player::GameMode(controller).value_or(255)) : -1);
    if (controller != nullptr) {
        if (auto const pov = player::CameraPov(player::Camera(controller)); pov && pov->location && pov->rotation && pov->fov) {
            json += std::format("\"playerCamera\": {{\"location\": [{}, {}, {}], \"rotation\": [{}, {}, {}], \"fov\": {}}},\n",
                Num(pov->location->x), Num(pov->location->y), Num(pov->location->z), pov->rotation->pitch, pov->rotation->yaw,
                pov->rotation->roll, Num(*pov->fov));
        }
    }
    if (auto const client = GameWindowClient()) {
        json += std::format("\"windowClient\": [{}, {}],\n", client->first, client->second);
    }
    // The behaviour's own fields (its lists of actors by name), then each listed actor in full.
    json += "\"behavior\": " + d.Object(behavior, behaviorStop) + ",\n";
    std::string summary;
    for (char const* list : {"m_pScaleformObjects", "m_pDynamicClusterPath", "m_pDynamicObjects"}) {
        auto const info = r.PropertyOf(r.ClassOf(behavior), list);
        json += std::format("{}: [", Esc(list));
        if (info) {
            auto const array = ReadAt<ue3::TArray<ue3::Object*>>(reinterpret_cast<std::uint8_t const*>(behavior) + info->offset);
            for (int i = 0; i < std::min(array.count, 400); ++i) {
                json += (i == 0 ? "\n  " : ",\n  ") + d.Object(array.data[i], actorClass);
            }
            summary += std::format(" {} {};", list, array.count);
        }
        json += "],\n";
    }
    for (char const* one : {"m_pCenterObject", "m_pCrossHairObject", "m_pCameraObject", "m_pSelectedObject", "m_pSelectedCluster",
                            "m_pSelectedSystem", "m_pSelectedPlanet", "m_pTemplatePlanet"}) {
        json += std::format("{}: {},\n", Esc(one), d.Object(ReadObject(r, behavior, one), actorClass));
    }
    // The Flash handler.
    if (ue3::Object* const handler = FindLive(r, "SFXGame.BioSFHandler_GalaxyMap")) {
        json += "\"handler\": " + d.Object(handler, r.FindClass("SFXGame.BioSFHandler")) + ",\n";
    }
    // The tables.
    json += "\"tables\": {";
    bool first = true;
    for (char const* table : {"m_pClusterTable", "m_pSystemTable", "m_pPlanetTable", "m_pMapTable", "m_pPlotPlanetTable", "m_pRelayTable"}) {
        json += std::format("{}\n{}: {}", first ? "" : ",", Esc(table), d.Table(ReadObject(r, behavior, table), &summary));
        first = false;
    }
    json += "\n}\n}\n";

    std::filesystem::path const folder = OutputFolder();
    if (folder.empty()) {
        return "galaxy dump: no %LOCALAPPDATA%";
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::time_t const now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32]{};
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    std::filesystem::path const path = folder / std::format("{}-{}.json", stamp, tag.empty() ? "map" : tag);
    std::ofstream out(path, std::ios::binary);
    out << json;
    out.close();
    double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::string const line = std::format("galaxy dump: wrote {} ({} KB, {:.0f} ms, {} texts looked up):{}", ini::Utf8(path), json.size() / 1024, ms,
                                         d.Texts(), summary);
    log::Info("{}", line);
    return line;
}

} // namespace mevr::game::galaxy_dump
