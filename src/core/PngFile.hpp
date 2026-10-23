// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// PNG files through WIC (Windows Imaging Component, part of Windows: no extra dependency). The
// calling thread must have COM initialized. Pixels are 0xAABBGGRR (R in the low byte), straight
// alpha, row after row. Tested in tests/Main.cpp.
namespace mevr::png {

bool Write(std::filesystem::path const& path, int width, int height, std::uint32_t const* pixels, std::string* error = nullptr);
bool Read(std::filesystem::path const& path, int& width, int& height, std::vector<std::uint32_t>& pixels,
          std::string* error = nullptr);

} // namespace mevr::png
