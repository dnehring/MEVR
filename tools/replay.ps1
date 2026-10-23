# SPDX-License-Identifier: MIT
# Plays a pose recording back in the running game (LE1, Halcyon's renderer) as if it were the live
# headset and controllers: the recorded head poses (with the runtime's FOVs) and controller poses.
# Buttons and sticks replay only with -Inputs (so a replay doesn't fire weapons unless asked).
# The VR session must be running (headset awake); see notes/next-vr-session.md, "Test loop".
#
#   tools/replay.ps1                          the newest recording
#   tools/replay.ps1 -Path <file> -Loop       loop one
#   tools/replay.ps1 -Inputs -Wait            with buttons and sticks; wait until it ends
#   tools/replay.ps1 -Stop
# Combine with tools/snapshot.ps1 to look at a replayed pose:
#   tools/replay.ps1 -Loop; tools/snapshot.ps1 -Delay 2; tools/replay.ps1 -Stop
param(
    [string]$Path = '',
    [switch]$Loop,
    [switch]$Inputs,
    [switch]$Stop,
    [switch]$Wait
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

if ($Stop) {
    $text = 'stop'
} else {
    if ($Path) {
        $text = (Resolve-Path -LiteralPath $Path).Path
    } else {
        $text = 'latest'
    }
    if ($Loop) { $text += ' loop=1' }
    if ($Inputs) { $text += ' inputs=1' }
}
$request = Write-MevrRequest 'replay.request' $text
Write-Host "Requested: replay $text"

$deadline = (Get-Date).AddSeconds(5)
while ((Test-Path $request) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
if (Test-Path $request) {
    Write-Warning "The game hasn't picked up the request (is it running with the host ASI deployed?)"
    exit 1
}
Start-Sleep -Milliseconds 1500
$status = Get-MevrTestLoopStatus
if (-not $status) { exit 0 }
if ($Stop) {
    Write-Host "Replaying: $($status.replaying)"
    exit 0
}
if (-not $status.replaying) {
    Write-Warning "Not replaying: see the game's log (<Win64>\Logs\LE1MEVR.log, 'test loop:' lines)."
    if (-not $status.framesRunning) { Write-Warning 'No headset frames are running (VR session stopped).' }
    exit 1
}
Write-Host ("Replaying {0} ({1:N1} of {2:N1} s{3})" -f $status.replayPath, $status.replaySeconds, $status.replayDuration,
    $(if ($status.replayLoop) { ', looping' } else { '' }))
if ($Wait -and -not $status.replayLoop) {
    do {
        Start-Sleep -Seconds 1
        $status = Get-MevrTestLoopStatus
    } while ($status -and $status.replaying)
    Write-Host 'Replay finished.'
}
