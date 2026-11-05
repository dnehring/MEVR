// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>

struct ID3D11Device;

namespace mevr::menu {
class Renderer;
}

// The omni-tool's rail drawn with ME1's own UI art (research/56; the owner, 2026-11-04: "ME1's screens, exactly"). From the
// movies game/GameArt caches in %LOCALAPPDATA%\MEVR\gameart (the player's own game data, never shipped), with core/gfx:
//   - the game's fonts, rebuilt as TrueType and registered with RmlUi as "ME1 Biomass" (titles), "ME1 Tigensu" (tabs, labels)
//     and "ME1 Myriad" (text);
//   - the journal's tab (GUI_SF_Journal.PC_Journal, sprite `leftTab`) in its states as "mevr:me1-tab", "mevr:me1-tab-hover"
//     (the game's orange mouse-on), "mevr:me1-tab-selected" and "mevr:me1-tab-selected-hover", fitted to the rail's buttons
//     keeping the bevel (insets 50/0/15/0, the corners scaled with the height);
//   - the inventory's item-list frame (GUI_SF_Inventory.PCInventory, shape 324) as "mevr:me1-panel", nine-sliced to the rail.
// Render thread. Nothing here touches the game.
namespace mevr::host::rail_art {

struct Status {
    bool fonts = false;  // all three faces registered
    bool tabs = false;   // the tab's four states made
    bool panel = false;  // the frame made
    std::string detail;  // what was missing, for the log
    std::string summary; // one line for the caller's log (`omni-tool rail: <summary>`)
};

struct Sizes {
    int tabWidth = 300;
    int tabHeight = 64;
    int panelWidth = 360;
    int panelHeight = 640;
};

// The cache folder (%LOCALAPPDATA%\MEVR\gameart).
std::filesystem::path CacheFolder();
// Loads what the cache holds (`folder`, empty: CacheFolder()) onto `renderer` (textures) and into RmlUi (fonts, once per
// process). Safe to call again when the cache changed: textures are replaced, fonts already registered are kept.
Status Load(ID3D11Device* device, menu::Renderer* renderer, Sizes const& sizes, std::filesystem::path const& folder = {});
// The cache's movies changed since the last Load (looked at most once every 2 s).
bool CacheChanged();

} // namespace mevr::host::rail_art
