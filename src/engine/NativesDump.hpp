// SPDX-License-Identifier: MIT
#pragma once

// Opt-in research dump of the engine's native functions, to name them in the local Ghidra project
// (tools/ghidra/seed.ps1). With MEVR_DUMP_NATIVES=1 in the environment, or `dumpNatives = 1` under
// [Debug] in ASI\<game>MEVR.ini, writes %LOCALAPPDATA%\MEVR\dump\natives-<game>-<timestamp>.csv:
// every UFunction with FUNC_Native and its Func pointer, as `rva,name,kind,comment` (name
// "ABioWeaponRanged::execGetMuzzleLoc", comment "native BioWeaponRanged.GetMuzzleLoc (SFXGame)"),
// plus the GNatives bytecode handlers no UFunction names. Local research data only. Read through our
// reflection layer (research/34, stage 3): nothing is written while it's off (LE2/LE3 until stage 5).
namespace mevr::engine::natives_dump {

// Once per session, after the script packages are loaded; game thread. Cheap when not requested.
void SaveIfRequested();

} // namespace mevr::engine::natives_dump
