// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The constant-buffer variables of a compiled D3D11 shader, read from its bytecode's reflection chunk (RDEF) without
// the D3DReflect API: the game's shaders keep it (LE1's global shader cache: 350 shaders, all with RDEF; research/37,
// "The right eye's shadows"). Used by the draw replay to find where a pixel shader takes a screen-space matrix.
// Every read is bounds-checked: the bytes come from the game, inside a device hook.
namespace mevr::d3d11::reflect {

struct Variable {
    std::string name;
    std::uint32_t slot = 0;     // the constant buffer's bind point (b#)
    std::uint32_t offset = 0;   // bytes into that constant buffer
    std::uint32_t size = 0;     // bytes
    std::uint16_t varClass = 0; // D3D_SHADER_VARIABLE_CLASS: 2 matrix stored by rows, 3 by columns
    std::uint16_t rows = 0, columns = 0;
};

// The variables of every constant buffer in the bytecode whose name `want` accepts (all of them with a null
// `want`). Empty when there's no RDEF chunk or it is malformed.
using NameFilter = bool (*)(std::string_view name);
std::vector<Variable> Variables(void const* bytecode, std::size_t size, NameFilter want = nullptr);

} // namespace mevr::d3d11::reflect
