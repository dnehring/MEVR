# SPDX-License-Identifier: MIT
# The galaxy map over the CIC's projector on a sequel (ME1's design, research/51 section 8.1; docs/galaxy-projector-handoff.md):
# with a stretch open on the Normandy at the galaxy map's console (prep.ps1, launch.ps1, load.ps1; ME3's original AutoSave
# stands at it), this reads the mod's projector flow (`mevr galaxy projector`, `mevr galaxy capture state`), takes a snapshot on
# foot, replays an A press at the console (tools/make-walk-recording.py: the head turned -Yaw and pitched -Pitch, the plan
# -Plan, replayed with its inputs), waits for the map's mode, takes snapshots in the map (the eyes, the window's layers, the
# UI panel), then replays the map's exit (X on ME3's system map, B in orbit) and checks that the view is the game's again.
# Results in the stretch's session folder, galaxy-<tag>-<time>\.
#
#   tools\le2-halcyon-run\galaxy.ps1 -Holder <you> [-Game LE3] [-Yaw 10] [-Pitch -20] [-Plan stand:2,a:0.25,stand:60]
#       [-MapSeconds 12] [-Tag what] [-NoExit] [-Variants on-raw,on-default,on]
# -Variants: in the map, each `mevr galaxy capture <variant>` in turn (the captures made again with it), a snapshot after each.
# -PickUv U,V: then the right controller's ray at that point of the map's UI panel (the snapshot's `hud` quad; U, V of its
# -hud.png) and its trigger pulled once (tools/make-galaxy-pick-recording.py --panel-uv), the game pointer's click there.
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE3',
    [double]$Yaw = 10,
    [double]$Pitch = -20,
    [ValidatePattern('^[a-z0-9:.,-]+$')][string]$Plan = 'stand:2,a:0.25,stand:90',
    [int]$MapSeconds = 12,
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'cic',
    [switch]$NoExit,
    [ValidatePattern('^[a-z,-]*$')][string]$Variants = '',
    [ValidatePattern('^([0-9.]+,[0-9.]+)?$')][string]$PickUv = ''
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$name = if ($Game -eq 'LE3') { 'ME3' } else { 'ME2' }
$current = Join-Path $env:LOCALAPPDATA ("MEVR\sessions\{0}-current.json" -f $Game.ToLowerInvariant())
if (-not (Test-Path -LiteralPath $current)) { throw "no $name stretch is open: prep.ps1, launch.ps1, load.ps1 first" }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open $name stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process ("MassEffect" + $Game.Substring(2)) -ErrorAction SilentlyContinue)) { throw "$name is not running" }
$out = Join-Path $open.session ('galaxy-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
$log = Join-Path $bin ("Logs\{0}MEVR.log" -f $Game)
$mapModes = if ($Game -eq 'LE3') { '1[12]' } else { '9|10' }

function Ask([string]$command, [int]$timeout = 10) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds $timeout 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
function ReadLog {
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -ne '' }) } finally { $reader.Close() }
}
function Snapshot([string]$what) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\snapshot.ps1') -Tag ("galaxy-" + $Tag + "-" + $what) |
        Add-Content -LiteralPath (Join-Path $out 'snapshots.txt') -Encoding utf8
}
function Mode {
    $state = ''
    try { $state = Ask 'mevr state' 5 } catch { }
    if ($state -match 'game mode (\d+)') { return [int]$Matches[1] }
    return -1
}
function Replay([string]$plan, [string]$file) {
    $path = Join-Path $out $file
    & python (Join-Path $W 'tools\make-walk-recording.py') $path --plan $plan --yaw $Yaw --pitch $Pitch |
        Add-Content -LiteralPath (Join-Path $out 'recordings.txt') -Encoding utf8
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Path $path -Inputs | Out-Null
}
function StopReplay { & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Stop | Out-Null }
$verdicts = New-Object System.Collections.Generic.List[string]
function Verdict([string]$what, [bool]$ok, [string]$detail) {
    $line = '{0} {1}: {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' }), $what, $detail)
    $verdicts.Add($line)
    Write-Output $line
}

$from = (ReadLog).Count
"state: " + (Ask 'mevr state')
"projector: " + (Ask 'mevr galaxy projector')
Start-Sleep -Seconds 3  # the flow measures the projector every 2 s on foot
$onFoot = Ask 'mevr galaxy capture state'
"on foot: $onFoot"
Verdict 'the projector measured on foot, the room retention armed' ($onFoot -match '"discValid":\s*true' -and $onFoot -match '"retentionArmed":\s*true') $onFoot
Snapshot 'foot'

Replay $Plan 'use.mevrrec'
$deadline = (Get-Date).AddSeconds(25)
do { Start-Sleep -Seconds 1; $mode = Mode } until ($mode -match "^($mapModes)$" -or (Get-Date) -gt $deadline)
Verdict "the map's mode after A" ("$mode" -match "^($mapModes)$") "game mode $mode"
Start-Sleep -Seconds $MapSeconds
$inMap = Ask 'mevr galaxy capture state'
"in the map: $inMap"
"galaxy: " + (Ask 'mevr galaxy state')
Verdict 'the view held, the window on the projector' ($inMap -match '"holding":\s*true' -and $inMap -match '"windowOn":\s*true') $inMap
Snapshot 'map1'
Start-Sleep -Seconds 3
Snapshot 'map2'
foreach ($variant in ($Variants -split ',' | Where-Object { $_ })) {
    "variant $($variant): " + (Ask "mevr galaxy capture $variant")
    Start-Sleep -Seconds 5
    Snapshot ("map-" + $variant)
}
StopReplay
if ($PickUv) {
    $snapshotJson = Get-ChildItem (Join-Path $env:LOCALAPPDATA 'MEVR\snapshots') -Filter '*.json' | Sort-Object LastWriteTime | Select-Object -Last 1
    $uv = $PickUv -split ','
    $pick = Join-Path $out 'pick.mevrrec'
    & python (Join-Path $W 'tools\make-galaxy-pick-recording.py') --snapshot $snapshotJson.FullName pick $pick --panel-uv $uv[0] $uv[1] --layer hud --seconds 10 --pick-at 4 |
        Add-Content -LiteralPath (Join-Path $out 'recordings.txt') -Encoding utf8
    $before = Ask 'mevr galaxy state'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Path $pick -Inputs | Out-Null
    Start-Sleep -Seconds 3
    Snapshot 'pick-aim'
    Start-Sleep -Seconds 6
    Snapshot 'pick-after'
    StopReplay
    $after = Ask 'mevr galaxy state'
    $clicks = @((ReadLog) | Select-Object -Skip $from | Where-Object { $_ -match 'game pointer|gui cursor' })
    Verdict 'the pointer on the map UI clicked' (@($clicks | Where-Object { $_ -match 'click' }).Count -gt 0) (($clicks | Select-Object -Last 3) -join ' | ')
    "galaxy before the pick: $before"
    "galaxy after the pick: $after"
}

if (-not $NoExit) {
    foreach ($button in 'x', 'b', 'x') {
        if ((Mode) -notmatch "^($mapModes)$") { break }
        Replay "stand:1,$($button):0.25,stand:4" ("exit-" + $button + ".mevrrec")
        Start-Sleep -Seconds 6
        StopReplay
    }
    $deadline = (Get-Date).AddSeconds(20)
    do { Start-Sleep -Seconds 1; $mode = Mode } until ($mode -eq 0 -or (Get-Date) -gt $deadline)
    Verdict 'on foot again after the exit' ($mode -eq 0) "game mode $mode"
    Start-Sleep -Seconds 3
    $after = Ask 'mevr galaxy capture state'
    "after: $after"
    Verdict 'the hold and the window released' ($after -match '"holding":\s*false' -and $after -match '"windowOn":\s*false') $after
    Snapshot 'after'
}

$lines = @((ReadLog) | Select-Object -Skip $from | Where-Object { $_ -match 'galaxy|scene capture|retention|overlays: game UI|swapchains' })
$lines | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$verdicts | Set-Content -LiteralPath (Join-Path $out 'verdicts.txt') -Encoding utf8
"results: $out"
