# SPDX-License-Identifier: MIT
# Builds OpenAL Soft from its source release for the game's 3D audio (research/69 "Real 3D audio"): the game's own OpenAL
# (Creative's router and software device) only pans; OpenAL Soft renders every positioned sound for two ears (HRTF). The
# host loads it from ASI\audio\soft_oal.dll and points the game's OpenAL imports at it (plugin/OpenAlSoft): no game file is
# replaced.
#
# Built here from the pinned source release, not taken from the prebuilt binaries: the DLL we ship then corresponds exactly
# to the source we ship beside it (OpenAL Soft is LGPL-2.0-or-later: distributing the library means offering its source from
# the same place), it is built with our compiler, and it can be signed with our other binaries.
#
#   tools/fetch-openal-soft.ps1 [-Version 1.25.2] [-Tarball <local .tar.bz2>] [-Force]
#
# Output (gitignored), external\openal-soft\:
#   soft_oal.dll                     the library (the build's OpenAL32.dll, renamed: never mistaken for the game's router)
#   openal-soft-<version>.tar.bz2    the source it was built from (the release package carries it, licenses\)
#   COPYING, LICENSE-pffft, BSD-3Clause, README.md   its licence texts
[CmdletBinding()]
param(
    [string] $Version = '1.25.2',
    [string] $Tarball = '',
    [switch] $Force
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$out = Join-Path $root 'external\openal-soft'
# The source releases' SHA-256, checked before anything is built (openal-soft.org/openal-releases).
$pinned = @{
    '1.25.2' = '1dbaac44e7579d5bc8847ca8db4b2e8b9fd3961041f35ee20def4958301e1089'
}
if (-not $pinned.ContainsKey($Version)) { throw "No pinned SHA-256 for OpenAL Soft ${Version}: add it to `$pinned after checking the release" }

$dll = Join-Path $out 'soft_oal.dll'
$stamp = Join-Path $out 'VERSION.txt'
if (-not $Force -and (Test-Path $dll) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $Version)) {
    Write-Output "OpenAL Soft ${Version} is in $out"
    exit 0
}

New-Item -ItemType Directory -Force $out | Out-Null
$name = "openal-soft-$Version.tar.bz2"
$kept = Join-Path $out $name
if ($Tarball) {
    Copy-Item -LiteralPath $Tarball -Destination $kept -Force
} elseif (-not (Test-Path $kept)) {
    $uri = "https://openal-soft.org/openal-releases/$name"
    Write-Output "  $uri"
    Invoke-WebRequest -Uri $uri -OutFile "$kept.part"
    Move-Item "$kept.part" $kept -Force
}
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $kept).Hash.ToLowerInvariant()
if ($hash -ne $pinned[$Version]) {
    Remove-Item -LiteralPath $kept -Force
    throw "$name has SHA-256 $hash, not the pinned $($pinned[$Version]): removed"
}

$work = Join-Path $env:TEMP "mevr-openal-soft-$Version"
if (Test-Path $work) { Remove-Item $work -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force $work | Out-Null
# Windows' own tar (bsdtar): a GNU tar first on the PATH (Git's, from a Git Bash shell) reads the drive letter in
# H:\... as a remote host and fails.
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
if (-not (Test-Path $tar)) { $tar = 'tar' }
& $tar -xjf $kept -C $work
if ($LASTEXITCODE -ne 0) { throw "tar failed on $kept" }
$src = Join-Path $work "openal-soft-$Version"
$build = Join-Path $work 'build'
Write-Output "OpenAL Soft ${Version} (LGPL-2.0-or-later): building in $build"
# Only the library: no utilities (alsoft-config needs Qt), examples, tests or install rules. Windows' own backends
# (WASAPI, then DirectSound and WinMM) are always there; nothing else is looked for. The C runtime linked in (/MT): the DLL
# needs nothing a player's machine may lack (its own FORCE_STATIC_VCRT switch does nothing under CMake's runtime policy).
& cmake -S $src -B $build -G 'Visual Studio 17 2022' -A x64 -DLIBTYPE=SHARED -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF `
    -DALSOFT_TESTS=OFF -DALSOFT_INSTALL=OFF -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF `
    -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF -DALSOFT_INSTALL_UTILS=OFF -DALSOFT_NO_CONFIG_UTIL=ON | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cmake configure of OpenAL Soft failed' }
& cmake --build $build --config Release --target OpenAL --parallel | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cmake build of OpenAL Soft failed' }
$built = Join-Path $build 'Release\OpenAL32.dll'
if (-not (Test-Path $built)) { throw "no $built after the build" }

Copy-Item -LiteralPath $built -Destination $dll -Force
$pdb = Join-Path $build 'Release\OpenAL32.pdb'
if (Test-Path $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $out 'soft_oal.pdb') -Force }
foreach ($f in 'COPYING', 'LICENSE-pffft', 'BSD-3Clause', 'README.md') {
    $from = Join-Path $src $f
    if (Test-Path $from) { Copy-Item -LiteralPath $from -Destination (Join-Path $out $f) -Force }
}
Set-Content -LiteralPath $stamp -Value $Version -Encoding ascii
Remove-Item $work -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue
Write-Output "OpenAL Soft ${Version} -> $dll ($([math]::Round((Get-Item $dll).Length / 1MB, 1)) MB), its source and licences beside it"
