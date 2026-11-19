# SPDX-License-Identifier: MIT
# One unattended run in the game on the fake headset, with the machine put back after (research/69 "Heads at the drawn eyes";
# the skill `first-person-scene-audit`). In order: deploy a build, back up the ME1 saves and the ASI folder's inis, the fake
# headset on, the game through Steam, wait for a player in the world, run your script; then always, whatever happened: quit
# the game, the fake headset off, every save put back as it was (files the run made moved aside, never deleted), the inis put
# back, the mod's log kept. Everything lands in %LOCALAPPDATA%\MEVR\sessions\<stamp>-<tag>.
#
#   tools/fake-headset-session.ps1 -Tag heads -Run my-run.ps1 [-BuildDir build-mono] [-NoDeploy] [-IgnoreSlot] [-Audit]
#   tools/fake-headset-session.ps1 -Tag heads -Command "tools\conversation-from-save.ps1 -All -Career John02 -Only 85 -Stances"
#   tools/fake-headset-session.ps1 -Restore <session folder>     the putting back alone (a run that was stopped half way)
#
# The run script is dot-sourced from the repository's root, with $Session (the session folder) and $Began (its start,
# yyyy-MM-ddTHH:mm:ss, for `--since`) set. -Audit prints `tools/conversation-stance.py --audit --since $Began` at the end.
# Refuses while the game runs. Deploying over a held game slot (tools/game-slot.ps1) needs -IgnoreSlot; this script never
# returns a slot. The players' build (build-mono) reads the test loop's request files (console.request, which every tool
# here talks through) only with `[Debug] requests = 1` in ASI\LE1MEVR.ini; the script stops before launching without it.
param(
    [string]$Tag = 'run',
    [string]$Run = '',
    [string]$Command = '',
    [string]$BuildDir = 'build-mono',
    [switch]$NoDeploy,
    [switch]$IgnoreSlot,
    [switch]$Audit,
    [int]$WaitSeconds = 180,
    [string]$Restore = '',
    # [Debug] calibrationKey in ASI\LE1MEVR.ini for the run (a controller type, or 'owner': calibration.ini's [Calibration] last,
    # read only; tools/owner-calibration.ps1): the null driver names no controller type. Development builds only; the inis
    # are put back after.
    [string]$CalibrationKey = ''
)
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game\ME1'
$asi = Join-Path $game 'Binaries\Win64\ASI'
$saves = Join-Path $env:USERPROFILE 'Documents\BioWare\Mass Effect Legendary Edition\Save\ME1'
$log = Join-Path $game 'Binaries\Win64\Logs\LE1MEVR.log'

function Say([string]$text) { Write-Output ("{0} {1}" -f (Get-Date -Format HH:mm:ss), $text) }

function Put-Back([string]$session) {
    # The game closed first: its quit through the console, then by force after 40 s.
    if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) {
        & (Join-Path $PSScriptRoot 'launch-game.ps1') -Quit *> (Join-Path $session 'quit.log')
        $d = (Get-Date).AddSeconds(40)
        while ((Get-Process MassEffect1 -ErrorAction SilentlyContinue) -and (Get-Date) -lt $d) { Start-Sleep -Seconds 2 }
        if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) {
            Get-Process MassEffect1 | Stop-Process -Force -Confirm:$false
            Start-Sleep -Seconds 3
            Say 'the game did not quit by itself: stopped'
        }
    }
    & (Join-Path $PSScriptRoot 'fake-headset.ps1') -Off *> (Join-Path $session 'fake-off.log')
    Say ('fake headset: ' + ((Get-Content (Join-Path $session 'fake-off.log') | Where-Object { "$_" -match '\S' } | Select-Object -Last 1) -join ' '))
    # The saves: changed files back from the backup, files the run made moved aside (a file the game just wrote can be busy
    # for a moment: docs/knowledge/gotchas/fresh-file-busy-for-a-moment).
    $kept = Join-Path $session 'backup\Save'
    $aside = Join-Path $session 'made-by-the-run'
    $restored = 0; $same = 0; $moved = 0
    foreach ($f in Get-ChildItem $kept -Recurse -File) {
        $rel = $f.FullName.Substring($kept.Length + 1)
        $live = Join-Path $saves $rel
        if ((Test-Path $live) -and (Get-FileHash $live).Hash -eq (Get-FileHash $f.FullName).Hash) { $same++; continue }
        New-Item -ItemType Directory -Force (Split-Path $live) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $live -Force
        $restored++; Say "save restored: $rel"
    }
    foreach ($f in Get-ChildItem $saves -Recurse -File) {
        $rel = $f.FullName.Substring($saves.Length + 1)
        if (Test-Path (Join-Path $kept $rel)) { continue }
        New-Item -ItemType Directory -Force (Split-Path (Join-Path $aside $rel)) | Out-Null
        $done = $false
        for ($try = 0; $try -lt 10 -and -not $done; $try++) {
            try { Move-Item -LiteralPath $f.FullName -Destination (Join-Path $aside $rel) -ErrorAction Stop; $done = $true }
            catch { Start-Sleep -Milliseconds 500 }
        }
        if ($done) { $moved++; Say "save made by the run, moved aside: $rel" } else { Say "STILL BUSY, left in place: $rel" }
    }
    Say "saves: $same unchanged, $restored restored, $moved moved aside to $aside"
    foreach ($f in Get-ChildItem (Join-Path $session 'backup\ASI') -File -ErrorAction SilentlyContinue) {
        $live = Join-Path $asi $f.Name
        if (-not (Test-Path $live) -or (Get-FileHash $live).Hash -ne (Get-FileHash $f.FullName).Hash) {
            Copy-Item -LiteralPath $f.FullName -Destination $live -Force; Say "ini restored: $($f.Name)"
        }
    }
    # The three games' settings.ini and the old files' taken copies (research/25 section 20): what the run's mod and host
    # wrote there goes, as the ASI folder's inis do. A run that made them (the first launch after the update) leaves none.
    $localKept = Join-Path $session 'backup\local'
    $mevrLocal = Join-Path $env:LOCALAPPDATA 'MEVR'
    if (Test-Path (Join-Path $localKept 'settings.ini')) {
        $live = Join-Path $mevrLocal 'settings.ini'
        if (-not (Test-Path $live) -or (Get-FileHash $live).Hash -ne (Get-FileHash (Join-Path $localKept 'settings.ini')).Hash) {
            Copy-Item -LiteralPath (Join-Path $localKept 'settings.ini') -Destination $live -Force; Say 'settings.ini restored'
        }
    } elseif (Test-Path (Join-Path $localKept 'no-settings.txt')) {
        if (Test-Path (Join-Path $mevrLocal 'settings.ini')) { Remove-Item -LiteralPath (Join-Path $mevrLocal 'settings.ini') -Force; Say 'settings.ini made by the run: removed' }
    }
    if (Test-Path (Join-Path $localKept 'settings-taken')) {
        Remove-Item -Recurse -Force (Join-Path $mevrLocal 'settings-taken') -ErrorAction SilentlyContinue
        Copy-Item -Recurse -Force (Join-Path $localKept 'settings-taken') (Join-Path $mevrLocal 'settings-taken')
    } elseif (Test-Path (Join-Path $localKept 'no-settings-taken.txt')) {
        Remove-Item -Recurse -Force (Join-Path $mevrLocal 'settings-taken') -ErrorAction SilentlyContinue
    }
    # The sequels' old inis, byte for byte (the settings migration marks them).
    $sequelInis = Join-Path $session 'backup\sequels'
    foreach ($pair in @(@('ME2', 'LE2'), @('ME3', 'LE3'))) {
        $kept = Join-Path $sequelInis "$($pair[1])MEVR.ini"
        $live = Join-Path (Split-Path -Parent $game) ("{0}\Binaries\Win64\ASI\{1}MEVR.ini" -f $pair[0], $pair[1])
        if ((Test-Path -LiteralPath $kept) -and (-not (Test-Path -LiteralPath $live) -or (Get-FileHash -LiteralPath $live).Hash -ne (Get-FileHash -LiteralPath $kept).Hash)) {
            Copy-Item -LiteralPath $kept -Destination $live -Force; Say "ini restored: $($pair[1])MEVR.ini"
        }
    }
    if (Test-Path $log) { Copy-Item $log (Join-Path $session 'LE1MEVR.log') -Force }
    Say ('the ASI in the game: ' + (Get-Item (Join-Path $asi 'LE1MEVR.asi')).VersionInfo.ProductVersion)
}

if ($Restore) {
    if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { Say 'the game runs: it is closed first' }
    Put-Back $Restore
    return
}
if (-not $Run -and -not $Command) { throw 'nothing to run: -Run <script> or -Command <line>' }
if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { throw 'the game is running: not touching anything' }

$Session = Join-Path $env:LOCALAPPDATA ("MEVR\sessions\{0}-{1}" -f (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'), $Tag)
New-Item -ItemType Directory -Force (Join-Path $Session 'backup\Save'), (Join-Path $Session 'backup\ASI') | Out-Null
Say "session: $Session"
if (-not $NoDeploy) {
    $deployArgs = @{ Game = 'LE1'; BuildDir = $BuildDir }
    if ($IgnoreSlot) { $deployArgs.IgnoreSlot = $true }
    & (Join-Path $PSScriptRoot 'deploy-asi.ps1') @deployArgs *> (Join-Path $Session 'deploy.log')
    if (-not $?) { throw "the deploy failed: $(Join-Path $Session 'deploy.log')" }
}
Say ('the ASI in the game: ' + (Get-Item (Join-Path $asi 'LE1MEVR.asi')).VersionInfo.ProductVersion)
$mono = -not (Test-Path (Join-Path $game 'Binaries\Win64\ASI\LE1MEVR.modpath'))
if ($mono -and -not (Select-String -Path (Join-Path $asi 'LE1MEVR.ini') -Pattern '^\s*requests\s*=\s*1' -Quiet)) {
    throw 'the players'' build reads no console requests without [Debug] requests = 1 in ASI\LE1MEVR.ini: add it, or deploy the development build'
}
Copy-Item -Recurse -Force (Join-Path $saves '*') (Join-Path $Session 'backup\Save')
Copy-Item -Force (Join-Path $asi '*.ini') (Join-Path $Session 'backup\ASI')
# The three games' settings.ini and the taken copies (research/25 section 20), put back by Put-Back.
$localKept = Join-Path $Session 'backup\local'
New-Item -ItemType Directory -Force $localKept | Out-Null
$mevrLocal = Join-Path $env:LOCALAPPDATA 'MEVR'
if (Test-Path (Join-Path $mevrLocal 'settings.ini')) { Copy-Item -Force (Join-Path $mevrLocal 'settings.ini') $localKept }
else { Set-Content -LiteralPath (Join-Path $localKept 'no-settings.txt') -Value 'none before the run' }
if (Test-Path (Join-Path $mevrLocal 'settings-taken')) { Copy-Item -Recurse -Force (Join-Path $mevrLocal 'settings-taken') (Join-Path $localKept 'settings-taken') }
else { Set-Content -LiteralPath (Join-Path $localKept 'no-settings-taken.txt') -Value 'none before the run' }
# The sequels' old files too: the host's first attach after the update takes ME2's and ME3's ASI\LE{2,3}MEVR.ini into
# settings.ini and marks them `[Settings] shared = 1`; a run that then removes the settings.ini it made must put them back,
# or the player's ME2/ME3 files stay marked with no shared file (2026-11-19, the coordinator's LE1 check).
$sequelInis = Join-Path $Session 'backup\sequels'
New-Item -ItemType Directory -Force $sequelInis | Out-Null
foreach ($pair in @(@('ME2', 'LE2'), @('ME3', 'LE3'))) {
    $file = Join-Path (Split-Path -Parent $game) ("{0}\Binaries\Win64\ASI\{1}MEVR.ini" -f $pair[0], $pair[1])
    if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination (Join-Path $sequelInis "$($pair[1])MEVR.ini") -Force }
}
if ($CalibrationKey) {
    if ($CalibrationKey -eq 'owner') {
        . (Join-Path $PSScriptRoot 'owner-calibration.ps1')
        $CalibrationKey = (Get-OwnerCalibration).Key
    }
    if ($CalibrationKey -notmatch '^[A-Za-z0-9_]+$') { throw "-CalibrationKey '$CalibrationKey' is not a controller type's key" }
    if ($mono) { Say 'warning: the players'' build ignores [Debug] calibrationKey: deploy a development build (-BuildDir build)' }
    Add-Type -Namespace MevrSession -Name Profile -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
'@
    # The game's old file (backed up above): the host takes its keys into this game's settings at attach.
    [void][MevrSession.Profile]::WritePrivateProfileString('Debug', 'calibrationKey', $CalibrationKey, (Join-Path $asi 'LE1MEVR.ini'))
    Say "[Debug] calibrationKey = $CalibrationKey for the run"
}
$Began = Get-Date -Format 'yyyy-MM-ddTHH:mm:ss'
Say "began: $Began (backups in the session folder)"
try {
    & (Join-Path $PSScriptRoot 'fake-headset.ps1') -On *> (Join-Path $Session 'fake-on.log')
    & (Join-Path $PSScriptRoot 'launch-game.ps1') -Steam *> (Join-Path $Session 'launch.log')
    $d = (Get-Date).AddSeconds($WaitSeconds); $state = ''
    while ((Get-Date) -lt $d) {
        $state = (& (Join-Path $PSScriptRoot 'console.ps1') 'mevr convo' 2>&1 | Where-Object { "$_" -match '\S' } | Select-Object -Last 1) -join ' '
        if ($state -match 'game mode (0|1)') { break }
        Start-Sleep -Seconds 4
    }
    Say "in the game: $state"
    if ($state -notmatch 'game mode (0|1)') { throw "no player in the world after $WaitSeconds s (see launch.log)" }
    if ($Run) { . (Resolve-Path $Run) } else { Invoke-Expression $Command }
}
catch { Say "the run stopped: $_" }
finally {
    Put-Back $Session
    if ($Audit) { & python -I (Join-Path $PSScriptRoot 'conversation-stance.py') --audit --since $Began }
    Say "done: $Session"
}
