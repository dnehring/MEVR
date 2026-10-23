#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Text-level INI editing for the installer (research/33): one key at a time, everything else in the file kept byte
// for byte (comments, order, blank lines, other keys, line endings). Each change can be recorded as an Edit, so an
// uninstall puts back exactly the values it changed, and only where the player hasn't changed them since.
//
// Sections and keys match case-insensitively (Unreal's and Halcyon's files are both read that way). A key line is
// `key=value` or `key = value`; a replaced value keeps the line's own spelling of the key and its spacing.
namespace mevr::install::ini {

struct Edit {
    std::string section;
    std::string key;
    std::optional<std::string> before;  // nullopt: the key wasn't there
    std::string after;
    bool operator==(Edit const&) const = default;
};

// The value of `key` in `section` (the first such line), trimmed; nullopt when either is missing.
std::optional<std::string> Get(std::string_view text, std::string_view section, std::string_view key);

// Sets `key` in `section` (every line of it, if the section repeats the key), adding the key after the section's last
// line, or the section at the end of the file. New lines use the section's separator style (" = " when its keys are
// written that way, else "=") and the file's line ending. A changed value is appended to `edits` (merged with an
// earlier edit of the same key: its `before` is kept).
std::string Set(std::string_view text, std::string_view section, std::string_view key, std::string_view value,
    std::vector<Edit>* edits = nullptr);

// Removes every `key` line in `section`.
std::string Remove(std::string_view text, std::string_view section, std::string_view key);

// Undoes `edits`, newest first: a key still holding the value an edit wrote gets its old value back (or is removed,
// if it wasn't there before); a key the player has changed since is left alone.
std::string Revert(std::string_view text, std::vector<Edit> const& edits);

// The section names in file order (as written, without brackets).
std::vector<std::string> Sections(std::string_view text);

// Records `edit` into `edits`, keeping the earliest `before` for the same section and key.
void Merge(std::vector<Edit>& edits, Edit const& edit);

// ---- Halcyon's MELEVR.ini (what tools/deploy-asi.ps1 does, here for the setup app) ------------------------------
// The file the deploy installs when there is none: Halcyon's release package's mode and resolutions in all four
// profile slots, plus our per-slot choices (welcome seen, menu on backslash, first person, no head aim, life-size
// stereo, the desktop mirror every 2nd frame, depth pop off) and the F-key profile hotkeys off.
std::string HalcyonReleaseIni();
// Every-install fixes on an existing file, never resetting anything else: the profile hotkeys off; in every slot the
// menu on backslash (menuKey = 220), no head aim (combatHeadAim / makoHeadAim 1 -> 0), the HUD panel's old 50 degree
// follow becomes 70, and sfr2Metric = 1 added to a slot that has no sfr2Metric yet (a 0 the player chose stays).
std::string FixHalcyonIni(std::string_view text, std::vector<Edit>* edits = nullptr);
// Halcyon's own copy of the stereo target (sfr2ResX/Y, expectedResX/Y) in every profile slot that has those keys.
std::string SetHalcyonResolution(std::string_view text, int width, int height, std::vector<Edit>* edits = nullptr);

} // namespace mevr::install::ini
