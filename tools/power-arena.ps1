# SPDX-License-Identifier: MIT
# A powers QA arena in one command (research/55 "Spawning enemies for a powers arena", 2026-11-07): backs up the saves and
# settings, starts the game, lands at UNC20, gets Shepard out of the Mako, turns on god mode, gives the powers we know the
# cheats for, wakes the level's mercenary squad and spawns a ring of hostile soldiers around Shepard that fight. Put the headset on once it
# says READY.
#
#   tools/power-arena.ps1                    the whole run (the game must be closed; SteamVR with the headset should be up)
#   tools/power-arena.ps1 -Count 25          more soldiers (at most 40 a call)
#   tools/power-arena.ps1 -Here              the game already running at UNC20 (a save loaded there): out of the Mako, god
#                                            mode, powers, the squad awake, soldiers; no backup, no launch
#   tools/power-arena.ps1 -Refill            the game already in the arena: more soldiers, nothing else
#   tools/power-arena.ps1 -Restore           after closing the game: the saves and settings put back from the last backup
#
# Opening UNC20 and the Mako's landing autosave into the career: always -Restore after the session (the backup is in
# %LOCALAPPDATA%\MEVR\arena-backup\<stamp>, its path in latest.txt). The players' build needs [Debug] requests = 1 for the
# console: the run sets it and -Restore puts the file back. Needs a build with `mevr spawnpawn` (main 34c8209 or later).
param(
    [int]$Count = 15,
    [double]$DistanceM = 20,
    [double]$SpreadM = 2.5,
    [string]$Type = 'BIOA_UNC_C.Mercenary.MIN_MercenaryHumanMaleRocketSoldier',
    [switch]$Refill,
    [switch]$Here,
    [switch]$Restore,
    [switch]$NoPowers,
    [switch]$Row              # a row ahead instead of a ring all around (Shepard leaves the Mako facing it)
)
$ErrorActionPreference = 'Stop'

$game = $null
foreach ($root in @("${env:ProgramFiles(x86)}\Steam\steamapps\common\Mass Effect Legendary Edition", "$env:ProgramFiles\EA Games\Mass Effect Legendary Edition")) {
    if (Test-Path "$root\Game\ME1\Binaries\Win64\MassEffect1.exe") { $game = "$root\Game\ME1"; break }
}
if (-not $game) { throw 'Mass Effect Legendary Edition not found (Steam or EA app default folder)' }
$asi = "$game\Binaries\Win64\ASI"
$saves = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition\Save'
$backupRoot = "$env:LOCALAPPDATA\MEVR\arena-backup"
$settings = @("$asi\LE1MEVR.ini", "$asi\MELEVR.ini", "$game\BioGame\Config\GamerSettings.ini", "$game\BioGame\Config\BIOEngine.ini")

function Invoke-Console([string[]]$lines) {
    $out = & "$PSScriptRoot\console.ps1" @lines 2>$null
    return ($out -join "`n")
}
function Get-Mode {
    $state = Invoke-Console @('mevr vehicle')
    if ($state -match 'map (\S+), game mode (-?\d+),') { return @{ map = $Matches[1]; mode = [int]$Matches[2] } }
    return $null
}
function Wait-Mode([int]$mode, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $m = Get-Mode
        if ($m -and $m.mode -eq $mode) { return $m }
        Start-Sleep -Seconds 2
    }
    return $null
}
function Spawn-Row {
    $layout = if ($Row) { 'hostile' } else { 'hostile,ring' }
    $out = Invoke-Console @("mevr spawnpawn $Type $Count $DistanceM $SpreadM $layout")
    $made = ([regex]::Matches($out, 'hostile, ChangeSquads\(\w+\) true')).Count
    Write-Host ("spawned $made of $Count hostile soldiers $DistanceM m " + $(if ($Row) { 'ahead' } else { 'around you' }))
    if ($made -eq 0) { Write-Warning $out }
}

if ($Restore) {
    if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { throw 'Close the game first: its files are in use and it would write over the restore' }
    $latest = Get-Content "$backupRoot\latest.txt" -ErrorAction Stop
    if (-not (Test-Path "$latest\Save")) { throw "no backup at $latest" }
    # The saves exactly as they were: files the run made are removed, changed ones copied back.
    $before = Get-ChildItem "$latest\Save" -Recurse -File | ForEach-Object { $_.FullName.Substring("$latest\Save".Length) }
    Get-ChildItem $saves -Recurse -File | Where-Object { $before -notcontains $_.FullName.Substring($saves.Length) } | Remove-Item -Force
    Copy-Item "$latest\Save\*" $saves -Recurse -Force
    foreach ($f in $settings) { $b = Join-Path $latest (Split-Path $f -Leaf); if (Test-Path $b) { Copy-Item $b $f -Force } }
    $diff = Compare-Object (Get-ChildItem "$latest\Save" -Recurse -File | ForEach-Object { (Get-FileHash $_.FullName).Hash }) (Get-ChildItem $saves -Recurse -File | ForEach-Object { (Get-FileHash $_.FullName).Hash })
    Write-Host ("restored from $latest; saves " + $(if ($diff) { "DIFFER in $(($diff | Measure-Object).Count) file(s)" } else { 'identical to the backup' }))
    exit 0
}

if ($Refill) {
    $m = Get-Mode
    if (-not $m -or $m.mode -ne 0) { throw "not on foot in the game (mevr vehicle: $($m.map) mode $($m.mode))" }
    Spawn-Row
    exit 0
}

$started = Get-Date
if (-not $Here) {
if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { throw 'Close the game first (or -Here / -Refill in a running game)' }

# 1. The backup, outside the game and save trees.
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$backup = "$backupRoot\$stamp"
New-Item -ItemType Directory -Force $backup | Out-Null
Copy-Item $saves "$backup\Save" -Recurse
foreach ($f in $settings) { if (Test-Path $f) { Copy-Item $f $backup } }
Set-Content "$backupRoot\latest.txt" $backup
Write-Host "backed up the saves and settings to $backup"

# 2. The console for the players' build ([Debug] requests = 1; -Restore puts the file back).
$ini = "$asi\LE1MEVR.ini"
$text = [IO.File]::ReadAllText($ini)
if ($text -notmatch '(?m)^\s*requests\s*=\s*1') {
    if ($text -match '(?m)^\[Debug\]\r?$') { $text = $text -replace '(?m)^(\[Debug\]\r?\n)', "`$1requests = 1`r`n" }
    else { $text = $text.TrimEnd() + "`r`n`r`n[Debug]`r`nrequests = 1`r`n" }
    [IO.File]::WriteAllText($ini, $text)
}

# 3. The game, the landing, on foot.
& "$PSScriptRoot\launch-game.ps1" -Steam | Select-Object -Last 1 | Write-Host
if (-not (Get-Mode)) { throw 'the game did not answer its console after the launch' }
& "$PSScriptRoot\mako-start.ps1" *> $null
}
$m = Get-Mode
if (-not $m) { throw 'the game does not answer its console (is it running, with [Debug] requests = 1 in ASI\LE1MEVR.ini?)' }
if ($m -and $m.mode -eq 1) {
    Invoke-Console @('mevr vehicle exit') | Out-Null
    $m = Wait-Mode 0 30
}
if (-not $m -or $m.map -ne 'BIOA_UNC20' -or $m.mode -ne 0) { throw "not on foot at UNC20 (mevr vehicle: $($m.map) mode $($m.mode))" }

# 4. God mode, the powers, the level's mercenaries awake, a row of ours.
Invoke-Console @('god') | Out-Null
if (-not $NoPowers) {
    # Bonus talents: GiveBonusTalent then ScaleTalentByIncrement (research/55: Throw 49, Singularity 57, Barrier 63, Overload 84;
    # the other powers' ids are still to be read from BIOG_2DA_Talents_X.Talent_BonusTalents).
    $grant = @()
    foreach ($id in 49, 57, 63, 84) { $grant += "GiveBonusTalent $id"; $grant += "ScaleTalentByIncrement $id 1" }
    Invoke-Console $grant | Out-Null
    Write-Host 'powers given: Throw, Singularity, Barrier, Overload (plus the class own)'
}
$list = Invoke-Console @('mevr instances BioSeqAct_ModifyPropertyPawn 00_DSG.TheWorld.PersistentLevel.Main_Sequence.BioSeq')
if ($list -match '\[(\d+)\][^\n]*ModifyPropertyPawn_1\b') {
    Invoke-Console @("mevr instances BioSeqAct_ModifyPropertyPawn 00_DSG.TheWorld.PersistentLevel.Main_Sequence.BioSeq", "mevr activate $($Matches[1]) 0") | Out-Null
    Write-Host "woke the level's mercenary squad"
} else {
    Write-Warning "UNC20's wake action wasn't found: the spawned soldiers may stand idle"
}
Spawn-Row
Write-Host ("READY in {0:N0} s: put the headset on. More: tools/power-arena.ps1 -Refill. After closing the game: tools/power-arena.ps1 -Restore" -f ((Get-Date) - $started).TotalSeconds)
