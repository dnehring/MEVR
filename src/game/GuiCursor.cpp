// SPDX-License-Identifier: MIT
// Where the game's Flash cursor is (game/GuiCursor.hpp, research/57 section 6).
#include "game/GuiCursor.hpp"

#include "core/Log.hpp"
#include "core/vr/GuiEvents.hpp"
#include "game/GameMenuRequest.hpp"
#include "game/Player.hpp"
#if defined(MEVR_GAME_LE3)
#include "game/Le3Gui.hpp"
#endif
#include "game/WorldUi.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <deque>
#include <format>
#include <optional>
#include <sstream>
#include <string>

namespace mevr::game::gui_cursor {
namespace {

namespace mem = ue3::memory;

ue3::FieldRef gScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager", ue3::kEveryGame};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel", ue3::kEveryGame};

std::deque<int> gKeys;  // mevr guikey's events still to send, one per tick (-1: a tick without one)
vr::gui_events::Scheduler gScheduler;  // the game pointer's events (HostServices v30)
std::uint64_t gTaken = 0;
std::uint64_t gSent = 0;
std::uint64_t gFailed = 0;
std::uint64_t gDroppedForPuzzle = 0;  // the pointer's events while the bypass puzzle was played
bool gLoggedFirst = false;
bool gLoggedFailure = false;
bool gOwnerDrain = false;
#if defined(MEVR_GAME_LE3)
ue3::Object* gInputMovie = nullptr;
#endif

std::uint8_t NativeEvent(std::uint8_t event) {
#if defined(MEVR_GAME_LE3)
    return vr::gui_events::Le3NativeEvent(event);
#elif defined(MEVR_GAME_LE2)
    return vr::gui_events::Le2NativeEvent(event);
#else
    return event;
#endif
}

ue3::Object* FocusPanel() {
#if defined(MEVR_GAME_LE3)
    return le3_gui::FocusMovie();
#else
    ue3::Object* const pc = player::Controller();
    ue3::Object* const manager = pc != nullptr ? gScaleFormManager.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    return manager != nullptr ? gFocusPanel.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
#endif
}

std::optional<float> VariableFloat(ue3::Object* panel, wchar_t const* path) {
    if (panel == nullptr) {
        return std::nullopt;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const f = r.FindFunction(r.ClassOf(panel), "GetVariableFloat");
    if (f == nullptr) {
        return std::nullopt;
    }
    ue3::Params p(r, f);
    mem::OwnedString name(path);
    if (!p.Valid() || !name.Valid()) {
        return std::nullopt;
    }
    p.Set("sPath", name.Get());
    if (!ue3::runtime::Call(panel, f, p)) {
        return std::nullopt;
    }
    return p.Return<float>();
}

} // namespace

std::string Command(std::string const& args) {
#if defined(MEVR_GAME_LE3)
    std::istringstream words(args);
    std::string option;
    words >> option;
    if (option == "hud") {
        std::string detail;
        words >> detail;
        return le3_gui::HudReport(detail == "parents");
    }
#else
    (void)args;
#endif
    ue3::Object* const panel = FocusPanel();
    if (panel == nullptr) {
        return "guimouse: no focused movie (open a screen)";
    }
    auto const x = VariableFloat(panel, L"_root._xmouse");
    auto const y = VariableFloat(panel, L"_root._ymouse");
    auto const w = VariableFloat(panel, L"Stage.width");
    auto const h = VariableFloat(panel, L"Stage.height");
    std::string out = std::format("guimouse: the movie's mouse ({:.1f}, {:.1f}) of a {:.0f} x {:.0f} stage", x.value_or(-1.0f),
        y.value_or(-1.0f), w.value_or(0.0f), h.value_or(0.0f));
#if defined(MEVR_GAME_LE3)
    out += std::format("; focused ME3 movie '{}' (native full-frame panel, crop not accepted)", le3_gui::Tag(panel));
#endif
    // The Windows cursor as the game sees it (GetCursorPos, answered by the host's pointer while it steers), in the foreground
    // window's client area, and the movie's mouse that would make.
    HWND const front = GetForegroundWindow();
    DWORD pid = 0;
    POINT cursor{};
    RECT client{};
    if (front != nullptr && GetWindowThreadProcessId(front, &pid) != 0 && pid == GetCurrentProcessId() && GetCursorPos(&cursor) &&
        ScreenToClient(front, &cursor) && GetClientRect(front, &client) && client.right > 0 && w && h) {
        float const scale = *w / static_cast<float>(client.right);
        out += std::format("; the cursor at ({}, {}) of the {}x{} client area would read ({:.1f}, {:.1f})", cursor.x, cursor.y, client.right,
            client.bottom, static_cast<float>(cursor.x) * scale, static_cast<float>(cursor.y) * scale - (*h - 720.0f) * 0.5f);
    } else {
        out += "; the game's window isn't in front";
    }
    return out;
}

std::string KeyCommand(std::string const& args) {
    std::istringstream words(args);
    std::string word;
    int count = 0;
    while (words >> word) {
        if (word == "w") {
            gKeys.push_back(-1);
            continue;
        }
        int event = -1;
        try {
            event = std::stoi(word);
        } catch (...) {
            return "usage: mevr guikey <event|w> ... (e.g. 6 w 50 w w w 52)";
        }
        if (event < 0 || event > 52) {
            return std::format("guikey: {} isn't a BIOGUI_EVENT (0..52)", event);
        }
        gKeys.push_back(event);
        ++count;
    }
    return std::format("guikey: {} events queued over {} ticks; the log says each (`gui cursor: GuiKey`)", count, gKeys.size());
}

void Tick(std::uint32_t (*take)(std::uint8_t* events, std::uint32_t capacity), bool otherUiOwns) {
    if (otherUiOwns) {
        gScheduler.Clear();
        gKeys.clear();
        gOwnerDrain = true;
        // Do not call the host's take here: it advertises a live native consumer. Only releases
        // of already-delivered presses may reach the native screen while another UI owns input.
        auto const releases = gScheduler.ReleaseSent();
        if (game_menu_request::GuiInputReady()) {
            std::string why;
            for (auto event : releases) (void)game_menu_request::GuiKey(NativeEvent(event), why);
        }
        return;
    }
    if (gOwnerDrain) {
        gScheduler.Clear();
        if (take != nullptr && game_menu_request::GuiInputReady()) {
            std::uint8_t discard[vr::gui_events::Queue::kCapacity] = {};
            gTaken += take(discard, static_cast<std::uint32_t>(vr::gui_events::Queue::kCapacity));
            gOwnerDrain = false;
        } else if (take == nullptr) {
            gOwnerDrain = false;
        }
        return; // no old press, wheel or console command is replayed on resume
    }
#if defined(MEVR_GAME_LE3)
    // Polling TakeGuiEvents claims native input ownership in the host. Keep Win32 fallback
    // until the real consumer exists, and never replay a previous screen's queued click.
    auto* const movie = le3_gui::FocusMovie();
    bool const changedMovie = movie != gInputMovie;
    bool const ready = movie && game_menu_request::GuiInputReady();
    if (!ready || changedMovie) {
        gScheduler.Clear();
        (void)gScheduler.ReleaseSent(); // an old movie's held state must not belong to its replacement
    }
    gInputMovie = movie;
    if (take != nullptr && ready) {
#else
    if (take != nullptr) {
#endif
        std::uint8_t events[vr::gui_events::Queue::kCapacity] = {};
        std::uint32_t const n = take(events, static_cast<std::uint32_t>(vr::gui_events::Queue::kCapacity));
#if defined(MEVR_GAME_LE3)
        // A new movie must not inherit the previous movie's unfinished click/scroll sequence.
        if (!changedMovie) gScheduler.Add(events, n);
#else
        gScheduler.Add(events, n);
#endif
        gTaken += n;
    }
    bool const puzzle = world_ui::PuzzlePlaying();
    for (std::uint8_t const event : gScheduler.Tick()) {
        if (puzzle && !vr::gui_events::IsControl(event)) {
            // The bypass puzzle takes only the d-pad: a mouse motion turns its marker without end (research/53 section 6.3).
            ++gDroppedForPuzzle;
            continue;
        }
        std::string why;
        std::uint8_t const nativeEvent = NativeEvent(event);
        if (game_menu_request::GuiKey(nativeEvent, why)) {
            gScheduler.NoteSent(event);
            ++gSent;
            if (!gLoggedFirst && event != vr::gui_events::kMouseMove) {
                gLoggedFirst = true;
                log::Info("gui cursor: the game pointer's input goes to the game's screens as their own GUI events ({}; research/57 "
                          "section 6)", why);
            }
        } else {
            ++gFailed;
            if (!gLoggedFailure) {
                gLoggedFailure = true;
                log::Warn("gui cursor: GuiKey {} not sent: {}", nativeEvent, why);
            }
        }
    }
    if (gKeys.empty()) {
        return;
    }
    int const event = gKeys.front();
    gKeys.pop_front();
    if (event < 0) {
        return;
    }
    std::string why;
    bool const ok = game_menu_request::GuiKey(static_cast<std::uint8_t>(event), why);
    log::Info("gui cursor: GuiKey {} {}: {}", event, ok ? "sent" : "not sent", why);
}

std::string StatusJson() {
    return std::format("{{\"taken\": {}, \"sent\": {}, \"failed\": {}, \"waiting\": {}, \"droppedForPuzzle\": {}}}", gTaken, gSent, gFailed,
        gScheduler.Waiting(), gDroppedForPuzzle);
}

} // namespace mevr::game::gui_cursor
