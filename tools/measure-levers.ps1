# SPDX-License-Identifier: MIT
# A render-lever A/B under replayed head motion, for the fake headset (research/35, the measuring loop): starts a
# looping replay of a head recording (the view then turns as the player's did, which is what makes occlusion and
# draw-count effects show; a still view hides them), runs tools/perf-ab.ps1 for the key, stops the replay.
#
#   tools/measure-levers.ps1 -Key flushFirstEye -Values 0,1 -Rounds 2 -Seconds 10
#   tools/measure-levers.ps1 -Key perEyeOcclusion -Values 0,1 -Recording <path.mevrrec> -Inputs   (walking as recorded)
#   tools/measure-levers.ps1 -Game LE2 -Key keepBoundShaderStates -Values 0,1                     (a sequel, in its protected run)
#
# Default recording: the newest in %LOCALAPPDATA%\MEVR\recordings. The game must be in gameplay (tools/launch-game.ps1
# says READY); freeze the world first if the save is dangerous (tools/console.ps1 pause: the replay still moves the view).
param(
    [Parameter(Mandatory = $true)][string]$Key,
    [Parameter(Mandatory = $true)][string]$Values,
    [int]$Rounds = 2,
    [int]$Seconds = 10,
    [string]$Recording = '',
    [switch]$Inputs,   # replay the recording's buttons and sticks too: Shepard walks and shoots as the player did
    [string]$Section = 'Performance',  # the key's section of the game's MEVR ini (Sights for scopeCapture, research/39)
    [ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE1'   # the running game (research/sequel-render-levers.md: the sequels)
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
if (-not (Get-Process "MassEffect$($Game.Substring(2))" -ErrorAction SilentlyContinue)) { throw "the game ($Game) is not running" }
if ($Recording -eq '') {
    $dir = Join-Path $env:LOCALAPPDATA 'MEVR\recordings'
    $latest = Get-ChildItem -LiteralPath $dir -Filter *.mevrrec -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw "no recording in $dir (tools/record.ps1 -Seconds 20 while someone looks around)" }
    $Recording = $latest.FullName
}
"replaying $Recording in a loop"
& (Join-Path $tools 'replay.ps1') -Path $Recording -Loop -Inputs:$Inputs | Out-Null
Start-Sleep -Seconds 3
try {
    & (Join-Path $tools 'perf-ab.ps1') -Game $Game -Section $Section -Key $Key -Values ($Values -split ',') -Rounds $Rounds -Seconds $Seconds
} finally {
    & (Join-Path $tools 'replay.ps1') -Stop | Out-Null
    'replay stopped'
}
