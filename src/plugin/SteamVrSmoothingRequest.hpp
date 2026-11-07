// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace mevr::host::steamvr_smoothing {
// Serial background helpers: a newer choice waits for the previous helper to finish.
void Request(bool off);
std::string StatusText();
} // namespace mevr::host::steamvr_smoothing
