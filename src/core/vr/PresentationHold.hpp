// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

// Two holds on Halcyon's flat/VR presentation decision (xr_session.cpp's DecideCine and RunFrame, [MEVR] seams;
// research/56), so the game's start and its front end show one view instead of flipping between views:
// - FrontEnd: from start-up until the first load from the menus or the first gameplay (a mode 0-4 for kGameplayFrames),
//   every mode but gameplay is the front end's menu (the flat panel): the byte reads -1 before the mode manager exists (1.5-2
//   s in which Halcyon showed the title's 3D backdrop as plain VR filling the view), the logos' Movie mode (8) whose video
//   isn't always seen decoding (1.5 s of plain VR again), and the camera flight when a key is pressed at the title (6:
//   Halcyon latched it as a cutscene, the 3D cinema screen came up between the title's panel and the main menu's, and the
//   latch outlived the main menu into the load's movie). After the menus (7), a load's unreadable byte and its Movie mode
//   are the menu too. The game's own cinematics (a new game's opening, a save's) come after that load.
// - VrEntry: plain VR (the world in the eyes) after another presentation waits a moment with the previous one shown, so a
//   blip of a gameplay mode between two others (pressing a key at the title: the cinematic's 6, one frame of 0, the main
//   menu's 7) never puts the world in the view for a frame.
// Game-independent (unit-tested, tests/Main.cpp TestPresentationHold).
namespace mevr::vr::presentation {

// An admitted room GUI takes over from the outgoing cinematic even if the cine latch or
// movie detector still remembers it. A mode-7 blip without ownership remains cinematic.
inline constexpr bool WorldGuiOwnsView(int mode, bool owned, bool frontEnd) {
    return owned && !frontEnd && (mode == 7 || mode == 10);
}

class FrontEnd {
public:
    // Gameplay ends the start-up's front end once it lasts this many frames (the title's camera flight goes 6, 0 for a
    // frame or two, 7).
    static constexpr int kGameplayFrames = 30;

    // Each frame, with the byte as read (-1 unreadable). True while the frame is the front end's menu.
    bool Update(int gameMode);
    // The last mode read (-1: none yet).
    int LastReadable() const { return last_; }
    // Still before the first load from the menus and the first gameplay mode.
    bool Starting() const { return starting_; }
    // The start-up's boot, before the title (its unreadable byte, the logos' movie): the title's 3D screen comes with the
    // title itself (7, or the camera flight's 6), so the boot isn't the screen, then a movie, then the screen again.
    bool BeforeTitle() const { return starting_ && !titled_; }

private:
    int last_ = -1;
    int lastOther_ = -1;      // the last mode read other than a movie (8)
    bool starting_ = true;
    bool titled_ = false;     // a 6 or 7 read at start-up
    int gameplayFrames_ = 0;  // consecutive frames of a gameplay mode (0-4)
};

class VrEntry {
public:
    static constexpr std::uint64_t kHoldMs = 100;

    // Each frame, with whether this frame's decision is plain VR. True: show the previous presentation instead.
    bool Hold(bool plainVr, std::uint64_t nowMs, std::uint64_t holdMs = kHoldMs);
    void AcceptWorldUi();  // an authorized handoff is not a transient mode blip
    // Plain VR runs that ended before the hold did (a blip never shown), for the log.
    unsigned Covered() const { return covered_; }

private:
    bool have_ = false;        // a decision was shown
    bool shownPlain_ = false;  // the shown one is plain VR
    bool waiting_ = false;     // plain VR decided, the previous presentation still shown
    std::uint64_t sinceMs_ = 0;
    unsigned covered_ = 0;
};

// The sequels' loads end in a cinematic mode for a moment (ME2: 0.27-0.30 s of its mode 6 after every load's movie, the
// picture black), which their renderers showed as the 3D cinema screen between the load's flat panel and the world: two
// changes of view for nothing (research/56 "The sequels"). A conversation or cinematic right after a movie is kept as the
// movie (the flat panel) until it has lasted kHoldMs; gameplay meanwhile ends it unseen; one that lasts is the game's. ME1's
// loads have no such tail (LE1 doesn't use it; inline, so LE1's objects don't carry it).
class LoadTail {
public:
    static constexpr std::uint64_t kHoldMs = 500;

    // Each frame: `movie` the renderer's own movie signal this frame (a load's or a prerendered one; never this class's
    // answer), `cinematic` the mode is a conversation or cinematic. True: show this frame as the movie.
    bool Hold(bool movie, bool cinematic, std::uint64_t nowMs, std::uint64_t holdMs = kHoldMs) {
        if (movie) {
            afterMovie_ = true;
            waiting_ = false;
            return false;
        }
        if (!afterMovie_) {
            return false;
        }
        if (!cinematic) {
            if (waiting_) {
                ++covered_;  // gameplay (or anything else) came first: the tail was never shown
            }
            afterMovie_ = false;
            waiting_ = false;
            return false;
        }
        if (!waiting_) {
            waiting_ = true;
            sinceMs_ = nowMs;
        }
        if (nowMs < sinceMs_ || nowMs - sinceMs_ >= holdMs) {
            afterMovie_ = false;  // it lasted: the game's cinematic, shown as it would be
            waiting_ = false;
            return false;
        }
        return true;
    }
    // Tails that ended in another mode before the hold did (never shown), for the log.
    unsigned Covered() const { return covered_; }

private:
    bool afterMovie_ = false;  // the last frame that wasn't this tail was a movie
    bool waiting_ = false;     // a tail is being held
    std::uint64_t sinceMs_ = 0;
    unsigned covered_ = 0;
};

} // namespace mevr::vr::presentation
