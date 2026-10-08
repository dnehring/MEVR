#pragma once

// The exe's code is encrypted on disk (EA DRM) and only readable once the game runs. For offline
// research (finding engine functions without a debugger), save the decrypted .text section once
// per exe build to %LOCALAPPDATA%\MEVR\dump. tools/splice_text.py turns it back into a
// disassemblable copy of the exe. Local research data only: never committed or shared.
namespace mevr::engine::code_dump {

// Writes the dump if this exe build doesn't have one yet. Cheap when it exists.
void SaveOnce();

} // namespace mevr::engine::code_dump
