# SPDX-License-Identifier: MIT
# Starts LE1 without the launcher or a person at the keyboard (research/35, the measuring loop): MassEffect1.exe with
# the launcher's own arguments (as the installer's Launch page does), the Steam app id in the environment, then at the
# title screen Enter (which resumes the last save), and waits until the mod reports gameplay ("on foot yes" in its
# log with the test loop's status fresh). With the fake headset on (tools/fake-headset.ps1 -On) this gives the full
# VR path with nobody in the headset.
#
#   tools/launch-game.ps1                 start, resume the last save, wait (up to 4 minutes), say READY
#   tools/launch-game.ps1 -Pause          ... and freeze the world (tools/console.ps1 pause) once in gameplay
#   tools/launch-game.ps1 -Quit           close a running game through its console (the mod's console.request)
#
# The mouse does nothing in VR mode (Halcyon decouples it); the keyboard works, so Enter goes through SendKeys to the
# game's window. Exit code 1 when the game never reached gameplay.
param([switch]$Pause, [switch]$Quit, [int]$TimeoutSeconds = 240)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition'
$win64 = Join-Path $root 'Game\ME1\Binaries\Win64'
$exe = Join-Path $win64 'MassEffect1.exe'
$log = Join-Path $win64 'Logs\LE1MEVR.log'
$status = Join-Path $env:LOCALAPPDATA 'MEVR\testloop.status'

if ($Quit) {
    if (-not (Get-Process MassEffect1 -ErrorAction SilentlyContinue)) { 'not running'; return }
    & (Join-Path $tools 'console.ps1') 'exit' | Out-Null
    for ($i = 0; $i -lt 60 -and (Get-Process MassEffect1 -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
    if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { 'still running after 60 s'; exit 1 }
    'game closed'
    return
}
if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { throw 'the game is already running' }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { throw 'Steam is not running (the game needs it)' }

$env:SteamAppId = '1328670'; $env:SteamGameId = '1328670'; $env:SteamOverlayGameId = '1328670'
$args = '-NoHomeDir -SeekFreeLoadingPCConsole -locale int -Subtitles 20 -OVERRIDELANGUAGE=INT'
$p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $win64 -PassThru
"started MassEffect1.exe (pid $($p.Id)) at $(Get-Date -Format HH:mm:ss)"
Add-Type -AssemblyName Microsoft.VisualBasic
Add-Type -AssemblyName System.Windows.Forms

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$enterSent = 0
$lastEnter = Get-Date
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
    if ($p.HasExited) { "the game exited (code $($p.ExitCode))"; exit 1 }
    $logItem = Get-Item $log -ErrorAction SilentlyContinue
    if (-not $logItem -or $logItem.LastWriteTime -lt $p.StartTime) { continue }
    $fresh = (Test-Path $status) -and (((Get-Date) - (Get-Item $status).LastWriteTime).TotalSeconds -lt 5)
    $onFoot = (Get-Content $log -Tail 40 | Select-String -SimpleMatch 'on foot yes' -Quiet)
    if ($fresh -and $onFoot) {
        "READY: gameplay at $(Get-Date -Format HH:mm:ss) (Enter sent $enterSent time(s))"
        if ($Pause) { Start-Sleep 3; & (Join-Path $tools 'console.ps1') 'pause' | Out-Null; 'world paused' }
        return
    }
    # Frames running but not on foot: the title screen (or the save's loading). Enter resumes the last save; one every
    # 12 s, from 25 s after the start.
    if ($fresh -and ((Get-Date) - $p.StartTime).TotalSeconds -gt 25 -and ((Get-Date) - $lastEnter).TotalSeconds -gt 12) {
        try {
            [Microsoft.VisualBasic.Interaction]::AppActivate($p.Id)
            Start-Sleep -Milliseconds 600
            [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
            $enterSent++; $lastEnter = Get-Date
            "Enter sent at $(Get-Date -Format HH:mm:ss)"
        } catch { "Enter failed: $($_.Exception.Message)" }
    }
}
"TIMEOUT: no gameplay within $TimeoutSeconds s"
exit 1
