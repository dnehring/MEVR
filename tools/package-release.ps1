# SPDX-License-Identifier: MIT
# Checks, builds and assembles a release folder (and optionally a zip) for players (research/33). Local only: it
# publishes and uploads nothing (the release workflow, .github/workflows/release.yml, runs it on a v* tag).
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
#     payload\cockpit\*     the Mako's cockpit (research/47): BIOG_MEVR_Cockpit.pcc (-CockpitPackage, default the hull
#                           packaging's build\cockpit-hull) and cockpit.txt (tools/cockpit/place.py --script); MEVR Setup
#                           puts the package in CookedPCConsole with its PCConsoleTOC.bin entry. -NoCockpit leaves it out.
#     payload\voice\MEVR-Voice.dll  the voice orders' recognizer interface (research/54; statically linked with Moonshine
#                           Voice's library, MIT: its licence goes in licenses\). MEVR Setup puts it in ASI\voice with the
#                           mod; the runtime and models it needs are optional content the setup downloads (install/Voice,
#                           research/33 section 14). Moonshine's SDK is fetched first if external\moonshine-voice is missing
#                           (tools/fetch-voice.ps1 -Models @(), 26 MB), and llama.cpp's headers (-Llm -NoBaseModel), without
#                           which the host has no model tier: it refuses a host project without core/voice/Llm.cpp.
#                           -NoVoice leaves both out.
#     payload\audio\*       OpenAL Soft (research/69 "Real 3D audio"): soft_oal.dll, built from its pinned source release by
#                           tools/fetch-openal-soft.ps1 (run first if external\openal-soft is missing), with its licence
#                           texts. MEVR Setup puts it in ASI\audio; the host points the game's OpenAL imports at it (no game
#                           file replaced). It is LGPL-2.0-or-later: the source it was built from goes in licenses\
#                           (openal-soft-<version>.tar.bz2), the same place as the binary. -NoAudio leaves it out.
#     payload\rig\*         the moving bodies' builder (tools/rig/README.md "The players' build", install/Bodies): mevr-rig.exe
#                           published self-contained for Windows x64 (dotnet publish tools/rig/Rig.csproj -p:MevrPlayers=true
#                           into build-mono\rig: the .NET runtime trimmed, Legendary Explorer's library and the four libraries
#                           it loads, their licence texts in licenses\, also copied into the release's licenses\; never Oodle,
#                           which it loads from the player's game folder), and plans\: players.jsonc and the plans it names.
#                           MEVR Setup runs it after each game's install and puts the packages it builds from the player's
#                           own game files in CookedPCConsole with PCConsoleTOC.bin entries. The publish needs Legendary
#                           Explorer's nightly in mod-manager\ (Rig.csproj's LexDir). -NoRig leaves it out.
#     With -Games LE1,LE2 (or LE3): the sequels' part of the release (docs/trilogy-plan.md T52). Whether and when a sequel is
#     offered to players is the owner's call, so the default is -Games LE1 and nothing below is in the package:
#     payload\LE2MEVR.asi   that game's monolithic mod (build-mono configured with -DMEVR_BUILD_LE2=ON / -DMEVR_BUILD_LE3=ON)
#     payload\MELE2VR.ini   Halcyon's ME2 release settings (src/halcyon/le2/runtime, GPL-3.0), which MEVR Setup installs next
#                           to MassEffect2.exe only when there is none; ME3's release has no such file
#     MEVR-Setup.ini        gets [Setup] games = LE1, LE2: the one switch that makes MEVR Setup offer the sequels
#     A sequel whose ASI links LExSDKv2 is refused (the gate below; a monolithic build links none since T04).
#   dist\MEVR-<version>-symbols\
#     *.pdb and the binaries they match (LE1MEVR.asi, bink2w64.dll, MEVR-Setup.exe, MEVR-Voice.dll), to symbolise the stacks in players'
#     crash reports. Kept next to the players' folder, never inside it (the binaries name their PDB without a path:
#     /PDBALTPATH, CMakeLists.txt). **For the owner's archive, never uploaded** (the owner, 2026-11-20, from 0.4.0): players
#     never need them, and a PDB records the build machine's paths (the Windows user's temp folder among them). A PDB fits
#     only the exact build it came from, so keep each release's symbols zip somewhere durable (dist\ is gitignored).
#   with -Zip: dist\MEVR-<version>.zip, dist\MEVR-<version>-SHA256SUMS.txt and dist\MEVR-<version>-release-notes.md
#     (.github/release-notes-template.md with this version's CHANGELOG.md entry): the three a GitHub release gets; and
#     dist\MEVR-<version>-symbols.zip for the archive (its SHA-256 in dist\MEVR-<version>-symbols.sha256.txt).
#
#   tools\package-release.ps1 [-Version 0.1.0] [-Tag v0.1.0] [-NoBuild] [-Zip] [-AllowDirty] [-SkipTests]
#                             [-LexSdkPermission] [-CockpitPackage <pcc>] [-NoCockpit] [-NoVoice] [-NoRig] [-Games LE1,LE2,LE3]
#
# The version is CMake's: project(MEVR VERSION x.y.z) in CMakeLists.txt is the only place it is written. -Version only
# confirms it. It refuses to package (docs/release-checklist.md section 5):
#   - a changed tracked file, or a release submodule (OpenXR-SDK, RmlUi, freetype) not at the commit's pointer, unless
#     -AllowDirty (the binaries then say <commit>-dirty);
#   - another version anywhere: -Version or -Tag (the release workflow's) other than CMake's, a v* tag on HEAD other than
#     v<version>, a binary whose version resource names another version or another commit than HEAD;
#   - a source file without its SPDX line (python tools/check-spdx.py);
#   - a failing ctest in build-mono, unless -SkipTests;
#   - an ASI that links LExSDKv2 (no licence), unless -LexSdkPermission says its author agreed: LE1MEVR.asi, and
#     LE2MEVR.asi / LE3MEVR.asi when build-mono is configured with them (T04, docs/trilogy-plan.md);
#   - a missing licence text, a missing cockpit package (unless -NoCockpit), no MEVR-Voice.dll (unless -NoVoice), or no
#     published mevr-rig of this version and commit (unless -NoRig).
# It warns when HEAD has no tag v<version> and when the cockpit package is older than its sources (tools/cockpit,
# concept/assets/game-ready: rebuild it with tools/cockpit/package). Texture packs are never bundled.
#
# LExSDKv2 has no licence: the release build (build-mono, MEVR_LE1_LESDK=OFF since 2026-10-26; MEVR_LE2_LESDK and
# MEVR_LE3_LESDK, off in a monolithic build since 2026-11-16) doesn't link it on any game.
param(
    [string]$Version = '',
    [string]$Tag = '',
    [switch]$NoBuild,
    [switch]$Zip,
    [switch]$AllowDirty,
    [switch]$SkipTests,
    [switch]$LexSdkPermission,
    [string]$CockpitPackage = '',
    [switch]$NoCockpit,
    [switch]$NoVoice,
    [switch]$NoAudio,
    [switch]$NoRig,
    # The games the package carries and MEVR Setup offers. Mass Effect 1 always; a sequel only when named.
    [string[]]$Games = @('LE1')
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $repo 'build-mono'

# Native tools write warnings to stderr (git, CMake), which Windows PowerShell 5.1 turns into terminating errors under
# 'Stop': run them under 'Continue' and judge them by $LASTEXITCODE.
function Invoke-Native([scriptblock]$Command) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command } finally { $ErrorActionPreference = $saved }
}
function Stop-Packaging([string]$Why) {
    throw "Refusing to package: $Why"
}
# -Games LE1,LE2 arrives as one word through powershell -File.
$Games = @($Games | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToUpper() } | Where-Object { $_ })
foreach ($g in $Games) { if ($g -notin 'LE1', 'LE2', 'LE3') { Stop-Packaging "-Games: LE1, LE2 or LE3, not '$g'" } }
$sequels = @('LE2', 'LE3' | Where-Object { $Games -contains $_ })
# The hard gate for a sequel in -Games, asked early: right after configuring (before a long build) and, with -NoBuild,
# before anything else about the sequel. The same rule as the gate before packaging (Test-LinksLexSdk below, T04): the
# game's ASI project names the LESDK-<game> library or lacks the MEVR_NO_LESDK define; no project to read counts as
# linking it (a build-mono never configured with -DMEVR_BUILD_<game>=ON).
function Assert-SequelSdkFree([string]$Sequel) {
    $project = Join-Path $buildDir "MEVR-$Sequel.vcxproj"
    $links = $true
    if (Test-Path $project) {
        $text = [IO.File]::ReadAllText($project)
        $links = $text.Contains("LESDK-$Sequel") -or -not $text.Contains('MEVR_NO_LESDK')
    }
    if ($links -and -not $LexSdkPermission) {
        throw ("Refusing to package: ${Sequel}MEVR.asi is built with LExSDKv2, which has no licence, or $project is missing " +
            "(configure $buildDir with -DMEVR_BUILD_${Sequel}=ON; a monolithic build links no SDK since T04). Leave $Sequel out " +
            "of -Games, or run again with -LexSdkPermission once the SDK's author has agreed in writing.")
    }
    if ($links) { Write-Warning "LExSDKv2 is compiled into ${Sequel}MEVR.asi: packaging under -LexSdkPermission (its author agreed)." }
}

# ---- The version: CMake's, and only CMake's ------------------------------------------------------------------------
$cmakeLists = Get-Content -Raw (Join-Path $repo 'CMakeLists.txt')
$match = [regex]::Match($cmakeLists, '(?m)^\s*project\(\s*MEVR\s+VERSION\s+(\d+\.\d+\.\d+)[\s)]')
if (-not $match.Success) { Stop-Packaging 'no project(MEVR VERSION x.y.z) line in CMakeLists.txt' }
$cmakeVersion = $match.Groups[1].Value
if ($Version -and $Version -ne $cmakeVersion) {
    Stop-Packaging ("-Version $Version, but CMake's version is $cmakeVersion. Change project(MEVR VERSION ...) in " +
        "CMakeLists.txt (the one place it is written), rebuild, and package again.")
}
$Version = $cmakeVersion
if ($Tag -and $Tag -ne "v$Version") { Stop-Packaging "the tag is $Tag, but CMake's version is $Version (tag v$Version)" }

$head = "$(Invoke-Native { git -C $repo rev-parse --short=10 HEAD })".Trim()
if ($LASTEXITCODE -ne 0 -or -not $head) { Stop-Packaging 'git rev-parse failed: package from a git checkout' }
$headTags = @(Invoke-Native { git -C $repo tag --points-at HEAD --list 'v*' } | Where-Object { $_ })
$otherTags = @($headTags | Where-Object { $_ -ne "v$Version" })
if ($otherTags.Count -gt 0) { Stop-Packaging "HEAD is tagged $($otherTags -join ', '), but CMake's version is $Version" }
if (-not $Tag -and -not ($headTags -contains "v$Version")) {
    Write-Warning "HEAD ($head) has no tag v$Version yet: tag the commit you release (the README points players at it)."
}
Write-Output "MEVR $Version, commit $head"

# ---- A clean tree ----------------------------------------------------------------------------------------------------
$dirty = @(Invoke-Native { git -C $repo status --porcelain --untracked-files=no --ignore-submodules=all } | Where-Object { $_ })
if ($LASTEXITCODE -ne 0) { Stop-Packaging 'git status failed' }
$releaseSubmodules = 'external/OpenXR-SDK', 'external/RmlUi', 'external/freetype'
$subStatus = @(Invoke-Native { git -C $repo submodule status -- $releaseSubmodules })
if ($LASTEXITCODE -ne 0) {
    $dirty += "the release submodules' commits can't be read (git submodule status failed): $($releaseSubmodules -join ', ')"
} else {
    foreach ($line in $subStatus) {
        # ' <sha> <path> (<describe>)': at the commit's pointer; '+': another commit; '-': not checked out; 'U': conflict.
        if ($line -and $line[0] -ne ' ') { $dirty += "submodule: $line" }
    }
}
if ($dirty.Count -gt 0) {
    $list = ($dirty | Select-Object -First 20) -join "`n  "
    if (-not $AllowDirty) {
        Stop-Packaging ("the tree isn't clean (a release is built from a commit):`n  $list`nCommit or stash the changes, " +
            "update the submodules (git submodule update --init $($releaseSubmodules -join ' ')), or pass -AllowDirty for a test package.")
    }
    Write-Warning "Packaging a dirty tree (-AllowDirty); not for release:`n  $list"
}

# ---- Every source file has its SPDX line ---------------------------------------------------------------------------
Invoke-Native { python (Join-Path $repo 'tools\check-spdx.py') }
if ($LASTEXITCODE -ne 0) { Stop-Packaging 'tools/check-spdx.py found files without an SPDX line (above)' }

# ---- Build ---------------------------------------------------------------------------------------------------------
if (-not $NoBuild) {
    if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        Invoke-Native { cmake -S $repo -B $buildDir -G 'Visual Studio 17 2022' -A x64 -DMEVR_HOT_RELOAD=OFF }
        if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
    }
    if (-not (Select-String -Path (Join-Path $buildDir 'CMakeCache.txt') -Pattern '^MEVR_HOT_RELOAD:BOOL=OFF' -Quiet)) {
        throw "$buildDir isn't a monolithic build (MEVR_HOT_RELOAD must be OFF)"
    }
    # The voice orders' DLL needs Moonshine Voice's prebuilt library (gitignored): fetched once, then CMake configured again
    # so it sees it (CMakeLists.txt builds MEVR-Voice only when external\moonshine-voice is there).
    if (-not $NoVoice -and -not (Test-Path (Join-Path $repo 'external\moonshine-voice\lib\moonshine.lib'))) {
        Invoke-Native { & (Join-Path $repo 'tools\fetch-voice.ps1') -Models @() }
        if ($LASTEXITCODE -ne 0 -and -not (Test-Path (Join-Path $repo 'external\moonshine-voice\lib\moonshine.lib'))) {
            throw 'tools/fetch-voice.ps1 failed: no Moonshine Voice SDK for MEVR-Voice.dll (or pass -NoVoice)'
        }
    }
    # The model tier's runtime in the host (core/voice/Llm, MEVR_HAVE_LLAMA) needs llama.cpp's headers (external\llama-cpp,
    # gitignored): without them the host builds without it and the game logs "voice model: off: this build has no llama.cpp
    # runtime" even with the model installed (the 2026-11-06 QA build). Fetched once (fetch-voice.ps1 -Llm -NoBaseModel).
    $llamaHeader = Join-Path $repo 'external\llama-cpp\include\llama.h'
    if (-not $NoVoice -and -not (Test-Path $llamaHeader)) {
        Invoke-Native { & (Join-Path $repo 'tools\fetch-voice.ps1') -Models @() -Llm -NoBaseModel }
        if (-not (Test-Path $llamaHeader)) { throw 'tools/fetch-voice.ps1 -Llm failed: no llama.cpp headers for the model tier (or pass -NoVoice)' }
    }
    $hostProject = Join-Path $buildDir 'MEVR-LE1.vcxproj'
    $hostHasLlm = { (Test-Path $hostProject) -and (Select-String -Path $hostProject -Pattern 'core\\voice\\Llm\.cpp' -Quiet) }
    if (-not $NoVoice -and (-not (Test-Path (Join-Path $buildDir 'MEVR-Voice.vcxproj')) -or -not (& $hostHasLlm))) {
        Invoke-Native { cmake -S $repo -B $buildDir }
        if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
    }
    if (-not $NoVoice -and -not (& $hostHasLlm)) { throw "$hostProject has no core\voice\Llm.cpp after configuring: the host would have no model tier" }
    $targets = @('MEVR-LE1', 'MEVR-Loader', 'MEVR-Setup')
    foreach ($sequel in $sequels) {
        # The sequel's host is only in a build configured for it.
        if (-not (Select-String -Path (Join-Path $buildDir 'CMakeCache.txt') -Pattern "^MEVR_BUILD_${sequel}:BOOL=ON" -Quiet)) {
            Invoke-Native { cmake -S $repo -B $buildDir "-DMEVR_BUILD_${sequel}=ON" }
            if ($LASTEXITCODE -ne 0) { throw "configure failed (-DMEVR_BUILD_${sequel}=ON)" }
        }
        Assert-SequelSdkFree $sequel
        $targets += "MEVR-$sequel"
    }
    if (-not $NoVoice) { $targets += 'MEVR-Voice' }
    if (-not $SkipTests) { $targets += 'mevr-test-programs' }  # everything ctest runs (CMakeLists.txt)
    Invoke-Native { cmake --build $buildDir --config Release --parallel --target $targets }
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
    if (-not $NoRig) {
        # The moving bodies' builder, published self-contained (Rig.csproj's players' build), this version and commit in it.
        $rigVersion = "$Version+$head$(if ($dirty.Count -gt 0) { '-dirty' })"
        $rigOut = Join-Path $buildDir 'rig'
        if (Test-Path $rigOut) { Remove-Item -Recurse -Force $rigOut }
        Invoke-Native { dotnet publish (Join-Path $repo 'tools\rig\Rig.csproj') -c Release -p:MevrPlayers=true "-p:Version=$Version" "-p:InformationalVersion=$rigVersion" -o $rigOut -nologo -v q }
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet publish of tools/rig failed: it needs the .NET 10 SDK and Legendary Explorer's nightly in mod-manager\ (Rig.csproj's LexDir), or pass -NoRig"
        }
    }
}

$out = Join-Path $buildDir 'Release'
foreach ($sequel in $sequels) { Assert-SequelSdkFree $sequel }  # with -NoBuild too, before anything else about the sequel
# The moving bodies' builder (with -NoBuild too: an earlier publish of this version and commit).
$rigOut = Join-Path $buildDir 'rig'
if (-not $NoRig) {
    $rigDll = Join-Path $rigOut 'mevr-rig.dll'
    if (-not (Test-Path (Join-Path $rigOut 'mevr-rig.exe')) -or -not (Test-Path $rigDll)) {
        throw "Not published: $rigOut\mevr-rig.exe (package without -NoBuild, which publishes tools/rig, or pass -NoRig)"
    }
    $rigInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($rigDll)
    if ($rigInfo.ProductVersion -ne "$Version+$head" -and -not ($AllowDirty -and $rigInfo.ProductVersion -eq "$Version+$head-dirty")) {
        Stop-Packaging "mevr-rig was published as $($rigInfo.ProductVersion), not $Version+$head (HEAD): package without -NoBuild"
    }
    if (Get-ChildItem $rigOut -Recurse -File -Filter 'oo2core*' -ErrorAction SilentlyContinue) {
        Stop-Packaging "$rigOut has an Oodle DLL (RAD's, never ours to ship): the tool loads the game's own"
    }
}
$binaries = @('LE1MEVR.asi', 'bink2w64.dll', 'MEVR-Setup.exe')
foreach ($sequel in $sequels) { $binaries += "${sequel}MEVR.asi" }
if (-not $NoVoice) { $binaries += 'MEVR-Voice.dll' }
foreach ($f in $binaries) {
    if (-not (Test-Path (Join-Path $out $f))) { throw "Not built: $(Join-Path $out $f)" }
}

# ---- The binaries are this version, built from this commit ---------------------------------------------------------
# Their version resources (src/plugin/Plugin.rc, src/loader/Loader.rc, tools/setup/Setup.rc): FileVersion is CMake's
# version, ProductVersion <version>+<commit> (tools/cmake/BuildInfo.cmake; "-dirty" for a build with changed files).
foreach ($f in $binaries) {
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $out $f))
    $numbers = '{0}.{1}.{2}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart
    if ($info.FileVersion -ne $Version -or $numbers -ne $Version) {
        Stop-Packaging "$f says version '$($info.FileVersion)' ($numbers), CMake's is ${Version}: rebuild (without -NoBuild)"
    }
    $built = $info.ProductVersion
    if ($built -ne "$Version+$head" -and -not ($AllowDirty -and $built -eq "$Version+$head-dirty")) {
        Stop-Packaging "$f was built as $built, not $Version+$head (HEAD): rebuild (without -NoBuild)"
    }
}

# The hard gate: LExSDKv2 has no licence, so a build that links it can't be given to anyone without its author's
# permission. Each game's ASI project says whether it links the SDK (the LESDK-<game> library, or no MEVR_NO_LESDK
# define: CMakeLists.txt's mevr_configure sets exactly one of the two); no project to read counts as linking it. LE1
# always; LE2 and LE3 whenever this build tree is configured with them or holds their ASI, packaged yet or not, so a
# sequel can never reach the payload from a tree that links the SDK (T04, docs/trilogy-plan.md).
function Test-LinksLexSdk([string]$Game) {
    $project = Join-Path $buildDir "MEVR-$Game.vcxproj"
    if (-not (Test-Path $project)) { return $true }
    $text = [IO.File]::ReadAllText($project)
    return $text.Contains("LESDK-$Game") -or -not $text.Contains('MEVR_NO_LESDK')
}
$sdkGames = @('LE1')
foreach ($game in @('LE2', 'LE3')) {
    if ((Test-Path (Join-Path $buildDir "MEVR-$game.vcxproj")) -or (Test-Path (Join-Path $out "${game}MEVR.asi"))) { $sdkGames += $game }
}
$linksSdk = $false
foreach ($game in $sdkGames) {
    if (-not (Test-LinksLexSdk $game)) { continue }
    $linksSdk = $true
    if (-not $LexSdkPermission) {
        throw ("Refusing to package: ${game}MEVR.asi is built with LExSDKv2, which has no licence. Distributing it needs its " +
            "author's written permission (docs/release-readme.md, research/33). Once you have it, run again with " +
            "-LexSdkPermission; or configure $buildDir without it (MEVR_${game}_LESDK=OFF, the default of a monolithic build; " +
            "the LE2 diagnostic host, MEVR_LE2_DIAGNOSTIC, always links it and is never packaged).")
    }
    Write-Warning "LExSDKv2 is compiled into ${game}MEVR.asi: packaging under -LexSdkPermission (its author agreed)."
}

# ---- The tests -----------------------------------------------------------------------------------------------------
if ($SkipTests) {
    Write-Warning 'ctest skipped (-SkipTests): not for release.'
} else {
    Invoke-Native { ctest -C Release --test-dir $buildDir --output-on-failure }
    if ($LASTEXITCODE -ne 0) { Stop-Packaging "ctest failed in $buildDir (above)" }
}

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
    'OpenVR-Valve-BSD-3-Clause.txt' = 'LICENSES\BSD-3-Clause-Valve.txt'
    'OpenXR-Loader-jsoncpp.txt'      = 'external\OpenXR-SDK\src\external\jsoncpp\LICENSE'
    'Rajdhani-OFL.txt'               = 'third_party\fonts\Rajdhani-OFL.txt'
    'TitilliumWeb-OFL.txt'           = 'third_party\fonts\TitilliumWeb-OFL.txt'
    'Lucide-ISC.txt'                 = 'third_party\lucide\LICENSE.txt'
}
# MEVR-Voice.dll carries Moonshine Voice's static library (MIT, its LICENSE with its own third-party notices).
if (-not $NoVoice) { $licences['MoonshineVoice-MIT.txt'] = 'external\moonshine-voice\LICENSE' }
# OpenAL Soft (research/69): built from its pinned source release once (tools/fetch-openal-soft.ps1), its licence texts and
# that source shipped with it (LGPL-2.0-or-later: the source from the same place as the binary).
$soft = Join-Path $repo 'external\openal-soft'
if (-not $NoAudio) {
    if (-not (Test-Path (Join-Path $soft 'soft_oal.dll'))) {
        Invoke-Native { & (Join-Path $repo 'tools\fetch-openal-soft.ps1') }
        if (-not (Test-Path (Join-Path $soft 'soft_oal.dll'))) { throw 'tools/fetch-openal-soft.ps1 failed: no OpenAL Soft (or pass -NoAudio)' }
    }
    $softVersion = (Get-Content (Join-Path $soft 'VERSION.txt') -Raw).Trim()
    $licences['OpenAL-Soft-LGPL-2.0.txt'] = 'external\openal-soft\COPYING'
    $licences['OpenAL-Soft-pffft.txt'] = 'external\openal-soft\LICENSE-pffft'
    $licences['OpenAL-Soft-BSD-3-Clause.txt'] = 'external\openal-soft\BSD-3Clause'
    $licences["openal-soft-$softVersion.tar.bz2"] = "external\openal-soft\openal-soft-$softVersion.tar.bz2"
}
# The moving bodies' builder: Legendary Explorer's library (GPL-3.0, GPL-3.0.txt above; its source: github.com/ME3Tweaks/
# LegendaryExplorer, NOTICE.md), the libraries it loads and the .NET runtime, as its publish put them in its licenses\.
if (-not $NoRig) {
    foreach ($name in 'Serilog-Apache-2.0.txt', 'Newtonsoft.Json-MIT.txt', 'CommunityToolkit-MIT.txt', 'RecyclableMemoryStream-MIT.txt',
        'dotnet-runtime-MIT.txt', 'dotnet-runtime-THIRD-PARTY-NOTICES.txt') {
        $licences["rig-$name"] = "build-mono\rig\licenses\$name"
    }
}
foreach ($source in $licences.Values) {
    if (-not (Test-Path (Join-Path $repo $source))) { throw "Licence text missing: $source (fill external\ and third_party\ first)" }
}
$pdbs = @('LE1MEVR.pdb', 'bink2w64.pdb', 'MEVR-Setup.pdb')
foreach ($sequel in $sequels) { $pdbs += "${sequel}MEVR.pdb" }
if (-not $NoVoice) { $pdbs += 'MEVR-Voice.pdb' }
# The sequels' renderer settings the package carries (GPL-3.0 files of src/halcyon, shipped as they are).
$rendererInis = @{ LE2 = 'src\halcyon\le2\runtime\MELE2VR.ini'; LE3 = 'src\halcyon\le3\runtime\MELE3VR.ini' }
foreach ($f in $pdbs) {
    if (-not (Test-Path (Join-Path $out $f))) { throw "No $f in $out (the release binaries link with /DEBUG): rebuild" }
}

$dist = Join-Path $repo "dist\MEVR-$Version"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
$payload = Join-Path $dist 'payload'
New-Item -ItemType Directory -Force $payload | Out-Null

Copy-Item (Join-Path $out 'MEVR-Setup.exe') $dist
Copy-Item (Join-Path $out 'LE1MEVR.asi') $payload
foreach ($sequel in $sequels) {
    Copy-Item (Join-Path $out "${sequel}MEVR.asi") $payload
    $rendererIni = Join-Path $repo $rendererInis[$sequel]
    if (Test-Path $rendererIni) {
        Copy-Item $rendererIni $payload
        Write-Output "${sequel}: payload\${sequel}MEVR.asi and payload\$(Split-Path -Leaf $rendererIni)"
    } else {
        Write-Output "${sequel}: payload\${sequel}MEVR.asi (no release settings file for its renderer: it writes its own)"
    }
}
Copy-Item (Join-Path $out 'bink2w64.dll') $payload
Copy-Item -Recurse (Join-Path $repo 'third_party\fonts') (Join-Path $payload 'fonts')
Copy-Item -Recurse (Join-Path $repo 'assets\menu') (Join-Path $payload 'menu')
if ($NoVoice) {
    Write-Warning 'No MEVR-Voice.dll (-NoVoice): players of this package get no voice orders, even with the downloads.'
} else {
    $voiceDir = Join-Path $payload 'voice'
    New-Item -ItemType Directory -Force $voiceDir | Out-Null
    Copy-Item (Join-Path $out 'MEVR-Voice.dll') $voiceDir  # not onnxruntime.dll: the setup downloads it (install/Voice)
}
if ($NoAudio) {
    Write-Warning "No OpenAL Soft (-NoAudio): players of this package hear the game's own OpenAL (panning only)."
} else {
    $audioDir = Join-Path $payload 'audio'
    New-Item -ItemType Directory -Force $audioDir | Out-Null
    foreach ($f in 'soft_oal.dll', 'COPYING', 'LICENSE-pffft', 'BSD-3Clause', 'README.md') {
        Copy-Item (Join-Path $soft $f) $audioDir
    }
    Write-Output "OpenAL Soft ${softVersion}: payload\audio"
}
if ($NoRig) {
    Write-Warning 'No body builder (-NoRig): players of this package get no moving bodies and hair.'
} else {
    $rigDir = Join-Path $payload 'rig'
    New-Item -ItemType Directory -Force (Join-Path $rigDir 'plans') | Out-Null
    Get-ChildItem $rigOut | Where-Object { $_.Name -ne 'plans' } | Copy-Item -Destination $rigDir -Recurse
    # The plans the players' build runs: players.jsonc and the plans it names (never a development plan such as Liara's).
    $plansDir = Join-Path $repo 'tools\rig\plans'
    $playersText = (Get-Content (Join-Path $plansDir 'players.jsonc') | Where-Object { $_ -notmatch '^\s*//' }) -join "`n"
    $players = $playersText | ConvertFrom-Json
    $planFiles = @('players.jsonc')
    foreach ($g in $players.games.PSObject.Properties) { $planFiles += @($g.Value.bodies) + @($g.Value.also) }
    foreach ($f in ($planFiles | Where-Object { $_ } | Select-Object -Unique)) { Copy-Item (Join-Path $plansDir $f) (Join-Path $rigDir 'plans') }
    $rigBytes = (Get-ChildItem -Recurse -File $rigDir | Measure-Object Length -Sum).Sum
    Write-Output ("Body builder: payload\rig ({0} files, {1:N1} MB; plans {2})" -f @(Get-ChildItem -Recurse -File $rigDir).Count, ($rigBytes / 1MB), ($planFiles -join ', '))
}
if (-not $NoCockpit) {
    if (-not $CockpitPackage) { $CockpitPackage = Join-Path $repo 'build\cockpit-hull\BIOG_MEVR_Cockpit.pcc' }
    if (-not (Test-Path $CockpitPackage)) {
        throw "No cockpit package at $CockpitPackage (tools/cockpit/package build, research/48): pass -CockpitPackage, or -NoCockpit"
    }
    # Built from the current models? The last commit to its sources (commit times: a fresh checkout's file times are all
    # "now") and any uncommitted change under them, against the package's time.
    $packageTime = (Get-Item $CockpitPackage).LastWriteTimeUtc
    $cockpitSources = 'tools/cockpit', 'concept/assets/game-ready'
    $last = "$(Invoke-Native { git -C $repo log -1 --format='%ct %h %s' -- $cockpitSources })".Trim()
    if ($last -match '^(\d+) (\S+) (.*)$') {
        $sourceTime = [DateTimeOffset]::FromUnixTimeSeconds([long]$Matches[1]).UtcDateTime
        if ($sourceTime -gt $packageTime) {
            Write-Warning (("The cockpit package ({0:u}) is older than the last change to its sources: {1} ({2:u}, {3}). " +
                "Rebuild it (tools/cockpit/package, the tripo-game-ready skill) unless that change can't affect it.") -f
                $packageTime, $Matches[2], $sourceTime, $Matches[3])
        }
    }
    $uncommitted = @(Invoke-Native { git -C $repo status --porcelain -- $cockpitSources } | Where-Object { $_ })
    if ($uncommitted.Count -gt 0) {
        Write-Warning "The cockpit's sources have uncommitted changes the package may not have:`n  $(($uncommitted | Select-Object -First 10) -join "`n  ")"
    }
    $cockpitDir = Join-Path $payload 'cockpit'
    New-Item -ItemType Directory -Force $cockpitDir | Out-Null
    Copy-Item $CockpitPackage (Join-Path $cockpitDir 'BIOG_MEVR_Cockpit.pcc')
    Invoke-Native { python (Join-Path $repo 'tools\cockpit\place.py') --script (Join-Path $cockpitDir 'cockpit.txt') }
    if ($LASTEXITCODE -ne 0) { throw 'place.py --script failed (it reads the hull manifest under concept/assets)' }
    Write-Output ("Cockpit: {0} ({1:N1} MB)" -f $CockpitPackage, ((Get-Item $CockpitPackage).Length / 1MB))
}
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
$ini = "; MEVR Setup's settings.`r`n[Update]`r`n; The GitHub repository whose Releases the setup checks (https://github.com/OWNER/REPO turns the check off).`r`nrepo = https://github.com/dnehring/MEVR`r`n" +
    "`r`n[Voice]`r`n; The voice orders' fine-tuned model (Optional content, research/33 section 14): the asset modelAsset of the release`r`n" +
    "; modelTag in [Update] repo (or modelRepo). modelUrl = https://... downloads that instead (checked against modelSha256 and`r`n" +
    "; modelSize when given). Until the release exists the setup installs the rest and says the model isn't published yet.`r`n" +
    ";modelTag = voice-model-1`r`n;modelAsset = model.gguf`r`n"
if ($sequels.Count -gt 0) {
    # The one switch that makes MEVR Setup offer a sequel (install/Game.hpp OfferedGames): without this key, Mass Effect 1 only.
    $ini += "`r`n[Setup]`r`n; The games this release offers besides Mass Effect 1 (their mods are in payload\).`r`ngames = $((@('LE1') + $sequels) -join ', ')`r`n"
    Write-Warning "This package offers $($sequels -join ' and ') to players (MEVR-Setup.ini [Setup] games): the owner decides when a sequel ships."
}
[IO.File]::WriteAllText((Join-Path $dist 'MEVR-Setup.ini'), $ini)

# The symbols: the PDBs and the exact binaries they belong to, next to the players' folder.
$symbols = Join-Path $repo "dist\MEVR-$Version-symbols"
if (Test-Path $symbols) { Remove-Item -Recurse -Force $symbols }
New-Item -ItemType Directory -Force $symbols | Out-Null
foreach ($f in $pdbs + $binaries) {
    Copy-Item (Join-Path $out $f) $symbols
}
$symbolsNote = "MEVR $Version (commit $head): the PDBs of the release's binaries, and the binaries they match.`r`n" +
    "For symbolising the stacks in players' crash reports (crash-breadcrumbs.txt, minidumps): point the debugger's symbol`r`n" +
    "path here. Not for players; never put these in the players' zip.`r`n"
[IO.File]::WriteAllText((Join-Path $symbols 'README.txt'), $symbolsNote)

Write-Output "Release folder: $dist"
Get-ChildItem -Recurse -File $dist | ForEach-Object {
    '{0,-40} {1,12:N0}  {2}' -f $_.FullName.Substring($dist.Length + 1), $_.Length, (Get-FileHash $_.FullName).Hash.Substring(0, 16)
}
Write-Output "Symbols: $symbols"

if ($Zip) {
    $zipPath = "$dist.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath }
    Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zipPath
    Write-Output ("Zip: {0} ({1:N1} MB, SHA-256 {2})" -f $zipPath, ((Get-Item $zipPath).Length / 1MB), (Get-FileHash $zipPath).Hash)
    $symbolsZip = "$symbols.zip"
    if (Test-Path $symbolsZip) { Remove-Item $symbolsZip }
    Compress-Archive -Path (Join-Path $symbols '*') -DestinationPath $symbolsZip
    $symbolsHash = (Get-FileHash $symbolsZip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$symbols.sha256.txt", ('{0}  {1}' -f $symbolsHash, (Split-Path -Leaf $symbolsZip)) + "`n")
    Write-Output ("Symbols zip: {0} ({1:N1} MB): for your archive, not for the release page (keep it somewhere durable)" -f
        $symbolsZip, ((Get-Item $symbolsZip).Length / 1MB))

    # Checksums of what the release page gets, as sha256sum writes them (sha256sum -c MEVR-<version>-SHA256SUMS.txt).
    $sums = foreach ($file in @($zipPath)) {
        '{0}  {1}' -f (Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path -Leaf $file)
    }
    $sumsPath = Join-Path $repo "dist\MEVR-$Version-SHA256SUMS.txt"
    [IO.File]::WriteAllText($sumsPath, (($sums -join "`n") + "`n"))
    Write-Output "Checksums: $sumsPath"

    # The release notes: the template with this version's CHANGELOG.md entry (between its '## ' heading and the next).
    $changelog = @(Get-Content (Join-Path $repo 'CHANGELOG.md'))
    $start = -1
    for ($i = 0; $i -lt $changelog.Count; $i++) {
        if ($changelog[$i] -match ('^##\s+\[?v?' + [regex]::Escape($Version) + '\]?(\s|$)')) { $start = $i; break }
    }
    $changes = "(CHANGELOG.md has no entry for ${Version}: write one.)"
    if ($start -lt 0) {
        Write-Warning "CHANGELOG.md has no '## $Version' entry: the release notes say so."
    } else {
        if ($changelog[$start] -match '(?i)unreleased') { Write-Warning "CHANGELOG.md's $Version entry still says unreleased: date it." }
        $end = $changelog.Count
        for ($i = $start + 1; $i -lt $changelog.Count; $i++) {
            if ($changelog[$i] -match '^##\s') { $end = $i; break }
        }
        $changes = if ($end - 1 -ge $start + 1) { (($changelog[($start + 1)..($end - 1)]) -join "`n").Trim() } else { '' }
    }
    $notes = Get-Content -Raw (Join-Path $repo '.github\release-notes-template.md')
    $notes = $notes -replace '(?s)^<!--.*?-->\s*', ''  # the template's own comment
    $values = [ordered]@{
        '{{version}}'        = $Version
        '{{commit}}'         = $head
        '{{changes}}'        = $changes
        '{{zip}}'            = (Split-Path -Leaf $zipPath)
        '{{zip_sha256}}'     = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    foreach ($key in $values.Keys) { $notes = $notes.Replace($key, $values[$key]) }
    $notesPath = Join-Path $repo "dist\MEVR-$Version-release-notes.md"
    [IO.File]::WriteAllText($notesPath, $notes)
    Write-Output "Release notes: $notesPath"
}
