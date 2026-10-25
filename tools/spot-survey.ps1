# SPDX-License-Identifier: MIT
# Measures the frame at a list of places (research/35 attempt 42): for each "X Y Z", moves the player's pawn there with
# the console's `set SFXPawn_Player Location (...)` (Actor.SetLocation through ProcessEvent refused to move the player;
# `set` writes the property and the next tick takes it), waits for streaming, replays a head recording in a loop so the
# view moves, runs a short frame probe, and prints the fpsVR-style CPU time and the draws. The places come from
# `tools/console.ps1 "mevr density 2000"` (the densest mesh cells) or `mevr loc` (where the player stood).
#
#   tools/spot-survey.ps1 -Spots "-35000 147000 150", "-33000 149000 0" [-Seconds 6] [-Recording <.mevrrec>] [-Tag x]
param(
    [Parameter(Mandatory = $true)][string[]]$Spots,
    [int]$Seconds = 6,
    [int]$SettleSeconds = 4,
    [string]$Recording = '',
    [string]$Tag = 'spot'
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
if (-not (Get-Process MassEffect1 -ErrorAction SilentlyContinue)) { throw 'the game is not running' }
if ($Recording -eq '') {
    $dir = Join-Path $env:LOCALAPPDATA 'MEVR\recordings'
    $Recording = (Get-ChildItem -LiteralPath $dir -Filter *.mevrrec | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
$perf = Join-Path $env:LOCALAPPDATA 'MEVR\perf'
$log = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game\ME1\Binaries\Win64\Logs\LE1MEVR.log'
# One argument may carry several spots separated by commas or semicolons (powershell -File joins them that way).
$Spots = @($Spots | ForEach-Object { $_ -split '[,;]' } | Where-Object { $_.Trim() -ne '' })
# The game's own cheats: no damage, no collision (the pawn hangs where it is put instead of falling to its death).
& (Join-Path $tools 'console.ps1') 'god' 'ghost' | Out-Null
& (Join-Path $tools 'replay.ps1') -Path $Recording -Loop | Out-Null
try {
    $i = 0
    "{0,-26} {1,7} {2,7} {3,7} {4,7} {5,7} {6,6}  {7}" -f 'spot (X Y Z)', 'cpu med', 'cpu p90', 'cpu max', 'draws', 'drw p90', 'frames', 'pawn'
    foreach ($spot in $Spots) {
        $i++
        $p = $spot.Trim() -split '\s+'
        if ($p.Count -lt 3) { "skipping '$spot'"; continue }
        & (Join-Path $tools 'console.ps1') ("set SFXPawn_Player Location (X=$($p[0]),Y=$($p[1]),Z=$($p[2]))") | Out-Null
        Start-Sleep $SettleSeconds
        $loc = (& (Join-Path $tools 'console.ps1') 'mevr loc' | Select-String -SimpleMatch 'loc ').Line
        if (-not (Get-Content $log -Tail 40 | Select-String -SimpleMatch 'on foot yes' -Quiet)) {
            "{0,-26} not on foot after the move (dead or a menu): {1}" -f $spot, $loc
            continue
        }
        $probeTag = "$Tag$i"
        & (Join-Path $tools 'perf-probe.ps1') -Seconds $Seconds -Tag $probeTag | Out-Null
        $probe = Get-ChildItem $perf -Filter "*-$probeTag-probe.json" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        $text = python -I (Join-Path $tools 'perf-probe.py') $probe.FullName
        $nums = { param($line) @($line -split '\s+' | Where-Object { $_ -match '^[0-9.]+$' }) }
        $cpu = & $nums ($text | Select-String -SimpleMatch 'poses -> submit, wall' | Select-Object -First 1).Line
        $draws = & $nums ($text | Select-String -Pattern '^\s+whole frame\s' | Select-Object -First 1).Line
        $frames = ($text | Select-String -Pattern '^(\d+) .*headset frames' | Select-Object -First 1).Matches[0].Groups[1].Value
        "{0,-26} {1,7} {2,7} {3,7} {4,7} {5,7} {6,6}  {7}" -f $spot, $cpu[0], $cpu[1], $cpu[2], $draws[0], $draws[1], $frames, $loc
    }
} finally {
    & (Join-Path $tools 'replay.ps1') -Stop | Out-Null
}
