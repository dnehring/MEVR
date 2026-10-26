// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The in-game side of "Report a problem" (research/33 section 8): the main menu's Advanced > Diagnostics > Report writes
// %LOCALAPPDATA%\MEVR\report.request and starts the setup copy in the ASI folder right away, with
// --report --from-game: a separate process at background priority that reads the logs, writes the zip to the Desktop
// and report.result, and exits. No window (it would take the focus from the game), nothing heavy in the game's process:
// the request file and the process start run on a thread of their own, never the render or game thread. Without the
// setup in the ASI folder (a development deploy) the request waits: MEVR Setup opens on Report a problem next time.
// Also the crash offer: crash-breadcrumbs.txt newer than the last report and the last "no" (install/ReportMarks).
namespace mevr::host::report_request {

// Starts a report. The message for the menu.
void Request(bool snapshot, bool perf, std::string& message);
// The last request's outcome for the menu ("Writing the report...", "Saved to the Desktop: ..."), or "" (re-read at most
// every second).
std::string StatusText();
// The game crashed last session and the player hasn't reported it or said no (re-checked at most every 2 s).
bool CrashOffer();
// The player said no: writes the dismissed mark.
void DismissCrash();

} // namespace mevr::host::report_request
