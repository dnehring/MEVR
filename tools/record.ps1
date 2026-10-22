# Starts or stops a pose recording in the running game (LE1, Halcyon's renderer): the head's eye
# views and both controllers' poses, buttons and sticks, every headset frame, to
# %LOCALAPPDATA%\MEVR\recordings\<stamp>.mevrrec (format: src/core/vr/PoseRecording.hpp).
# The player can also use Home or the VR hands menu tab.
#
#   tools/record.ps1 -Seconds 20   record for 20 s, wait, print the file
#   tools/record.ps1 -Start        start (until -Stop)
#   tools/record.ps1 -Stop         stop, print the file
param(
    [switch]$Start,
    [switch]$Stop,
    [double]$Seconds = 0
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

if ($Stop) {
    $text = 'stop'
} elseif ($Start -or $Seconds -gt 0) {
    $text = 'start'
    if ($Seconds -gt 0) { $text += " seconds=$Seconds" }
} else {
    $text = 'toggle'
}
$request = Write-MevrRequest 'record.request' $text
Write-Host "Requested: $text"

# Wait for the game to act on it (it polls once a second).
$deadline = (Get-Date).AddSeconds(5)
while ((Test-Path $request) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
if (Test-Path $request) {
    Write-Warning "The game hasn't picked up the request (is it running with the host ASI deployed?)"
    exit 1
}
if ($Seconds -gt 0) {
    Write-Host "Recording for $Seconds s..."
    Start-Sleep -Seconds ([math]::Ceiling($Seconds + 2))
}
Start-Sleep -Milliseconds 1500  # the status file is rewritten once a second
$status = Get-MevrTestLoopStatus
if ($status) {
    if ($status.recording) {
        Write-Host "Recording ($([math]::Round($status.recordingSeconds)) s so far)."
    } else {
        Write-Host "Not recording. Last recording: $($status.lastRecording)"
    }
    if (-not $status.framesRunning) {
        Write-Warning 'No headset frames are running: nothing is recorded until the VR session runs.'
    }
}
