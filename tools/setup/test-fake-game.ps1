# SPDX-License-Identifier: MIT
# Tests MEVR-Setup.exe's command line against a FAKE game folder (research/33); never the real game.
#
# Builds scratch\fakegame\Mass Effect Legendary Edition with a stand-in MassEffect1.exe (the real version number,
# tools/setup/FakeGame), a copy of RAD's bink2w64 DLL (the real game's bink2w64_original.dll, read only, or -BinkDll)
# as bink2w64.dll, a copy of the game's PCConsoleTOC.bin (the one before the cockpit's test install, if kept, else the
# game's), and small GamerSettings.ini / BIOEngine.ini files; then runs status, install, a tamper + repair,
# restore and apply of the game settings, and uninstall, and checks the folder is byte for byte what it was.
#
#   tools\setup\test-fake-game.ps1 [-Build build] [-Payload <folder>] [-BinkDll <path>] [-LauncherBinkDll <path>]
#
# The fake game also has Game\Launcher (research/33 section 12): a stand-in MassEffectLauncher.exe and a RAD Bink DLL of
# the launcher's build (LE2's own bink2w64.dll, read only: the same 2.7s build, or -LauncherBinkDll), where Install puts
# our loader too (it boots Mass Effect 1 for Launch's -game 1).
param(
    [string]$Build = (Join-Path $PSScriptRoot '..\..\build'),
    [string]$Payload,
    [string]$BinkDll,
    [string]$LauncherBinkDll
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Build = (Resolve-Path $Build).Path
$scratch = Join-Path $repo 'scratch'
$root = Join-Path $scratch 'fakegame\Mass Effect Legendary Edition'
$win64 = Join-Path $root 'Game\ME1\Binaries\Win64'
$config = Join-Path $root 'Game\ME1\BioGame\Config'
$launcher = Join-Path $root 'Game\Launcher'
$setup = Join-Path $Build 'Release\MEVR-Setup.exe'
$failures = 0

function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Output "ok   $what" } else { Write-Output "FAIL $what"; $script:failures++ }
}

function Run-Setup([string[]]$arguments) {
    $out = Join-Path $scratch 'setup-cli.txt'
    $quoted = ($arguments + @('--game', $root, '--payload', $Payload, '--out', $out) | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    $p = Start-Process -FilePath $setup -ArgumentList $quoted -Wait -PassThru -NoNewWindow
    $text = if (Test-Path $out) { Get-Content -Raw $out } else { '' }
    Write-Host "---- MEVR-Setup $($arguments -join ' ') (exit $($p.ExitCode))"
    Write-Host $text.TrimEnd()
    return $p.ExitCode
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
    $LauncherBinkDll = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE2') 'bink2w64.dll'
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
    Copy-Item (Join-Path $Build 'Release\bink2w64.dll') $Payload
    Copy-Item -Recurse (Join-Path $repo 'third_party\fonts') (Join-Path $Payload 'fonts')
    Copy-Item -Recurse (Join-Path $repo 'assets\menu') (Join-Path $Payload 'menu')
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
}
$withCockpit = Test-Path (Join-Path $Payload 'cockpit\BIOG_MEVR_Cockpit.pcc')

# The fake game.
Remove-Item -Recurse -Force (Join-Path $scratch 'fakegame') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $win64, $config, $launcher | Out-Null
Copy-Item $fakeExe $win64
Copy-Item $BinkDll (Join-Path $win64 'bink2w64.dll')
Copy-Item $fakeExe (Join-Path $launcher 'MassEffectLauncher.exe')
Copy-Item $LauncherBinkDll (Join-Path $launcher 'bink2w64.dll')
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
$ErrorActionPreference = 'Continue'  # from here on a failed check is reported, not thrown

Check ((Run-Setup @('--status')) -eq 0) 'status'
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'performance')) -eq 0) 'install'
$bink = (Get-Item (Join-Path $win64 'bink2w64.dll')).VersionInfo
Check ($bink.ProductName -eq 'MEVR ASI loader') 'bink2w64.dll is our loader'
Check ((Get-FileHash (Join-Path $win64 'bink2w64_original.dll')).Hash -eq (Get-FileHash $BinkDll).Hash) 'RAD''s DLL kept as bink2w64_original.dll'
Check (Test-Path (Join-Path $win64 'ASI\LE1MEVR.asi')) 'the ASI'
Check ((Get-Item (Join-Path $launcher 'bink2w64.dll')).VersionInfo.ProductName -eq 'MEVR ASI loader') 'Game\Launcher\bink2w64.dll is our loader (its autoboot)'
Check ((Get-FileHash (Join-Path $launcher 'bink2w64_original.dll')).Hash -eq (Get-FileHash $LauncherBinkDll).Hash) 'the launcher''s own Bink kept as bink2w64_original.dll'
Check ((Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')) -match '(?m)^launcher: our loader starts Mass Effect 1') 'status: the launcher boots the game'
Check (Test-Path (Join-Path $win64 'ASI\menu\main.rml')) 'the menu'
Check (Test-Path (Join-Path $win64 'ASI\fonts\Rajdhani-Bold.ttf')) 'the fonts'
Check (Test-Path (Join-Path $win64 'ASI\MEVR-Setup.exe')) 'the setup copy (for uninstall and optional content)'
Check (Test-Path (Join-Path $win64 'ASI\MEVR-install.json')) 'the manifest'
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
    Check ($tocInstalled -ne (Get-FileHash $gameToc).Hash -and (Get-Item $toc).Length -eq (Get-Item $gameToc).Length + 76) 'PCConsoleTOC.bin: one entry more'
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
Check ((Get-Item (Join-Path $win64 'bink2w64.dll')).VersionInfo.ProductName -eq 'MEVR ASI loader') 'repair: our loader back'
Check ((Get-Item (Join-Path $launcher 'bink2w64.dll')).VersionInfo.ProductName -eq 'MEVR ASI loader') 'repair: the launcher''s loader back'
Check ((Get-FileHash (Join-Path $launcher 'bink2w64_original.dll')).Hash -eq (Get-FileHash $LauncherBinkDll).Hash) 'repair: the launcher''s own Bink still behind it'
Check (Test-Path (Join-Path $win64 'ASI\menu\main.rcss')) 'repair: the menu file back'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match 'sfr2HalfEyeUU = 2\.9') 'repair: the player''s Halcyon value kept'

Check ((Run-Setup @('--restore-settings')) -eq 0) 'restore the game settings'
Check ((Get-Content -Raw (Join-Path $config 'GamerSettings.ini')) -eq $gamer) 'GamerSettings.ini exactly as it was'
Check ((Run-Setup @('--apply-settings', '--quality', 'balanced')) -eq 0) 'apply the game settings again'
Check ((Get-Content -Raw (Join-Path $config 'GamerSettings.ini')) -match '(?m)^ResX=4096') 'GamerSettings.ini: Balanced'

# Install over the top (an update): nothing is backed up twice, the player's MELEVR.ini stays.
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'keep')) -eq 0) 'install again (update)'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match 'sfr2HalfEyeUU = 2\.9') 'update: the player''s Halcyon value kept'

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
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'performance')) -eq 0) 'install on a game never started'
$out = Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')
Check ($out -match "hasn't been started on this PC yet" -and $out -match '(?m)^game settings waiting for the game''s first start: 3072 x 3188') 'install: says so, and the settings wait'
Check (-not (Test-Path $gamerFile)) 'install: no GamerSettings.ini made up'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match '(?m)^sfr2ResX = 4096') 'install: MELEVR.ini''s target waits too'
Check ((Run-Setup @('--repair')) -eq 0) 'repair before the first start'
Check ((Get-Content -Raw (Join-Path $scratch 'setup-cli.txt')) -match 'still wait for the game''s first start') 'repair: the settings still wait'
# The game's first start makes its file (as the game writes it: full screen, its own resolution).
$firstGamer = "[SystemSettings]`r`nFullscreen=True`r`nResX=1920`r`nResY=1080`r`nUseVSync=True`r`nMotionBlur=True`r`n`r`n[HDR]`r`nDesiredRange=DynamicRange_HDR`r`n"
[IO.File]::WriteAllText($gamerFile, $firstGamer)
Check ((Run-Setup @('--repair')) -eq 0) 'repair after the first start'
$g = Get-Content -Raw $gamerFile
Check ($g -match '(?m)^ResX=3072' -and $g -match '(?m)^ResY=3188' -and $g -match '(?m)^BorderlessWindow=True' -and $g -match '(?m)^Fullscreen=False' -and $g -match 'DynamicRange_SDR') 'repair: the waiting VR game settings applied'
Check ((Get-Content -Raw (Join-Path $win64 'ASI\MELEVR.ini')) -match '(?m)^sfr2ResX = 3072') 'repair: MELEVR.ini''s target with them'
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

Write-Output $(if ($failures -eq 0) { 'fake game test passed' } else { "fake game test FAILED ($failures)" })
exit $failures
