# SPDX-License-Identifier: MIT
# Builds and assembles a release folder (and optionally a zip) for players (research/33). Local only: it publishes
# and uploads nothing.
#
#   dist\MEVR-<version>\
#     MEVR-Setup.exe        the installer (tools/setup)
#     MEVR-Setup.ini        its settings: the update check's repository (dnehring/MEVR)
#     README.md             docs/release-readme.md, with the source pointer (GPL-3.0: tag v<version>)
#     LICENSE, NOTICE.md
#     licenses\*            every third-party licence text the binaries need (MinHook, Dear ImGui, RmlUi, FreeType,
#                           FSR 1, the OpenXR loader and its jsoncpp, our MIT text, the fonts' OFL)
#     payload\LE1MEVR.asi   the monolithic mod (build-mono: -DMEVR_HOT_RELOAD=OFF)
#     payload\bink2w64.dll  our ASI loader (src/loader)
#     payload\fonts\*       the menu's fonts with their OFL licences
#     payload\menu\*        the in-game menu's documents and the setup's own page
#
#   tools\package-release.ps1 [-Version 0.1.0] [-NoBuild] [-Zip] [-LexSdkPermission]
#
# LExSDKv2 (compiled into LE1MEVR.asi while research/34's removal isn't finished) has no licence: distributing the
# build needs its author's permission first (docs/release-readme.md, research/33). The script refuses to package while
# the ASI links it, unless -LexSdkPermission says that permission was given. Texture packs are never bundled.
param(
    [string]$Version = '0.1.0',
    [switch]$NoBuild,
    [switch]$Zip,
    [switch]$LexSdkPermission
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $repo 'build-mono'

if (-not $NoBuild) {
    if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        cmake -S $repo -B $buildDir -G 'Visual Studio 17 2022' -A x64 -DMEVR_HOT_RELOAD=OFF
        if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
    }
    if (-not (Select-String -Path (Join-Path $buildDir 'CMakeCache.txt') -Pattern '^MEVR_HOT_RELOAD:BOOL=OFF' -Quiet)) {
        throw "$buildDir isn't a monolithic build (MEVR_HOT_RELOAD must be OFF)"
    }
    cmake --build $buildDir --config Release --target MEVR-LE1 MEVR-Loader MEVR-Setup
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

$out = Join-Path $buildDir 'Release'
foreach ($f in 'LE1MEVR.asi', 'bink2w64.dll', 'MEVR-Setup.exe') {
    if (-not (Test-Path (Join-Path $out $f))) { throw "Not built: $(Join-Path $out $f)" }
}

# The hard gate: LExSDKv2 has no licence, so a build that links it can't be given to anyone without its author's
# permission. MEVR-LE1's project says whether it links the SDK (LESDK-LE1); no project to read counts as linking it.
$asiProject = Join-Path $buildDir 'MEVR-LE1.vcxproj'
$linksSdk = -not (Test-Path $asiProject) -or (Select-String -Path $asiProject -Pattern 'LESDK-LE1' -SimpleMatch -Quiet)
if ($linksSdk -and -not $LexSdkPermission) {
    throw ("Refusing to package: LE1MEVR.asi is built with LExSDKv2, which has no licence. Distributing it needs its " +
        "author's written permission (docs/release-readme.md, research/33). Once you have it, run again with " +
        "-LexSdkPermission; or finish removing LExSDKv2 (research/34) first.")
}
if ($linksSdk) { Write-Warning 'LExSDKv2 is compiled into LE1MEVR.asi: packaging under -LexSdkPermission (its author agreed).' }

# Every licence text the shipped binaries need, gathered first so a missing one stops the packaging.
$licences = [ordered]@{
    'MEVR-MIT.txt'                   = 'LICENSES\MIT.txt'
    'GPL-3.0.txt'                    = 'LICENSES\GPL-3.0-only.txt'
    'Halcyon-MassEffect1-VR.txt'     = 'src\halcyon\LICENSE'
    'MinHook-BSD-2-Clause.txt'       = 'third_party\minhook\LICENSE.txt'
    'DearImGui-MIT.txt'              = 'third_party\imgui\LICENSE.txt'
    'RmlUi-MIT.txt'                  = 'external\RmlUi\LICENSE.txt'
    'RmlUi-Core-Containers.txt'      = 'external\RmlUi\Include\RmlUi\Core\Containers\LICENSE.txt'
    'FreeType-FTL.txt'               = 'external\freetype\docs\FTL.TXT'
    'FSR1-MIT.txt'                   = 'external\fsr1\LICENSE.txt'
    'OpenXR-Loader-Apache-2.0.txt'   = 'external\OpenXR-SDK\LICENSE'
    'OpenXR-Loader-jsoncpp.txt'      = 'external\OpenXR-SDK\src\external\jsoncpp\LICENSE'
    'Rajdhani-OFL.txt'               = 'third_party\fonts\Rajdhani-OFL.txt'
    'TitilliumWeb-OFL.txt'           = 'third_party\fonts\TitilliumWeb-OFL.txt'
}
foreach ($source in $licences.Values) {
    if (-not (Test-Path (Join-Path $repo $source))) { throw "Licence text missing: $source (fill external\ and third_party\ first)" }
}

$dist = Join-Path $repo "dist\MEVR-$Version"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
$payload = Join-Path $dist 'payload'
New-Item -ItemType Directory -Force $payload | Out-Null

Copy-Item (Join-Path $out 'MEVR-Setup.exe') $dist
Copy-Item (Join-Path $out 'LE1MEVR.asi') $payload
Copy-Item (Join-Path $out 'bink2w64.dll') $payload
Copy-Item -Recurse (Join-Path $repo 'third_party\fonts') (Join-Path $payload 'fonts')
Copy-Item -Recurse (Join-Path $repo 'assets\menu') (Join-Path $payload 'menu')
$readme = Get-Content -Raw (Join-Path $repo 'docs\release-readme.md')
$readme = $readme.Replace('<version>', $Version)
[IO.File]::WriteAllText((Join-Path $dist 'README.md'), $readme)
foreach ($f in 'LICENSE', 'NOTICE.md') {
    Copy-Item (Join-Path $repo $f) $dist
}
$licenceDir = Join-Path $dist 'licenses'
New-Item -ItemType Directory -Force $licenceDir | Out-Null
foreach ($name in $licences.Keys) {
    Copy-Item (Join-Path $repo $licences[$name]) (Join-Path $licenceDir $name)
}
$ini = "; MEVR Setup's settings.`r`n[Update]`r`n; The GitHub repository whose Releases the setup checks (https://github.com/OWNER/REPO turns the check off).`r`nrepo = https://github.com/dnehring/MEVR`r`n"
[IO.File]::WriteAllText((Join-Path $dist 'MEVR-Setup.ini'), $ini)

Write-Output "Release folder: $dist"
Get-ChildItem -Recurse -File $dist | ForEach-Object {
    '{0,-40} {1,12:N0}  {2}' -f $_.FullName.Substring($dist.Length + 1), $_.Length, (Get-FileHash $_.FullName).Hash.Substring(0, 16)
}

if ($Zip) {
    $zipPath = "$dist.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath }
    Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zipPath
    Write-Output ("Zip: {0} ({1:N1} MB, SHA-256 {2})" -f $zipPath, ((Get-Item $zipPath).Length / 1MB), (Get-FileHash $zipPath).Hash)
}
