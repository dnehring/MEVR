#pragma once

// Between WorldPanels.cpp (MevrBridge::PlacePanel) and the rest of the host's bridge. Present thread.
namespace MELEVR::MevrBridge {

// Each frame, right after xrBeginFrame (OnFrameBegun): counts frames, so a panel can tell whether it
// was shown the frame before.
void NoteWorldPanelFrame() noexcept;

// Recentering re-origined the app space (OnRecentered): every world-locked panel is placed afresh.
void RecenterWorldPanels() noexcept;

} // namespace MELEVR::MevrBridge
