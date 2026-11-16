// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <Windows.h>

namespace ME2VR::Dxgi
{
void InstallRealFactoryHooks() noexcept; // [MEVR] ASI factory hooks
bool EnsureLoaded() noexcept;
FARPROC Resolve(const char* name) noexcept;
HRESULT MissingExport(const char* name) noexcept;
}
