// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <Windows.h>

namespace MELEVR::DxgiProxy
{
bool EnsureRealDxgiLoaded() noexcept;
void InstallRealFactoryHooks() noexcept;
FARPROC ResolveExport(const char* exportName) noexcept;
HRESULT MissingExportResult(const char* exportName) noexcept;
}
