# SPDX-License-Identifier: MIT
# Tests MEVR-Setup.exe's command line against a FAKE game folder (research/33); never the real game.
#
# Builds scratch\fakegame\Mass Effect Legendary Edition with a stand-in MassEffect1.exe (the real version number,
# tools/setup/FakeGame), a copy of RAD's bink2w64 DLL (the real game's bink2w64_original.dll, read only, or -BinkDll)
# as bink2w64.dll, a copy of the game's PCConsoleTOC.bin (the one before the cockpit's test install, if kept, else the
# game's), and small GamerSettings.ini / BIOEngine.ini files; then runs status, install, a tamper + repair,
# restore and apply of the game settings, and uninstall, and checks the folder is byte for byte what it was.
#
#   tools\setup\test-fake-game.ps1 [-Build build] [-Payload <folder>] [-BinkDll <path>] [-LauncherBinkDll <path>] [-Voice]
#                                  [-Games LE1,LE2,LE3] [-Setup <MEVR-Setup.exe>] [-Trace <folder>]
#
# The three games (docs/trilogy-plan.md T52): the fake Legendary Edition has Game\ME2 and Game\ME3 beside Game\ME1 (the
# stand-in exe under each game's name, RAD's Bink of the launcher's build, a GamerSettings.ini), and after Mass Effect 1's
# steps the same steps run for each sequel in -Games with --for (install, "verify files" and Repair, the game settings,
# an update, the report, the renderer's own writes, uninstall byte for byte, upstream's MELE2-VR, a lost record, a game
# never started), then the three installed together: the launcher's loader, which they share, stays until the last one
# goes. The payload gets LE2MEVR.asi / LE3MEVR.asi (build-trilogy's, else stand-ins), upstream's MELE2VR.ini and ME3's own
# MELE3VR.ini (src/halcyon/le3/runtime, since 2026-11-17), as tools/package-release.ps1 -Games puts them.
#
# -Setup runs another MEVR-Setup.exe (an older build) through the same steps, and -Trace writes, after every command, its
# output, every file's hash and the install record (times and the setup copy's own hash masked) into that folder: two
# traces of -Games LE1, one from the build before a change and one after, must be the same files (the proof that Mass
# Effect 1's install is byte for byte what it was).
#
# The payload carries MEVR-Voice.dll when it's built (the voice orders, install/Voice): Install puts it in ASI\voice.
# -Voice also runs the voice orders' downloads for real (research/33 section 14: Moonshine Voice's release and the tiny
# models from GitHub and Hugging Face, about 71 MB, and GitHub's answer for the order model's release), checks them by
# SHA-256, and leaves them for the uninstall's byte-for-byte check. Needs the internet; nothing else here does.
#
# The fake game also has Game\Launcher (research/33 section 12): a stand-in MassEffectLauncher.exe and a RAD Bink DLL of
# the launcher's build (LE2's own bink2w64.dll, read only: the same 2.7s build, or -LauncherBinkDll), where Install puts
# our loader too (it boots Mass Effect 1 for Launch's -game 1).
param(
    [string]$Build = (Join-Path $PSScriptRoot '..\..\build'),
    [string]$Payload,
    [string]$BinkDll,
    [string]$LauncherBinkDll,
    [switch]$Voice,
    [string[]]$Games = @('LE1', 'LE2', 'LE3'),
    [string]$Setup,
    [string]$Trace
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Build = (Resolve-Path $Build).Path
$scratch = Join-Path $repo 'scratch'
$root = Join-Path $scratch 'fakegame\Mass Effect Legendary Edition'
$win64 = Join-Path $root 'Game\ME1\Binaries\Win64'
$config = Join-Path $root 'Game\ME1\BioGame\Config'
$launcher = Join-Path $root 'Game\Launcher'
$setup = if ($Setup) { (Resolve-Path $Setup).Path } else { Join-Path $Build 'Release\MEVR-Setup.exe' }
# The %LOCALAPPDATA% every MEVR Setup this test starts sees (Run-Setup): its settings.ini is the test's.
$setupLocal = Join-Path $scratch 'setup-local'
Remove-Item -Recurse -Force $setupLocal -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $setupLocal 'MEVR') | Out-Null
$failures = 0
# -Games LE2,LE3 arrives as one word through powershell -File.
$Games = @($Games | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToUpper() } | Where-Object { $_ })
foreach ($g in $Games) { if ($g -notin 'LE1', 'LE2', 'LE3') { throw "-Games: LE1, LE2 or LE3, not '$g'" } }
$Games = @('LE1', 'LE2', 'LE3' | Where-Object { $Games -contains $_ })
$script:For = $null    # --for <game> on every command (the sequels' steps)
$script:Step = 0
if ($Trace) {
    Remove-Item -Recurse -Force $Trace -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $Trace | Out-Null
    $Trace = (Resolve-Path $Trace).Path
}

function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Output "ok   $what" } else { Write-Output "FAIL $what"; $script:failures++ }
}

function Run-Setup([string[]]$arguments) {
    $out = Join-Path $scratch 'setup-cli.txt'
    Remove-Item $out -ErrorAction SilentlyContinue
    $all = $arguments + @('--game', $root, '--payload', $Payload, '--out', $out)
    if ($script:For) { $all += @('--for', $script:For) }
    $quoted = ($all | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    # Setup on a %LOCALAPPDATA% of the test's own: never the player's settings.ini or calibration.ini (research/25 section 20).
    $realLocal = $env:LOCALAPPDATA
    $env:LOCALAPPDATA = $setupLocal
    try { $p = Start-Process -FilePath $setup -ArgumentList $quoted -Wait -PassThru -NoNewWindow }
    finally { $env:LOCALAPPDATA = $realLocal }
    $text = if (Test-Path $out) { Get-Content -Raw $out } else { '' }
    Write-Host "---- MEVR-Setup $($arguments -join ' ')$(if ($script:For) { " --for $($script:For)" }) (exit $($p.ExitCode))"
    Write-Host $text.TrimEnd()
    if ($Trace) { Write-Trace $arguments $p.ExitCode $text }
    return $p.ExitCode
}

# One step of the trace: the command, its output, every file under the fake game with its hash, and each game's install
# record. What differs between two builds without meaning anything is masked: times, the setup's copy of itself (its hash
# is the build's), the report's commit and sizes.
function Write-Trace([string[]]$arguments, [int]$exit, [string]$text) {
    $script:Step++
    $setupHash = (Get-FileHash $setup).Hash
    $mask = {
        param([string]$s)
        $s = $s -replace '\d{4}-\d{2}-\d{2}[T_ -]\d{2}[:-]?\d{2}([:-]\d{2})?([.-]\d+)?', '<time>'
        $s = $s -replace '(?i)\b[0-9a-f]{7,10}(-dirty)?\b(?=\))', '<commit>'
        $s = $s -replace ('(?i)' + $setupHash), '<setup>'
        $s -replace '\d+(\.\d+)? (KB|MB|bytes)', '<size>'
    }
    $lines = @("command: $($arguments -join ' ')$(if ($script:For) { " --for $($script:For)" })", "exit: $exit", '', '== output', (& $mask $text).TrimEnd(), '', '== files')
    $lines += Get-ChildItem -Recurse -File $root | Sort-Object FullName | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length)
        $hash = if ($rel -match '\\ASI\\MEVR-Setup\.exe$|\\ASI\\MEVR-install\.json$|\\Logs\\MEVRLoader\.log$') { '<varies>' } else { (Get-FileHash $_.FullName).Hash }
        "$rel $hash"
    }
    foreach ($record in Get-ChildItem -Recurse -File $root -Filter 'MEVR-install.json' | Sort-Object FullName) {
        $lines += @('', "== $($record.FullName.Substring($root.Length))", (& $mask (Get-Content -Raw $record.FullName)).TrimEnd())
    }
    # The report: REPORT.md from the zip it just wrote (the same masks).
    $dirAt = [array]::IndexOf($arguments, '--report-dir')
    if ($dirAt -ge 0 -and $dirAt + 1 -lt $arguments.Count) {
        $zip = Get-ChildItem (Join-Path $arguments[$dirAt + 1] 'MEVR-report-*.zip') -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($zip) {
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $archive = [IO.Compression.ZipFile]::OpenRead($zip.FullName)
            $lines += @('', '== the zip', (($archive.Entries | ForEach-Object { $_.FullName }) -join "`r`n"))
            $entry = $archive.Entries | Where-Object { $_.FullName -eq 'REPORT.md' }
            if ($entry) {
                $reader = New-Object IO.StreamReader($entry.Open())
                $lines += @('', '== REPORT.md', (& $mask $reader.ReadToEnd()).TrimEnd())
                $reader.Dispose()
            }
            $archive.Dispose()
        }
    }
    $name = '{0:D3}-{1}.txt' -f $script:Step, ((($arguments | Where-Object { $_ -notmatch '[\\/]' }) -join '_') -replace '[^A-Za-z0-9_-]', '')
    [IO.File]::WriteAllText((Join-Path $Trace $name), ($lines -join "`r`n") + "`r`n")
}

# RAD's own Bink DLL among the candidates (a development install may have a proxy where RAD's file normally is).
function Find-RadBink([string[]]$candidates) {
    foreach ($c in $candidates) {
        if ((Test-Path $c) -and (Get-Item $c).VersionInfo.CompanyName -match 'RAD') { return $c }
    }
    throw "No RAD bink2w64 DLL among: $($candidates -join ', ') (pass -BinkDll / -LauncherBinkDll)"
}

function Snapshot {
    Get-ChildItem -Recurse -File $root | Sort-Object FullName | ForEach-Object {
        '{0} {1}' -f $_.FullName.Substring($root.Length), (Get-FileHash $_.FullName).Hash
    }
}

if (-not (Test-Path $setup)) { throw "Not built: $setup (cmake --build build --config Release --target MEVR-Setup mevr-fake-game MEVR-Loader)" }
$fakeExe = Join-Path $Build 'fake-game\Release\MassEffect1.exe'
if (-not (Test-Path $fakeExe)) { throw "Not built: $fakeExe" }
if (-not $BinkDll) {
    . (Join-Path $repo 'tools\common.ps1')
    $BinkDll = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE1') 'bink2w64_original.dll'
}
if (-not $LauncherBinkDll) {
    . (Join-Path $repo 'tools\common.ps1')
    # RAD's 2.7s build: LE2's own DLL (behind a loader when a development stretch is open), LE3's, or the launcher's.
    $real = (Find-MELEInstall).Root
    $LauncherBinkDll = Find-RadBink @(
        (Join-Path (Get-MELEBinDir $real 'LE2') 'bink2w64.dll'), (Join-Path (Get-MELEBinDir $real 'LE2') 'bink2w64_original.dll'),
        (Join-Path (Get-MELEBinDir $real 'LE3') 'bink2w64.dll'), (Join-Path (Get-MELEBinDir $real 'LE3') 'bink2w64_original.dll'),
        (Join-Path $real 'Game\Launcher\bink2w64_original.dll'), (Join-Path $real 'Game\Launcher\bink2w64.dll'))
}
if (-not $Payload) {
    $Payload = Join-Path $scratch 'payload'
    Remove-Item -Recurse -Force $Payload -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $Payload | Out-Null
    $asi = Join-Path $repo 'build-mono\Release\LE1MEVR.asi'
    if (-not (Test-Path $asi)) { $asi = Join-Path $Build 'Release\LE1MEVR.asi' }
    if (Test-Path $asi) {
        Copy-Item $asi $Payload
    } else {
        # No mod built here (a worktree that built only the setup): a stand-in, as the fake game never loads it.
        [IO.File]::WriteAllText((Join-Path $Payload 'LE1MEVR.asi'), 'stand-in LE1MEVR.asi (test-fake-game.ps1)')
    }
    # The sequels' mods (build-trilogy's, else stand-ins: the fake games never load them) and their renderer settings as the
    # release carries them (ME2: upstream's file; ME3: ours, src/halcyon/le3/runtime, since 2026-11-17).
    foreach ($sequel in 'LE2', 'LE3') {
        $sequelAsi = Join-Path $repo "build-trilogy\Release\${sequel}MEVR.asi"
        if (Test-Path $sequelAsi) { Copy-Item $sequelAsi $Payload } else {
            [IO.File]::WriteAllText((Join-Path $Payload "${sequel}MEVR.asi"), "stand-in ${sequel}MEVR.asi (test-fake-game.ps1)")
        }
    }
    Copy-Item (Join-Path $repo 'src\halcyon\le2\runtime\MELE2VR.ini') $Payload
    Copy-Item (Join-Path $repo 'src\halcyon\le3\runtime\MELE3VR.ini') $Payload
    Copy-Item (Join-Path $Build 'Release\bink2w64.dll') $Payload
    Copy-Item -Recurse (Join-Path $repo 'third_party\fonts') (Join-Path $Payload 'fonts')
    Copy-Item -Recurse (Join-Path $repo 'assets\menu') (Join-Path $Payload 'menu')
    $voiceDll = Join-Path $Build 'Release\MEVR-Voice.dll'
    if (Test-Path $voiceDll) {
        New-Item -ItemType Directory -Force (Join-Path $Payload 'voice') | Out-Null
        Copy-Item $voiceDll (Join-Path $Payload 'voice')
    }
    # The Mako's cockpit (research/47), when its package is built: into CookedPCConsole with a TOC entry. Without a built
    # package a stand-in of a few KB (the setup only needs its size for the TOC entry), so Repair's TOC entry is always tested.
    $cockpitPackage = Join-Path $repo 'build\cockpit-hull\BIOG_MEVR_Cockpit.pcc'
    New-Item -ItemType Directory -Force (Join-Path $Payload 'cockpit') | Out-Null
    if (Test-Path $cockpitPackage) {
        Copy-Item $cockpitPackage (Join-Path $Payload 'cockpit')
        python (Join-Path $repo 'tools\cockpit\place.py') --script (Join-Path $Payload 'cockpit\cockpit.txt') | Out-Null
    } else {
        [IO.File]::WriteAllBytes((Join-Path $Payload 'cockpit\BIOG_MEVR_Cockpit.pcc'), [byte[]](@(0xC1, 0x83, 0x2A, 0x9E) + (1..4092 | ForEach-Object { 0 })))
        [IO.File]::WriteAllText((Join-Path $Payload 'cockpit\cockpit.txt'), "# stand-in cockpit script (test-fake-game.ps1)`r`n")
    }
    # The moving bodies' builder (install/Bodies): the stand-in (tools/setup/FakeRig.cpp: small packages, their index and
    # bodies.json, as `mevr-rig bodies` writes them; the fake games have no packages to rig) with the real plans list.
    $fakeRig = Join-Path $Build 'fake-rig\Release\mevr-rig.exe'
    if (Test-Path $fakeRig) {
        New-Item -ItemType Directory -Force (Join-Path $Payload 'rig\plans') | Out-Null
        Copy-Item $fakeRig (Join-Path $Payload 'rig')
        Copy-Item (Join-Path $repo 'tools\rig\plans\players.jsonc') (Join-Path $Payload 'rig\plans')
    }
}
$withCockpit = Test-Path (Join-Path $Payload 'cockpit\BIOG_MEVR_Cockpit.pcc')
$withRig = Test-Path (Join-Path $Payload 'rig\mevr-rig.exe')
# One PCConsoleTOC.bin entry's bytes (install/Toc): a 28-byte header and the name, NUL-terminated, padded to 4.
function Toc-EntryBytes([string]$name) { 28 + [math]::Ceiling(($name.Length + 1) / 4) * 4 }
$withVoice = Test-Path (Join-Path $Payload 'voice\MEVR-Voice.dll')
if ($Voice -and -not $withVoice) { throw "-Voice needs MEVR-Voice.dll in the payload (build the MEVR-Voice target: tools/fetch-voice.ps1 first)" }

# The fake game.
. (Join-Path $repo 'tools\common.ps1')  # Find-MELEInstall: the sequels' TOCs below
Remove-Item -Recurse -Force (Join-Path $scratch 'fakegame') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $win64, $config, $launcher | Out-Null
Copy-Item $fakeExe $win64
Copy-Item $BinkDll (Join-Path $win64 'bink2w64.dll')
Copy-Item $fakeExe (Join-Path $launcher 'MassEffectLauncher.exe')
Copy-Item $LauncherBinkDll (Join-Path $launcher 'bink2w64.dll')
# The sequels: the stand-in exe under each game's name (the version resource is what counts), RAD's Bink of their build,
# a GamerSettings.ini as the game writes it, and a second ini file nothing of ours may touch.
$sequelGamer = "[SystemSettings]`r`nFullscreen=True`r`nResX=2560`r`nResY=1440`r`nUseVSync=True`r`nMotionBlur=True`r`nFilmGrain=True`r`nDynamicShadows=True`r`n`r`n[HDR]`r`nDesiredRange=DynamicRange_HDR`r`n"
foreach ($n in 2, 3) {
    $w = Join-Path $root "Game\ME$n\Binaries\Win64"
    $c = Join-Path $root "Game\ME$n\BioGame\Config"
    New-Item -ItemType Directory -Force $w, $c | Out-Null
    Copy-Item $fakeExe (Join-Path $w "MassEffect$n.exe")
    Copy-Item $LauncherBinkDll (Join-Path $w 'bink2w64.dll')
    [IO.File]::WriteAllText((Join-Path $c 'GamerSettings.ini'), $sequelGamer)
    [IO.File]::WriteAllText((Join-Path $c 'BIOEngine.ini'), "[Engine.GameEngine]`r`nSmoothFrameRate=TRUE`r`n`r`n[SystemSettings]`r`nDynamicShadows=True`r`n")
    # Its own TOC (read only: the one tools/rig/install.ps1 kept before a development install, else the game's), where the
    # moving bodies get their entries.
    $sequelToc = Join-Path $env:LOCALAPPDATA "MEVR\rig-install\LE$n\PCConsoleTOC.bin.orig"
    if (-not (Test-Path $sequelToc)) { $sequelToc = Join-Path (Find-MELEInstall).Root "Game\ME$n\BioGame\PCConsoleTOC.bin" }
    Copy-Item $sequelToc (Join-Path $root "Game\ME$n\BioGame\PCConsoleTOC.bin")
}
$gameToc = Join-Path $env:LOCALAPPDATA 'MEVR\cockpit-install\PCConsoleTOC.bin.original'
if (-not (Test-Path $gameToc)) { $gameToc = Join-Path (Split-Path (Split-Path $BinkDll)) '..\BioGame\PCConsoleTOC.bin' }
$toc = Join-Path $root 'Game\ME1\BioGame\PCConsoleTOC.bin'
Copy-Item $gameToc $toc
$gamer = "[SystemSettings]`r`nFullscreen=True`r`nResX=2560`r`nResY=1440`r`nUseVSync=True`r`nMotionBlur=True`r`nFilmGrain=True`r`nDynamicShadows=True`r`n`r`n[HDR]`r`nDesiredRange=DynamicRange_HDR`r`n"
[IO.File]::WriteAllText((Join-Path $config 'GamerSettings.ini'), $gamer)
# BIOEngine.ini's [SystemSettings] as the game writes it (its copy of the live settings, which it rewrites during play:
# research/33 section 13).
$engineText = "[Engine.GameEngine]`r`nSmoothFrameRate=TRUE`r`nMaxSmoothedFrameRate=62`r`n`r`n" +
    "[SystemSettings]`r`nDynamicShadows=True`r`nMotionBlur=True`r`nUseVsync=True`r`nFullscreen=False`r`nMaxAnisotropy=16`r`nResX=2560`r`nResY=1440`r`n" +
    "FoliageDrawRadiusMultiplier=1.000000`r`n`r`n[HDR]`r`nDesiredRange=DynamicRange_Auto`r`n"
[IO.File]::WriteAllText((Join-Path $config 'BIOEngine.ini'), $engineText)
$before = Snapshot
# A real game of one of the three running elsewhere (another session's run) can fail a step here: Setup counts a
# MassEffect<n>.exe or launcher process whose path it can't read as this folder's, on the safe side, and refuses.
$live = @(Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue | ForEach-Object { $_.Name } | Select-Object -Unique)
if ($live.Count -gt 0) { Write-Warning "$($live -join ', ') is running: a step for that game may be refused with 'Mass Effect (or its launcher) is running'. Run this again once it has closed." }
$ErrorActionPreference = 'Continue'  # from here on a failed check is reported, not thrown

if ($Games -contains 'LE1') {
Check ((Run-Setup @('--status')) -eq 0) 'status'
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'performance')) -eq 0) 'install'
$bink = (Get-Item (Join-Path $win64 'bink2w64.dll')).VersionInfo
Check ($bink.FileDescription -like 'MEVR ASI loader*') 'bink2w64.dll is our loader'
Check ((Get-FileHash (Join-Path $win64 'bink2w64_original.dll')).Hash -eq (Get-FileHash $BinkDll).Hash) 'RAD''s DLL kept as bink2w64_original.dll'
Check (Test-Path (Join-Path $win64 'ASI\LE1MEVR.asi')) 'the ASI'
Check ((Get-Item (Join-Path $launcher 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*') 'Game\Launcher\bink2w64.dll is our loader (its autoboot)'
Check ((Get-FileHash (Join-Path $launcher 'bink2w64_original.dll')).Hash -eq (Get-FileHash $LauncherBinkDll).Hash) 'the launcher''s own Bink kept as bink2w64_original.dll'
Check ((Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')) -match '(?m)^launcher: our loader starts Mass Effect 1') 'status: the launcher boots the game'
Check (Test-Path (Join-Path $win64 'ASI\menu\main.rml')) 'the menu'
Check (Test-Path (Join-Path $win64 'ASI\fonts\Rajdhani-Bold.ttf')) 'the fonts'
Check (Test-Path (Join-Path $win64 'ASI\MEVR-Setup.exe')) 'the setup copy (for uninstall and optional content)'
Check (Test-Path (Join-Path $win64 'ASI\MEVR-install.json')) 'the manifest'
if ($withVoice) { Check (Test-Path (Join-Path $win64 'ASI\voice\MEVR-Voice.dll')) 'the voice orders'' MEVR-Voice.dll' }
Check ((Get-Content -Raw (Join-Path $win64 'steam_appid.txt')) -eq '1328670') 'steam_appid.txt'
$melevr = Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')
Check ($melevr -match '(?m)^menuKey = 220' -and $melevr -match '(?m)^sfr2ResX = 3072' -and $melevr -match '(?m)^combatHeadAim = 0') 'MELEVR.ini: release file, menu key, no head aim, the target'
$g = Get-Content -Raw (Join-Path $config 'GamerSettings.ini')
Check ($g -match '(?m)^ResX=3072' -and $g -match '(?m)^ResY=3188' -and $g -match '(?m)^BorderlessWindow=True' -and $g -match '(?m)^UseVSync=False' -and $g -match 'DynamicRange_SDR') 'GamerSettings.ini: the VR values'
Check ((Get-Content -Raw (Join-Path $config 'BIOEngine.ini')) -match 'SmoothFrameRate=FALSE') 'BIOEngine.ini: no 60 fps cap'
$e = Get-Content -Raw (Join-Path $config 'BIOEngine.ini')
Check ($e -match '(?m)^ResX=3072\r?$' -and $e -match '(?m)^ResY=3188\r?$' -and $e -match '(?m)^DynamicShadows=False' -and $e -match '(?m)^MotionBlur=False' -and
    $e -match '(?m)^FoliageDrawRadiusMultiplier=0\.500000' -and $e -match '(?m)^UseVsync=True' -and $e -match 'DynamicRange_Auto') 'BIOEngine.ini: the values the game copies there recorded, UseVsync and [HDR] left'
if ($withCockpit) {
    Check (Test-Path (Join-Path $root 'Game\ME1\BioGame\CookedPCConsole\BIOG_MEVR_Cockpit.pcc')) 'the cockpit''s package'
    Check (Test-Path (Join-Path $win64 'ASI\cockpit\cockpit.txt')) 'the cockpit''s script'
    $tocInstalled = (Get-FileHash $toc).Hash
    $tocGrows = 76 + $(if ($withRig) { Toc-EntryBytes 'BioGame\CookedPCConsole\MEVR_Bodies_LE1.pcc' } else { 0 })
    Check ($tocInstalled -ne (Get-FileHash $gameToc).Hash -and (Get-Item $toc).Length -eq (Get-Item $gameToc).Length + $tocGrows) "PCConsoleTOC.bin: the cockpit's entry more$(if ($withRig) { ' and the moving bodies''' })"
}
if ($withRig) {
    $bodiesLocal = Join-Path $setupLocal 'MEVR\rig\LE1'
    Check ((Get-Item (Join-Path $root 'Game\ME1\BioGame\CookedPCConsole\MEVR_Bodies_LE1.pcc')).Length -eq 4096) 'the moving bodies: their package in CookedPCConsole'
    Check ((Test-Path (Join-Path $bodiesLocal 'MEVR_Bodies_LE1.json')) -and (Test-Path (Join-Path $bodiesLocal 'colliders\HMF_ARM_NKDa_MDL_MEVR.json')) -and
        -not (Test-Path (Join-Path $setupLocal 'MEVR\rig\LE1.build'))) 'the moving bodies: their index and spheres in %LOCALAPPDATA%\MEVR\rig\LE1'
    Check ((Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')) -match '(?m)^moving bodies: built \(MEVR_Bodies_LE1\.pcc; 1 bodies\)') 'status: the moving bodies built'
    Check ((Get-Content -Raw (Join-Path $Payload 'rig\fake-rig.log')) -match "(?m)^call: bodies \| --game \| LE1 \| --game-root \| $([regex]::Escape($root)) \| --out \| ") 'the builder: run for LE1 on this game folder'
}

# The player tunes Halcyon in game (a value we never touch): it must survive everything below.
$melevr = $melevr -replace '(?m)^sfr2HalfEyeUU = .*$', 'sfr2HalfEyeUU = 2.9'
if ($melevr -notmatch 'sfr2HalfEyeUU') { $melevr = $melevr -replace '\[profile:Default\]\r\n', "[profile:Default]`r`nsfr2HalfEyeUU = 2.9`r`n" }
[IO.File]::WriteAllText((Join-Path $win64 'ASI\MELEVR.ini'), $melevr)

# Steam's "verify files" puts RAD's DLL back (and the game's own TOC), and a menu file goes missing: Repair.
Copy-Item $BinkDll (Join-Path $win64 'bink2w64.dll') -Force
Copy-Item $LauncherBinkDll (Join-Path $launcher 'bink2w64.dll') -Force
Remove-Item (Join-Path $win64 'ASI\menu\main.rcss')
if ($withCockpit) { Copy-Item $gameToc $toc -Force }
Run-Setup @('--status') | Out-Null
$st = Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')
Check ($st -match '(?m)^needs repair: .*Game/Launcher/bink2w64\.dll') 'status after "verify files": the launcher''s loader needs repair'
Check ($st -match '(?m)^needs repair: .*bink2w64\.dll' -and $st -match '(?m)^needs repair: .*main\.rcss' -and (-not $withCockpit -or $st -match '(?m)^needs repair: .*PCConsoleTOC\.bin')) 'status after "verify files": the loader, the menu file and the TOC need repair'
Check ((Run-Setup @('--repair')) -eq 0) 'repair'
Run-Setup @('--status') | Out-Null
Check ((Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')) -match '(?m)^needs repair: nothing') 'status after repair: nothing to repair'
if ($withCockpit) { Check ((Get-FileHash $toc).Hash -eq $tocInstalled) 'repair: the cockpit''s TOC entry back' }
Check ((Get-Item (Join-Path $win64 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*') 'repair: our loader back'
Check ((Get-Item (Join-Path $launcher 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*') 'repair: the launcher''s loader back'
Check ((Get-FileHash (Join-Path $launcher 'bink2w64_original.dll')).Hash -eq (Get-FileHash $LauncherBinkDll).Hash) 'repair: the launcher''s own Bink still behind it'
Check (Test-Path (Join-Path $win64 'ASI\menu\main.rcss')) 'repair: the menu file back'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match 'sfr2HalfEyeUU = 2\.9') 'repair: the player''s Halcyon value kept'

Check ((Run-Setup @('--restore-settings')) -eq 0) 'restore the game settings'
Check ((Get-Content -Raw (Join-Path $config 'GamerSettings.ini')) -eq $gamer) 'GamerSettings.ini exactly as it was'
Check ((Run-Setup @('--apply-settings', '--quality', 'balanced')) -eq 0) 'apply the game settings again'
Check ((Get-Content -Raw (Join-Path $config 'GamerSettings.ini')) -match '(?m)^ResX=4096') 'GamerSettings.ini: Balanced'
# Automatic again: the three games' settings.ini forgets ME1's first-start size, not ME2's (research/25 section 20.5).
$sharedSettings = Join-Path $setupLocal 'MEVR\settings.ini'
[IO.File]::WriteAllText($sharedSettings, "[Hands]`r`nphysicalCrouch=0`r`n[Display.LE1]`r`neyeSizeSet=1`r`n[Display.LE2]`r`neyeSizeSet=1`r`n")
Check ((Run-Setup @('--apply-settings', '--quality', 'auto')) -eq 0) 'apply Automatic'
$sharedText = Get-Content -Raw $sharedSettings
Check (($sharedText -match '(?ms)\[Display\.LE1\]\s+eyeSizeSet=0') -and ($sharedText -match '(?ms)\[Display\.LE2\]\s+eyeSizeSet=1') -and
    ($sharedText -match 'physicalCrouch=0')) 'Automatic: settings.ini [Display.LE1] eyeSizeSet = 0, ME2''s and the rest untouched'
Check ((Run-Setup @('--apply-settings', '--quality', 'balanced')) -eq 0) 'apply Balanced again'
Check ((Get-Content -Raw (Join-Path $config 'GamerSettings.ini')) -match '(?m)^ResX=4096') 'GamerSettings.ini: Balanced again'

# Install over the top (an update): nothing is backed up twice, the player's MELEVR.ini stays.
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'keep')) -eq 0) 'install again (update)'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match 'sfr2HalfEyeUU = 2\.9') 'update: the player''s Halcyon value kept'
if ($Voice) {
    # The real downloads (the internet): the order model's release isn't published until the owner uploads it, so GitHub's
    # 404 is the "not published yet" path and the recognizer goes in alone; with the release up, the model tier comes too.
    Check ((Run-Setup @('--install-voice')) -eq 0) 'voice: --install-voice (the real downloads)'
    $vout = Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')
    $voiceDir = Join-Path $win64 'ASI\voice'
    Check ((Get-FileHash (Join-Path $voiceDir 'onnxruntime.dll')).Hash -eq 'DEC964AB1EE36CC9B0AE247D13B376627992FC57DEC0454354017AB8FD84F1EA') 'voice: onnxruntime.dll from Moonshine''s release'
    Check (Test-Path (Join-Path $voiceDir 'models\tiny-streaming-en\streaming_config.json')) 'voice: the tiny models'
    Check ($vout -match "isn't published yet" -or (Test-Path (Join-Path $voiceDir 'llm\model.gguf'))) 'voice: the order model, or why not'
    Check ($vout -match '(?m)^voice orders: recognizer, models-tiny') 'voice: status'
    Check ((Run-Setup @('--check-voice', '--deep')) -eq 0) 'voice: every file''s SHA-256 as installed'
    # Left installed: the uninstall below has to take the downloads out too (the byte-for-byte check).
}

# Report a problem (research/33 section 8): fake logs that name this PC's user and computer and an e-mail address, a
# crash and a snapshot in a fake %LOCALAPPDATA%\MEVR. The zip lands in scratch; the game folder isn't touched.
$fakeLocal = Join-Path $scratch 'fake-local\MEVR'
$reportOut = Join-Path $scratch 'report-out'
Remove-Item -Recurse -Force (Join-Path $scratch 'fake-local'), $reportOut -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $fakeLocal 'snapshots'), (Join-Path $win64 'Logs') | Out-Null
$who = "C:\Users\$env:USERNAME\AppData\Local\MEVR on $env:COMPUTERNAME, mail $env:USERNAME.test@example.com"
[IO.File]::WriteAllText((Join-Path $win64 'Logs\LE1MEVR.log'),
    "[01:00:00.000] [info] MEVR 0.1.0 (monolithic, commit abc1234) attaching to LE1`r`n[01:00:01.000] [error] could not open $who`r`n" +
    "[01:00:02.000] [warn] late frame 12 ms`r`n[01:00:03.000] [warn] late frame 15 ms`r`n")
[IO.File]::WriteAllText((Join-Path $fakeLocal 'crash-breadcrumbs.txt'),
    "==== MEVR crash breadcrumbs ====`r`n2026-10-26 00:11:26.458  pid 1`r`nexception 0xC0000005 (access violation) at LE1MEVR.asi+0x1234, reading 0x0`r`n`r`nnative stack (unwound from the fault):`r`n  [0] LE1MEVR.asi+0x1234  (rsp 0x1)`r`n`r`n$who`r`n")
[IO.File]::WriteAllText((Join-Path $fakeLocal 'snapshots\2026-10-26_01-00-00-000.json'), "{`"note`": `"$($who -replace '\\', '\\')`"}")
[IO.File]::WriteAllBytes((Join-Path $fakeLocal 'snapshots\2026-10-26_01-00-00-000-left.png'), [byte[]](137, 80, 78, 71))
# The calibration the three games share (research/25 section 17): in the zip, redacted as the rest.
[IO.File]::WriteAllText((Join-Path $fakeLocal 'calibration.ini'),
    "[Calibration.oculus_touch_controller__meta_quest_3]`r`nlabel=Touch Controller`r`nnote=$who`r`ntime=2026-11-18 10:00`r`n")
# The settings the three games share (research/25 section 20): in the zip, redacted, and what ME1 reads from it diffed.
[IO.File]::WriteAllText((Join-Path $fakeLocal 'settings.ini'),
    "[Comfort]`r`nturn=snap`r`n[Voice]`r`ndevice=$who`r`n[Hands.LE2]`r`naimAssist=1`r`n")
$beforeReport = Snapshot
Check ((Run-Setup @('--report', '--snapshot', '--report-dir', $reportOut, '--local', $fakeLocal)) -eq 0) 'report'
$zipFile = Get-ChildItem (Join-Path $reportOut 'MEVR-report-*.zip') | Select-Object -First 1
Check ($null -ne $zipFile) 'report: the zip is written'
if ($zipFile) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zipFile.FullName)
    $names = @($archive.Entries | ForEach-Object { $_.FullName })
    $texts = @{}
    foreach ($entry in $archive.Entries) {
        $reader = New-Object IO.StreamReader($entry.Open())
        $texts[$entry.FullName] = $reader.ReadToEnd()
        $reader.Dispose()
    }
    $archive.Dispose()
    Check ($names[0] -eq 'REPORT.md') 'report: REPORT.md first'
    Check (($names -contains 'logs/LE1MEVR.log') -and ($names -contains 'logs/crash-breadcrumbs.txt') -and ($names -contains 'settings/MELEVR.ini') -and
        ($names -contains 'settings/MEVR-install.json') -and ($names -contains 'system/folders.txt')) 'report: logs, settings, listing'
    Check (($names -contains 'snapshot/2026-10-26_01-00-00-000.json') -and ($names -contains 'snapshot/2026-10-26_01-00-00-000-left.png')) 'report: the snapshot'
    Check (($names -contains 'settings/calibration.ini') -and ($texts['settings/calibration.ini'] -match '\[Calibration\.oculus_touch_controller__meta_quest_3\]')) 'report: the shared calibration'
    Check (($names -contains 'settings/settings.ini') -and ($texts['settings/settings.ini'] -match '\[Hands\.LE2\]') -and
        ($texts['REPORT.md'] -match 'settings\.ini')) 'report: the shared settings, and what ME1 reads from them'
    $leak = $texts.Keys | Where-Object { $_ -notlike '*.png' -and ($texts[$_] -match [regex]::Escape($env:COMPUTERNAME) -or
        $texts[$_] -match ('\b' + [regex]::Escape($env:USERNAME) + '\b') -or $texts[$_] -match 'test@example\.com') }
    Check (-not $leak) "report: no user name, computer name or e-mail left ($($leak -join ', '))"
    Check ($texts['logs/LE1MEVR.log'] -match 'C:\\Users\\<user>\\AppData' -and $texts['logs/LE1MEVR.log'] -match '<computer>' -and $texts['logs/LE1MEVR.log'] -match '<email>') 'report: redacted in place'
    $md = $texts['REPORT.md']
    Check ($md -match 'Faulting module\+offset: LE1MEVR\.asi\+0x1234' -and $md -match '\[warn\] x2' -and $md -match 'Ask me what I was doing') 'report: REPORT.md (crash, merged warnings, prompt)'
    Check ($md.Length -lt 30KB) 'report: REPORT.md under 30 KB'
    $expand = Join-Path $scratch 'report-expanded'
    Remove-Item -Recurse -Force $expand -ErrorAction SilentlyContinue
    Expand-Archive -Path $zipFile.FullName -DestinationPath $expand
    Check ((Get-Content -Raw (Join-Path $expand 'logs\LE1MEVR.log')) -eq $texts['logs/LE1MEVR.log']) 'report: Windows'' own unzip reads it'
}
Check ((Test-Path (Join-Path $fakeLocal 'report.last')) -and (Test-Path (Join-Path $fakeLocal 'report.result'))) 'report: report.last and report.result'
Check (-not (Compare-Object $beforeReport (Snapshot))) 'report: the game folder untouched'
# The fake log stays: the uninstall below removes the mod's run-time files and the Logs folder (on record as ours).

# What the mod writes while it's installed (research/33 section 13): the first VR start's eye size through Halcyon's profile
# into GamerSettings.ini (the game copies it into BIOEngine.ini), and Halcyon's DepthOfField line at the top of the section.
# The uninstall puts the player's values back over those too.
$melevrPath = Join-Path $win64 'ASI\MELEVR.ini'
[IO.File]::WriteAllText($melevrPath, ([IO.File]::ReadAllText($melevrPath) -replace '(?m)^sfr2ResX = \d+', 'sfr2ResX = 2824' -replace '(?m)^sfr2ResY = \d+', 'sfr2ResY = 2880'))
foreach ($f in (Join-Path $config 'GamerSettings.ini'), (Join-Path $config 'BIOEngine.ini')) {
    [IO.File]::WriteAllText($f, ([IO.File]::ReadAllText($f) -replace '(?m)^ResX=\d+', 'ResX=2824' -replace '(?m)^ResY=\d+', 'ResY=2880'))
}
$gs = Join-Path $config 'GamerSettings.ini'
[IO.File]::WriteAllText($gs, ([IO.File]::ReadAllText($gs) -replace '(?m)^\[SystemSettings\]\r?\n', "[SystemSettings]`r`nDepthOfField=True`r`n"))
Check ((Get-Content -Raw $gs) -match '(?m)^ResX=2824' -and (Get-Content -Raw $gs) -match '(?m)^DepthOfField=True') 'the mod''s writes in place (eye size, depth of field)'

Check ((Run-Setup @('--uninstall')) -eq 0) 'uninstall'
$after = Snapshot
$diff = Compare-Object $before $after
if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
Check (-not $diff) 'the fake game folder is byte for byte what it was'
Check (-not (Test-Path (Join-Path $win64 'ASI'))) 'the ASI folder is gone (we created it)'

# QA 2026-10-26 (research/33 section 7). Halcyon's upstream MELE-VR in Win64: moved aside, put back.
[IO.File]::WriteAllBytes((Join-Path $win64 'dxgi.dll'), [Text.Encoding]::ASCII.GetBytes('MZ stand-in for upstream MELEVR dxgi.dll'))
[IO.File]::WriteAllText((Join-Path $win64 'openxr_loader.dll'), 'stand-in')
$beforeUpstream = Snapshot
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'keep')) -eq 0) 'install over Halcyon''s MELE-VR'
Check ((Test-Path (Join-Path $win64 'dxgi.dll.mevr-off')) -and -not (Test-Path (Join-Path $win64 'dxgi.dll'))) 'its dxgi.dll moved aside'
Check ((Run-Setup @('--uninstall')) -eq 0) 'uninstall'
Check (-not (Compare-Object $beforeUpstream (Snapshot))) 'its DLLs back, the folder byte for byte'
Remove-Item (Join-Path $win64 'dxgi.dll'), (Join-Path $win64 'openxr_loader.dll')

# The mod's run-time files, and a lost install record followed by a reinstall: the uninstall still takes it all out.
Check ((Run-Setup @('--install', '--source', 'Steam', '--no-settings')) -eq 0) 'install'
New-Item -ItemType Directory -Force (Join-Path $win64 'Logs') | Out-Null
[IO.File]::WriteAllText((Join-Path $win64 'ASI\LE1MEVR.ini'), "[Hands]`r`n")
[IO.File]::WriteAllText((Join-Path $win64 'Logs\LE1MEVR.log'), 'log')
Remove-Item (Join-Path $win64 'ASI\MEVR-install.json')
Check ((Run-Setup @('--install', '--source', 'Steam', '--no-settings')) -eq 0) 'install again without the install record'
Check ((Run-Setup @('--uninstall')) -eq 0) 'uninstall'
Check ((Get-FileHash (Join-Path $win64 'bink2w64.dll')).Hash -eq (Get-FileHash $BinkDll).Hash -and -not (Test-Path (Join-Path $win64 'bink2w64_original.dll'))) 'RAD''s DLL back after the lost record'
Check (-not (Test-Path (Join-Path $win64 'ASI')) -and -not (Test-Path (Join-Path $win64 'Logs'))) 'LE1MEVR.ini, the log, ASI and Logs gone'
# steam_appid.txt from the first install: without its record it's left (ME3Tweaks Mod Manager writes the same file).
Check (Test-Path (Join-Path $win64 'steam_appid.txt')) 'steam_appid.txt left (no record of it)'
Remove-Item (Join-Path $win64 'steam_appid.txt')
$diff = Compare-Object $before (Snapshot)
if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
Check (-not $diff) 'the fake game folder is byte for byte what it was'

# research/33 section 9. A game never started on this PC: no GamerSettings.ini (nor BIOEngine.ini). The install goes on and
# keeps the VR game settings; Repair applies them once the game has made its file; uninstall leaves the game's own file.
$gamerFile = Join-Path $config 'GamerSettings.ini'
$engineFile = Join-Path $config 'BIOEngine.ini'
Remove-Item $gamerFile, $engineFile
$beforeFirstStart = Snapshot
Check ((Run-Setup @('--install', '--source', 'Steam')) -eq 0) 'install on a game never started with default quality'
$out = Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')
Check ($out -match "hasn't been started on this PC yet" -and $out -match '(?m)^game settings waiting for the game''s first start: 2048 x 2124') 'install: the Automatic starting size waits'
Check (-not (Test-Path $gamerFile)) 'install: no GamerSettings.ini made up'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match '(?m)^sfr2ResX = 4096') 'install: MELEVR.ini''s target waits too'
Check ((Run-Setup @('--repair')) -eq 0) 'repair before the first start'
Check ((Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')) -match 'still wait for the game''s first start') 'repair: the settings still wait'
# The game's first start makes its file (as the game writes it: full screen, its own resolution).
$firstGamer = "[SystemSettings]`r`nFullscreen=True`r`nResX=1920`r`nResY=1080`r`nUseVSync=True`r`nMotionBlur=True`r`n`r`n[HDR]`r`nDesiredRange=DynamicRange_HDR`r`n"
[IO.File]::WriteAllText($gamerFile, $firstGamer)
Check ((Run-Setup @('--repair')) -eq 0) 'repair after the first start'
$g = Get-Content -Raw $gamerFile
Check ($g -match '(?m)^ResX=2048' -and $g -match '(?m)^ResY=2124' -and $g -match '(?m)^BorderlessWindow=True' -and $g -match '(?m)^Fullscreen=False' -and $g -match 'DynamicRange_SDR') 'repair: the waiting VR game settings applied'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match '(?m)^sfr2ResX = 2048') 'repair: MELEVR.ini''s target with them'
$installRecord = Get-Content -Raw (Join-Path $win64 'ASI\MEVR-install.json') | ConvertFrom-Json
Check $installRecord.eyeSizeAtFirstStart 'repair: the first VR session will size the render target'
$out = Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')
Check ($out -match '(?m)^game settings applied: yes' -and $out -notmatch 'game settings waiting') 'repair: nothing waits any more'
Check ((Run-Setup @('--uninstall')) -eq 0) 'uninstall'
Check ((Get-Content -Raw $gamerFile) -eq $firstGamer) 'uninstall: the game''s own GamerSettings.ini, exactly'
Remove-Item $gamerFile
$diff = Compare-Object $beforeFirstStart (Snapshot)
if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
Check (-not $diff) 'the never-started game folder is byte for byte what it was'
[IO.File]::WriteAllText($gamerFile, $gamer)
[IO.File]::WriteAllText($engineFile, $engineText)

# Upgrading from Halcyon's upstream MELE-VR, its MELEVR.ini in the ASI folder (AER, third person, its welcome, Insert, the
# mirror every frame): every slot normalised, the player's other values kept; "keep my settings" puts it back exactly.
New-Item -ItemType Directory -Force (Join-Path $win64 'ASI') | Out-Null
$upstreamIni = "[active]`nprofile=Default`n`n[hotkeys]`nenabled=1`n`n[profile:Default]`nvrMode = 1`nfirstRunDone = 0`nmenuKey = 45`nfpEnabled = 0`n" +
    "combatHeadAim = 1`nmirrorPresentEvery = 1`ndepthPopKeyEnabled = 1`nsfr2HalfEyeUU = 3.4`n`n[profile:Custom 1]`nvrMode = 3`nfpEnabled = 0`n"
[IO.File]::WriteAllText((Join-Path $win64 'ASI\MELEVR.ini'), $upstreamIni)
$beforeUpgrade = Snapshot
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'keep')) -eq 0) 'install over upstream''s MELEVR.ini'
$m = Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')
$slots = [regex]::Split($m, '(?m)^\[profile:')
Check ($slots.Count -eq 3 -and @($slots[1..2] | Where-Object { $_ -match '(?m)^vrMode = 2' -and $_ -match '(?m)^fpEnabled = 1' -and $_ -match '(?m)^firstRunDone = 1' -and
    $_ -match '(?m)^menuKey = 220' -and $_ -match '(?m)^mirrorPresentEvery = 2' -and $_ -match '(?m)^depthPopKeyEnabled = 0' -and $_ -match '(?m)^sfr2Metric = 1' }).Count -eq 2) 'MELEVR.ini: both slots as a fresh install writes them'
Check ($m -match '(?m)^sfr2HalfEyeUU = 3\.4' -and $m -match '(?m)^enabled=0') 'MELEVR.ini: the player''s tuning kept, the F-key hotkeys off'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MEVR-install.json')) -match '"key": "fpEnabled"') 'the manifest records each change'
Check ((Run-Setup @('--uninstall', '--keep-settings')) -eq 0) 'uninstall, keeping the VR settings'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -eq $upstreamIni) 'kept MELEVR.ini: exactly as it came'
$diff = Compare-Object $beforeUpgrade (Snapshot)
if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
Check (-not $diff) 'the folder is byte for byte what it was before the upgrade'
Remove-Item -Recurse -Force (Join-Path $win64 'ASI')
$diff = Compare-Object $before (Snapshot)
Check (-not $diff) 'the fake game folder is byte for byte what it was'
}  # LE1

# ---- The sequels: the same steps by the same code, with --for (docs/trilogy-plan.md T52) -----------------------------
function Test-Sequel([string]$game) {
    $n = $game.Substring(2)
    $name = "Mass Effect $n"
    $script:For = $game
    $w = Join-Path $root "Game\ME$n\Binaries\Win64"
    $c = Join-Path $root "Game\ME$n\BioGame\Config"
    $gamerFile = Join-Path $c 'GamerSettings.ini'
    $engineFile = Join-Path $c 'BIOEngine.ini'
    $engineBefore = Get-Content -Raw $engineFile
    $rendererIni = Join-Path $w "MELE${n}VR.ini"
    $hasTemplate = Test-Path (Join-Path $Payload "MELE${n}VR.ini")
    $cli = Join-Path $scratch 'setup-cli.txt'
    $beforeGame = Snapshot
    Copy-Item (Join-Path $root "Game\ME$n\BioGame\PCConsoleTOC.bin") (Join-Path $scratch "toc-$game.bin") -Force  # the game's own, for "verify files"

    Check ((Run-Setup @('--status')) -eq 0) "${game}: status"
    Check ((Get-Content -Raw $cli) -match "(?m)^title: $name \($game\)") "${game}: status names the game"
    Check ((Run-Setup @('--install', '--source', 'Steam')) -eq 0) "${game}: install"
    Check ((Get-Item (Join-Path $w 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*') "${game}: bink2w64.dll is our loader"
    Check ((Get-FileHash (Join-Path $w 'bink2w64_original.dll')).Hash -eq (Get-FileHash $LauncherBinkDll).Hash) "${game}: RAD's DLL kept as bink2w64_original.dll"
    Check ((Get-FileHash (Join-Path $w "ASI\${game}MEVR.asi")).Hash -eq (Get-FileHash (Join-Path $Payload "${game}MEVR.asi")).Hash) "${game}: its own ASI"
    Check (-not (Test-Path (Join-Path $w 'ASI\LE1MEVR.asi'))) "${game}: not Mass Effect 1's ASI"
    Check ((Get-Item (Join-Path $launcher 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*') "${game}: Game\Launcher\bink2w64.dll is our loader"
    Check ((Test-Path (Join-Path $w 'ASI\menu\main.rml')) -and (Test-Path (Join-Path $w 'ASI\fonts\Rajdhani-Bold.ttf')) -and (Test-Path (Join-Path $w 'ASI\MEVR-Setup.exe'))) "${game}: the menu, the fonts, the setup copy"
    $record = Get-Content -Raw (Join-Path $w 'ASI\MEVR-install.json') | ConvertFrom-Json
    Check ($record.game -eq $game -and $record.loader -eq 'mevr' -and $record.gameSettings) "${game}: its own install record (game, loader, settings applied)"
    Check (-not (Test-Path (Join-Path $root 'Game\ME1\Binaries\Win64\ASI'))) "${game}: nothing in Mass Effect 1's folder"
    Check (-not (Test-Path (Join-Path $w 'ASI\cockpit')) -and -not (Test-Path (Join-Path $w 'ASI\voice')) -and -not (Test-Path (Join-Path $w 'ASI\audio'))) "${game}: no cockpit, voice or OpenAL Soft (Mass Effect 1's)"
    Check ((Get-Content -Raw (Join-Path $w 'steam_appid.txt')) -eq '1328670') "${game}: steam_appid.txt"
    $sequelToc = Join-Path $root "Game\ME$n\BioGame\PCConsoleTOC.bin"
    $sequelTocGame = Join-Path $scratch "toc-$game.bin"
    if ($withRig) {
        # The moving bodies: both packages in its CookedPCConsole, their TOC entries, the index in %LOCALAPPDATA%.
        $cooked = Join-Path $root "Game\ME$n\BioGame\CookedPCConsole"
        Check ((Get-Item (Join-Path $cooked "MEVR_Bodies_$game.pcc")).Length -eq 4096 -and (Get-Item (Join-Path $cooked 'MEVR_Miranda.pcc')).Length -eq 4096) "${game}: the moving bodies' packages (and Miranda's hair)"
        $grows = (Toc-EntryBytes "BioGame\CookedPCConsole\MEVR_Bodies_$game.pcc") + (Toc-EntryBytes 'BioGame\CookedPCConsole\MEVR_Miranda.pcc')
        Check ((Get-Item $sequelToc).Length -eq (Get-Item $sequelTocGame).Length + $grows) "${game}: PCConsoleTOC.bin: two entries more"
        Check (Test-Path (Join-Path $setupLocal "MEVR\rig\$game\MEVR_Bodies_$game.json")) "${game}: the moving bodies' index in %LOCALAPPDATA%\MEVR\rig\$game"
        $sequelTocInstalled = (Get-FileHash $sequelToc).Hash
    }
    if ($hasTemplate) {
        Check ((Get-FileHash $rendererIni).Hash -eq (Get-FileHash (Join-Path $Payload "MELE${n}VR.ini")).Hash) "${game}: the renderer's release settings next to the exe"
    } else {
        Check (-not (Test-Path $rendererIni)) "${game}: no renderer settings made up (the release has none: the renderer writes its own)"
    }
    $g = Get-Content -Raw $gamerFile
    Check ($g -match '(?m)^BorderlessWindow=True' -and $g -match '(?m)^Fullscreen=False' -and $g -match '(?m)^UseVSync=False' -and $g -match '(?m)^MotionBlur=False' -and
        $g -match '(?m)^FilmGrain=False' -and $g -match '(?m)^MaxAnisotropy=16' -and $g -match 'DynamicRange_SDR') "${game}: GamerSettings.ini: the VR values"
    Check ($g -match '(?m)^ResX=2560' -and $g -match '(?m)^ResY=1440') "${game}: GamerSettings.ini: the resolution left to the renderer"
    if ($game -eq 'LE2') {
        Check ($g -match '(?m)^DynamicShadows=True' -and $g -match '(?m)^FoliageDrawRadiusMultiplier=0\.5') 'LE2: GamerSettings.ini: the game''s dynamic shadows, the foliage radius'
    } else {
        Check ($g -match '(?m)^DynamicShadows=True' -and $g -notmatch 'FoliageDrawRadiusMultiplier') 'LE3: GamerSettings.ini: shadows and foliage as the game has them'
    }
    Check ((Get-Content -Raw $engineFile) -eq $engineBefore) "${game}: BIOEngine.ini untouched"

    # The player tunes the renderer (a value we never touch): it must survive everything below.
    if (-not (Test-Path $rendererIni)) { [IO.File]::WriteAllText($rendererIni, "[VR]`r`nHalfEyeUU=3.350`r`nRenderW=4096`r`nRenderH=2304`r`n") }
    $tuned = [IO.File]::ReadAllText($rendererIni)
    $tuned = if ($tuned -match '(?m)^HalfEyeUU=') { $tuned -replace '(?m)^HalfEyeUU=.*$', 'HalfEyeUU=2.900' } else { $tuned -replace '(?m)^\[VR\]\r?\n', "[VR]`r`nHalfEyeUU=2.900`r`n" }
    [IO.File]::WriteAllText($rendererIni, $tuned)   # ME3's own file has no HalfEyeUU (unused in life-size stereo): added

    # Steam's "verify files": RAD's DLL back in the game and the launcher, a menu file gone.
    Copy-Item $LauncherBinkDll (Join-Path $w 'bink2w64.dll') -Force
    Copy-Item $LauncherBinkDll (Join-Path $launcher 'bink2w64.dll') -Force
    Remove-Item (Join-Path $w 'ASI\menu\main.rcss')
    if ($withRig) { Copy-Item $sequelTocGame $sequelToc -Force }  # and the game's own TOC
    Run-Setup @('--status') | Out-Null
    $st = Get-Content -Raw $cli
    Check ($st -match "(?m)^needs repair: .*Game/ME$n/Binaries/Win64/bink2w64\.dll" -and $st -match '(?m)^needs repair: .*Game/Launcher/bink2w64\.dll' -and $st -match '(?m)^needs repair: .*main\.rcss') "${game}: status after `"verify files`": both loaders and the menu file need repair"
    if ($withRig) { Check ($st -match "(?m)^needs repair: .*Game/ME$n/BioGame/PCConsoleTOC\.bin") "${game}: status after `"verify files`": the TOC needs repair" }
    Check ((Run-Setup @('--repair')) -eq 0) "${game}: repair"
    Run-Setup @('--status') | Out-Null
    Check ((Get-Content -Raw $cli) -match '(?m)^needs repair: nothing') "${game}: status after repair: nothing to repair"
    if ($withRig) { Check ((Get-FileHash $sequelToc).Hash -eq $sequelTocInstalled) "${game}: repair: the moving bodies' TOC entries back" }
    Check ((Get-Item (Join-Path $w 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*' -and
        (Get-Item (Join-Path $launcher 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*' -and (Test-Path (Join-Path $w 'ASI\menu\main.rcss'))) "${game}: repair: both loaders and the menu file back"
    Check ((Get-Content -Raw $rendererIni) -match '(?m)^HalfEyeUU=2\.900') "${game}: repair: the player's renderer value kept"

    Check ((Run-Setup @('--restore-settings')) -eq 0) "${game}: restore the game settings"
    Check ((Get-Content -Raw $gamerFile) -eq $sequelGamer) "${game}: GamerSettings.ini exactly as it was"
    Check ((Run-Setup @('--apply-settings')) -eq 0) "${game}: apply the game settings again"
    Check ((Get-Content -Raw $gamerFile) -match '(?m)^BorderlessWindow=True') "${game}: GamerSettings.ini: the VR values again"
    Check ((Run-Setup @('--install', '--source', 'Steam')) -eq 0) "${game}: install again (update)"
    Check ((Get-Content -Raw $rendererIni) -match '(?m)^HalfEyeUU=2\.900') "${game}: update: the player's renderer value kept"

    # Report a problem for this game: its own log and settings, redacted as Mass Effect 1's.
    $fakeLocal = Join-Path $scratch "fake-local-$game\MEVR"
    $reportOut = Join-Path $scratch "report-out-$game"
    Remove-Item -Recurse -Force (Join-Path $scratch "fake-local-$game"), $reportOut -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $fakeLocal, (Join-Path $w 'Logs') | Out-Null
    $who = "C:\Users\$env:USERNAME\AppData\Local\MEVR on $env:COMPUTERNAME, mail $env:USERNAME.test@example.com"
    [IO.File]::WriteAllText((Join-Path $w "Logs\${game}MEVR.log"),
        "[01:00:00.000] [info] MEVR 0.1.0 (monolithic, commit abc1234) attaching to $game`r`n[01:00:01.000] [error] could not open $who`r`n[01:00:02.000] [warn] late frame 12 ms`r`n")
    $beforeReport = Snapshot
    Check ((Run-Setup @('--report', '--report-dir', $reportOut, '--local', $fakeLocal)) -eq 0) "${game}: report"
    $zipFile = Get-ChildItem (Join-Path $reportOut 'MEVR-report-*.zip') | Select-Object -First 1
    Check ($null -ne $zipFile) "${game}: report: the zip is written"
    if ($zipFile) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($zipFile.FullName)
        $names = @($archive.Entries | ForEach-Object { $_.FullName })
        $texts = @{}
        foreach ($entry in $archive.Entries) {
            $reader = New-Object IO.StreamReader($entry.Open())
            $texts[$entry.FullName] = $reader.ReadToEnd()
            $reader.Dispose()
        }
        $archive.Dispose()
        Check (($names -contains "logs/${game}MEVR.log") -and ($names -contains "settings/MELE${n}VR.ini") -and ($names -contains 'settings/MEVR-install.json') -and
            ($names -contains 'system/folders.txt') -and -not ($names -contains 'logs/LE1MEVR.log')) "${game}: report: its own log, settings and listing"
        Check ($texts['system/folders.txt'] -match "Game\\ME$n\\Binaries\\Win64" -and $texts['system/folders.txt'] -match "${game}MEVR\.asi") "${game}: report: the listing is its folder's"
        $leak = $texts.Keys | Where-Object { $texts[$_] -match [regex]::Escape($env:COMPUTERNAME) -or $texts[$_] -match ('\b' + [regex]::Escape($env:USERNAME) + '\b') -or $texts[$_] -match 'test@example\.com' }
        Check (-not $leak) "${game}: report: no user name, computer name or e-mail left ($($leak -join ', '))"
        $md = $texts['REPORT.md']
        Check ($md -match "MassEffect$n\.exe" -and $md -match "logs/${game}MEVR\.log\): 1 errors, 1 warnings" -and $md -notmatch 'LE1MEVR') "${game}: report: REPORT.md names this game's exe and log"
    }
    Check (-not (Compare-Object $beforeReport (Snapshot))) "${game}: report: the game folder untouched"

    # What the renderer writes while the mod is installed: its RenderW x RenderH into ResX/ResY at attach (the renderer ini's:
    # ME2's 4096x2304, ME3's 3072x2216), and DepthOfField.
    $renderer = [IO.File]::ReadAllText($rendererIni)
    $renderW = [regex]::Match($renderer, '(?m)^RenderW=(\d+)').Groups[1].Value
    $renderH = [regex]::Match($renderer, '(?m)^RenderH=(\d+)').Groups[1].Value
    Check ($renderW -and $renderH) "${game}: the renderer's size in its settings file ($renderW x $renderH)"
    [IO.File]::WriteAllText($gamerFile, ([IO.File]::ReadAllText($gamerFile) -replace '(?m)^ResX=\d+', "ResX=$renderW" -replace '(?m)^ResY=\d+', "ResY=$renderH" -replace
        '(?m)^\[SystemSettings\]\r?\n', "[SystemSettings]`r`nDepthOfField=False`r`n"))
    Check ((Run-Setup @('--uninstall')) -eq 0) "${game}: uninstall"
    $diff = Compare-Object $beforeGame (Snapshot)
    if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
    Check (-not $diff) "${game}: the fake game folder is byte for byte what it was"
    Check (-not (Test-Path (Join-Path $w 'ASI')) -and -not (Test-Path (Join-Path $w 'Logs'))) "${game}: the ASI and Logs folders are gone (we created them)"

    # Halcyon's own release for this game: its dxgi.dll (told by "ME2VR" in it), openxr_loader.dll and its settings file
    # next to the exe. The DLLs move aside; its settings file is the player's: never written, never deleted.
    [IO.File]::WriteAllBytes((Join-Path $w 'dxgi.dll'), [Text.Encoding]::ASCII.GetBytes('MZ stand-in for upstream ME2VR dxgi.dll'))
    [IO.File]::WriteAllText((Join-Path $w 'openxr_loader.dll'), 'stand-in')
    $upstreamIni = "[VR]`r`nMode=4`r`nRenderW=3072`r`nRenderH=1728`r`nInstallerRun=1`r`n"
    [IO.File]::WriteAllText($rendererIni, $upstreamIni)
    $beforeUpstream = Snapshot
    Check ((Run-Setup @('--install', '--source', 'Steam')) -eq 0) "${game}: install over Halcyon's own release"
    Check ((Test-Path (Join-Path $w 'dxgi.dll.mevr-off')) -and -not (Test-Path (Join-Path $w 'dxgi.dll')) -and (Test-Path (Join-Path $w 'openxr_loader.dll.mevr-off'))) "${game}: its two DLLs moved aside"
    Check ((Get-Content -Raw $rendererIni) -eq $upstreamIni) "${game}: its settings file left as it is"
    Check ((Run-Setup @('--uninstall')) -eq 0) "${game}: uninstall"
    Check (-not (Compare-Object $beforeUpstream (Snapshot))) "${game}: its DLLs back and its settings file still there, byte for byte"
    Remove-Item (Join-Path $w 'dxgi.dll'), (Join-Path $w 'openxr_loader.dll'), $rendererIni

    # A lost install record, then a reinstall: the uninstall still takes it all out.
    Check ((Run-Setup @('--install', '--source', 'Steam', '--no-settings')) -eq 0) "${game}: install"
    New-Item -ItemType Directory -Force (Join-Path $w 'Logs') | Out-Null
    [IO.File]::WriteAllText((Join-Path $w "ASI\${game}MEVR.ini"), "[Hands]`r`n")
    [IO.File]::WriteAllText((Join-Path $w "Logs\${game}MEVR.log"), 'log')
    Remove-Item (Join-Path $w 'ASI\MEVR-install.json')
    Check ((Run-Setup @('--install', '--source', 'Steam', '--no-settings')) -eq 0) "${game}: install again without the install record"
    Check ((Run-Setup @('--uninstall')) -eq 0) "${game}: uninstall"
    Check ((Get-FileHash (Join-Path $w 'bink2w64.dll')).Hash -eq (Get-FileHash $LauncherBinkDll).Hash -and -not (Test-Path (Join-Path $w 'bink2w64_original.dll'))) "${game}: RAD's DLL back after the lost record"
    Check (-not (Test-Path (Join-Path $w 'ASI')) -and -not (Test-Path (Join-Path $w 'Logs'))) "${game}: its ini, the log, ASI and Logs gone"
    # Left without their record, as Mass Effect 1's steam_appid.txt is: steam_appid.txt, and the release's renderer settings
    # (next to the exe, where a file without a record counts as the player's).
    Check (Test-Path (Join-Path $w 'steam_appid.txt')) "${game}: steam_appid.txt left (no record of it)"
    Remove-Item (Join-Path $w 'steam_appid.txt')
    if ($hasTemplate) {
        Check (Test-Path $rendererIni) "${game}: the renderer settings left (no record of them: the player's)"
        Remove-Item $rendererIni
    }
    $diff = Compare-Object $beforeGame (Snapshot)
    if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
    Check (-not $diff) "${game}: the fake game folder is byte for byte what it was"

    # A game never started on this PC: no GamerSettings.ini. The settings wait; Repair applies them once the file exists.
    Remove-Item $gamerFile
    $beforeFirstStart = Snapshot
    Check ((Run-Setup @('--install', '--source', 'Steam')) -eq 0) "${game}: install on a game never started"
    $out = Get-Content -Raw $cli
    Check ($out -match "$name hasn't been started on this PC yet" -and $out -match "(?m)^game settings waiting for the game's first start") "${game}: install: the VR game settings wait"
    Check (-not (Test-Path $gamerFile)) "${game}: install: no GamerSettings.ini made up"
    Check ((Run-Setup @('--repair')) -eq 0) "${game}: repair before the first start"
    Check ((Get-Content -Raw $cli) -match "start $name once") "${game}: repair: the settings still wait, and it names the game"
    $firstGamer = "[SystemSettings]`r`nFullscreen=True`r`nResX=1920`r`nResY=1080`r`nUseVSync=True`r`nMotionBlur=True`r`n`r`n[HDR]`r`nDesiredRange=DynamicRange_HDR`r`n"
    [IO.File]::WriteAllText($gamerFile, $firstGamer)
    Check ((Run-Setup @('--repair')) -eq 0) "${game}: repair after the first start"
    $g = Get-Content -Raw $gamerFile
    Check ($g -match '(?m)^BorderlessWindow=True' -and $g -match '(?m)^Fullscreen=False' -and $g -match 'DynamicRange_SDR' -and $g -match '(?m)^ResX=1920') "${game}: repair: the waiting VR game settings applied"
    $out = Get-Content -Raw $cli
    Check ($out -match '(?m)^game settings applied: yes' -and $out -notmatch 'game settings waiting') "${game}: repair: nothing waits any more"
    Check ((Run-Setup @('--uninstall')) -eq 0) "${game}: uninstall"
    Check ((Get-Content -Raw $gamerFile) -eq $firstGamer) "${game}: uninstall: the game's own GamerSettings.ini, exactly"
    Remove-Item $gamerFile
    $diff = Compare-Object $beforeFirstStart (Snapshot)
    if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
    Check (-not $diff) "${game}: the never-started game folder is byte for byte what it was"
    [IO.File]::WriteAllText($gamerFile, $sequelGamer)
    Check (-not (Compare-Object $beforeGame (Snapshot))) "${game}: the fake game folder is byte for byte what it was"
    $script:For = $null
}
foreach ($sequel in $Games | Where-Object { $_ -ne 'LE1' }) { Test-Sequel $sequel }

# ---- The games together: one launcher, one loader in it ---------------------------------------------------------------
if ($Games.Count -gt 1) {
    foreach ($game in $Games) {
        $script:For = $game
        Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'performance')) -eq 0) "together: install $game"
    }
    $first = $Games[0]
    $rest = @($Games | Select-Object -Skip 1)
    $script:For = $first
    Check ((Run-Setup @('--uninstall')) -eq 0) "together: uninstall $first while $($rest -join ' and ') stay"
    Check ((Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')) -match "left Game\\Launcher's loader in place") "together: the uninstall says the launcher's loader stays"
    Check ((Get-Item (Join-Path $launcher 'bink2w64.dll')).VersionInfo.FileDescription -like 'MEVR ASI loader*' -and (Test-Path (Join-Path $launcher 'bink2w64_original.dll'))) "together: the launcher's loader is still there for the others"
    Check (-not (Test-Path (Join-Path $root "Game\ME$($first.Substring(2))\Binaries\Win64\ASI"))) "together: $first's own files are gone"
    foreach ($game in $rest) {
        $script:For = $game
        Run-Setup @('--status') | Out-Null
        $st = Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')
        Check ($st -match '(?m)^installed: yes' -and $st -match '(?m)^needs repair: nothing') "together: $game still installed, nothing to repair"
    }
    foreach ($game in $rest) {
        $script:For = $game
        Check ((Run-Setup @('--uninstall')) -eq 0) "together: uninstall $game"
    }
    $script:For = $null
    Check ((Get-FileHash (Join-Path $launcher 'bink2w64.dll')).Hash -eq (Get-FileHash $LauncherBinkDll).Hash -and -not (Test-Path (Join-Path $launcher 'bink2w64_original.dll'))) "together: the launcher's own Bink back with the last uninstall"
    $diff = Compare-Object $before (Snapshot)
    if ($diff) { $diff | ForEach-Object { Write-Output "  differs: $($_.SideIndicator) $($_.InputObject)" } }
    Check (-not $diff) 'together: the fake game folder is byte for byte what it was'
}

Write-Output $(if ($failures -eq 0) { 'fake game test passed' } else { "fake game test FAILED ($failures)" })
exit $failures
