// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

// Text files embedded at build time (tools/cmake/EmbedText.cmake, `mevr_embed_text` in CMakeLists.txt): each is a
// NUL-terminated char array and its byte count. The targets that use one list its generated .cpp among their sources.
namespace mevr::embedded {

extern char const kFfxA[];       // external/fsr1/ffx_a.h (AMD FidelityFX shader portability header, MIT)
extern std::size_t const kFfxASize;
extern char const kFfxFsr1[];    // external/fsr1/ffx_fsr1.h (FidelityFX Super Resolution 1: EASU and RCAS, MIT)
extern std::size_t const kFfxFsr1Size;

} // namespace mevr::embedded
