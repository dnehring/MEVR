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
#   dist\MEVR-<version>-symbols\
#     *.pdb and the binaries they match (LE1MEVR.asi, bink2w64.dll, MEVR-Setup.exe, MEVR-Voice.dll), to symbolise the stacks in players'
#     crash reports. Kept next to the players' folder, never inside it (the binaries name their PDB without a path:
#     /PDBALTPATH, CMakeLists.txt).
#   with -Zip: dist\MEVR-<version>.zip, dist\MEVR-<version>-symbols.zip, dist\MEVR-<version>-SHA256SUMS.txt and
#     dist\MEVR-<version>-release-notes.md (.github/release-notes-template.md with this version's CHANGELOG.md entry).
#
#   tools\package-release.ps1 [-Version 0.1.0] [-Tag v0.1.0] [-NoBuild] [-Zip] [-AllowDirty] [-SkipTests]
#                             [-LexSdkPermission] [-CockpitPackage <pcc>] [-NoCockpit] [-NoVoice]
#
# The version is CMake's: project(MEVR VERSION x.y.z) in CMakeLists.txt is the only place it is written. -Version only
# confirms it. It refuses to package (docs/release-checklist.md section 5):
#   - a changed tracked file, or a release submodule (OpenXR-SDK, RmlUi, freetype) not at the commit's pointer, unless
#     -AllowDirty (the binaries then say <commit>-dirty);
#   - another version anywhere: -Version or -Tag (the release workflow's) other than CMake's, a v* tag on HEAD other than
#     v<version>, a binary whose version resource names another version or another commit than HEAD;
#   - a source file without its SPDX line (python tools/check-spdx.py);
#   - a failing ctest in build-mono, unless -SkipTests;
#   - an ASI that links LExSDKv2 (no licence), unless -LexSdkPermission says its author agreed;
#   - a missing licence text, a missing cockpit package (unless -NoCockpit), or no MEVR-Voice.dll (unless -NoVoice).
# It warns when HEAD has no tag v<version> and when the cockpit package is older than its sources (tools/cockpit,
# concept/assets/game-ready: rebuild it with tools/cockpit/package). Texture packs are never bundled.
#
# LExSDKv2 has no licence: the release build (build-mono, MEVR_LE1_LESDK=OFF since 2026-10-26) doesn't link it.
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
    [switch]$NoVoice
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
    if (-not $NoVoice) { $targets += 'MEVR-Voice' }
    if (-not $SkipTests) { $targets += 'mevr-test-programs' }  # everything ctest runs (CMakeLists.txt)
    Invoke-Native { cmake --build $buildDir --config Release --parallel --target $targets }
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

$out = Join-Path $buildDir 'Release'
$binaries = @('LE1MEVR.asi', 'bink2w64.dll', 'MEVR-Setup.exe')
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
# permission. MEVR-LE1's project says whether it links the SDK (LESDK-LE1); no project to read counts as linking it.
$asiProject = Join-Path $buildDir 'MEVR-LE1.vcxproj'
$linksSdk = -not (Test-Path $asiProject) -or (Select-String -Path $asiProject -Pattern 'LESDK-LE1' -SimpleMatch -Quiet)
if ($linksSdk -and -not $LexSdkPermission) {
    throw ("Refusing to package: LE1MEVR.asi is built with LExSDKv2, which has no licence. Distributing it needs its " +
        "author's written permission (docs/release-readme.md, research/33). Once you have it, run again with " +
        "-LexSdkPermission; or finish removing LExSDKv2 (research/34) first.")
}
if ($linksSdk) { Write-Warning 'LExSDKv2 is compiled into LE1MEVR.asi: packaging under -LexSdkPermission (its author agreed).' }

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
    'OpenXR-Loader-jsoncpp.txt'      = 'external\OpenXR-SDK\src\external\jsoncpp\LICENSE'
    'Rajdhani-OFL.txt'               = 'third_party\fonts\Rajdhani-OFL.txt'
    'TitilliumWeb-OFL.txt'           = 'third_party\fonts\TitilliumWeb-OFL.txt'
    'Lucide-ISC.txt'                 = 'third_party\lucide\LICENSE.txt'
}
# MEVR-Voice.dll carries Moonshine Voice's static library (MIT, its LICENSE with its own third-party notices).
if (-not $NoVoice) { $licences['MoonshineVoice-MIT.txt'] = 'external\moonshine-voice\LICENSE' }
foreach ($source in $licences.Values) {
    if (-not (Test-Path (Join-Path $repo $source))) { throw "Licence text missing: $source (fill external\ and third_party\ first)" }
}
$pdbs = @('LE1MEVR.pdb', 'bink2w64.pdb', 'MEVR-Setup.pdb')
if (-not $NoVoice) { $pdbs += 'MEVR-Voice.pdb' }
foreach ($f in $pdbs) {
    if (-not (Test-Path (Join-Path $out $f))) { throw "No $f in $out (the release binaries link with /DEBUG): rebuild" }
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
if ($NoVoice) {
    Write-Warning 'No MEVR-Voice.dll (-NoVoice): players of this package get no voice orders, even with the downloads.'
} else {
    $voiceDir = Join-Path $payload 'voice'
    New-Item -ItemType Directory -Force $voiceDir | Out-Null
    Copy-Item (Join-Path $out 'MEVR-Voice.dll') $voiceDir  # not onnxruntime.dll: the setup downloads it (install/Voice)
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
    Write-Output ("Symbols zip: {0} ({1:N1} MB)" -f $symbolsZip, ((Get-Item $symbolsZip).Length / 1MB))

    # Checksums, as sha256sum writes them (sha256sum -c MEVR-<version>-SHA256SUMS.txt checks them).
    $sums = foreach ($file in $zipPath, $symbolsZip) {
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
        '{{symbols_zip}}'    = (Split-Path -Leaf $symbolsZip)
        '{{symbols_sha256}}' = (Get-FileHash $symbolsZip -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    foreach ($key in $values.Keys) { $notes = $notes.Replace($key, $values[$key]) }
    $notesPath = Join-Path $repo "dist\MEVR-$Version-release-notes.md"
    [IO.File]::WriteAllText($notesPath, $notes)
    Write-Output "Release notes: $notesPath"
}
