// SPDX-License-Identifier: MIT
#pragma once

// The exe's code is encrypted on disk (EA DRM) and only readable once the game runs. For offline
// research (finding engine functions without a debugger), save the decrypted .text section once
// per exe build to %LOCALAPPDATA%\MEVR\dump. a local-only tool (local-tools/, not published) turns it back into a
// disassemblable copy of the exe (tools/ghidra). Local research data only: never committed or
// shared.
//
// Hooks overwrite the first bytes of the functions they patch (a 5-byte jmp out of the image), and
// those bytes are lost in a dump. So the host dumps at attach, before it or Halcyon hooks anything;
// the .txt beside the dump lists the function starts that were already patched (other ASIs).
namespace mevr::engine::code_dump {

// Writes the dump if this exe build doesn't have one yet, or if the existing one has more
// hook-patched function starts than the code has now (or doesn't say). Cheap when it exists.
void SaveOnce();

} // namespace mevr::engine::code_dump
