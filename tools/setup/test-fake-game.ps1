# SPDX-License-Identifier: MIT
# Tests MEVR-Setup.exe's command line against a FAKE game folder (research/33); never the real game.
#
# Builds scratch\fakegame\Mass Effect Legendary Edition with a stand-in MassEffect1.exe (the real version number,
# tools/setup/FakeGame), a copy of RAD's bink2w64 DLL (the real game's bink2w64_original.dll, read only, or -BinkDll)
# as bink2w64.dll, and small GamerSettings.ini / BIOEngine.ini files; then runs status, install, a tamper + repair,
# restore and apply of the game settings, and uninstall, and checks the folder is byte for byte what it was.
#
#   tools\setup\test-fake-game.ps1 [-Build build] [-Payload <folder>] [-BinkDll <path>]
param(
    [string]$Build = (Join-Path $PSScriptRoot '..\..\build'),
    [string]$Payload,
    [string]$BinkDll
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Build = (Resolve-Path $Build).Path
$scratch = Join-Path $repo 'scratch'
$root = Join-Path $scratch 'fakegame\Mass Effect Legendary Edition'
$win64 = Join-Path $root 'Game\ME1\Binaries\Win64'
$config = Join-Path $root 'Game\ME1\BioGame\Config'
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
if (-not $Payload) {
    $Payload = Join-Path $scratch 'payload'
    Remove-Item -Recurse -Force $Payload -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $Payload | Out-Null
    $asi = Join-Path $repo 'build-mono\Release\LE1MEVR.asi'
    if (-not (Test-Path $asi)) { $asi = Join-Path $Build 'Release\LE1MEVR.asi' }
    Copy-Item $asi $Payload
    Copy-Item (Join-Path $Build 'Release\bink2w64.dll') $Payload
    Copy-Item -Recurse (Join-Path $repo 'third_party\fonts') (Join-Path $Payload 'fonts')
    Copy-Item -Recurse (Join-Path $repo 'assets\menu') (Join-Path $Payload 'menu')
}

# The fake game.
Remove-Item -Recurse -Force (Join-Path $scratch 'fakegame') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $win64, $config | Out-Null
Copy-Item $fakeExe $win64
Copy-Item $BinkDll (Join-Path $win64 'bink2w64.dll')
$gamer = "[SystemSettings]`r`nFullscreen=True`r`nResX=2560`r`nResY=1440`r`nUseVSync=True`r`nMotionBlur=True`r`nFilmGrain=True`r`nDynamicShadows=True`r`n`r`n[HDR]`r`nDesiredRange=DynamicRange_HDR`r`n"
[IO.File]::WriteAllText((Join-Path $config 'GamerSettings.ini'), $gamer)
[IO.File]::WriteAllText((Join-Path $config 'BIOEngine.ini'), "[Engine.GameEngine]`r`nSmoothFrameRate=TRUE`r`nMaxSmoothedFrameRate=62`r`n")
$before = Snapshot
$ErrorActionPreference = 'Continue'  # from here on a failed check is reported, not thrown

Check ((Run-Setup @('--status')) -eq 0) 'status'
Check ((Run-Setup @('--install', '--source', 'Steam', '--quality', 'performance')) -eq 0) 'install'
$bink = (Get-Item (Join-Path $win64 'bink2w64.dll')).VersionInfo
Check ($bink.ProductName -eq 'MEVR ASI loader') 'bink2w64.dll is our loader'
Check ((Get-FileHash (Join-Path $win64 'bink2w64_original.dll')).Hash -eq (Get-FileHash $BinkDll).Hash) 'RAD''s DLL kept as bink2w64_original.dll'
Check (Test-Path (Join-Path $win64 'ASI\LE1MEVR.asi')) 'the ASI'
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

# The player tunes Halcyon in game (a value we never touch): it must survive everything below.
$melevr = $melevr -replace '(?m)^sfr2HalfEyeUU = .*$', 'sfr2HalfEyeUU = 2.9'
if ($melevr -notmatch 'sfr2HalfEyeUU') { $melevr = $melevr -replace '\[profile:Default\]\r\n', "[profile:Default]`r`nsfr2HalfEyeUU = 2.9`r`n" }
[IO.File]::WriteAllText((Join-Path $win64 'ASI\MELEVR.ini'), $melevr)

# Steam's "verify files" puts RAD's DLL back, and a menu file goes missing: Repair.
Copy-Item $BinkDll (Join-Path $win64 'bink2w64.dll') -Force
Remove-Item (Join-Path $win64 'ASI\menu\main.rcss')
Check ((Run-Setup @('--repair')) -eq 0) 'repair'
Check ((Get-Item (Join-Path $win64 'bink2w64.dll')).VersionInfo.ProductName -eq 'MEVR ASI loader') 'repair: our loader back'
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

Write-Output $(if ($failures -eq 0) { 'fake game test passed' } else { "fake game test FAILED ($failures)" })
exit $failures
