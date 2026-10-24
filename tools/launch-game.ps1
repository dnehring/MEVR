# SPDX-License-Identifier: MIT
# Starts LE1 without the launcher's PLAY click or a person at the keyboard (research/35, the measuring loop), then at
# the title screen presses Enter (which resumes the last save) and waits until the mod reports gameplay ("on foot
# yes" in its log with the test loop's status fresh). With the fake headset on (tools/fake-headset.ps1 -On) this
# gives the full VR path with nobody in the headset.
#
# How the Steam edition launches (read from Steam's and the EA app's logs, 2026-10-24): Steam -> EA's Link2EA.exe
# (link2ea://launchgame/1328670?platform=steam) -> the EA app -> EASteamProxy.exe MassEffectLauncher.exe -> the
# launcher -> MassEffect1.exe. Only Link2EA reaches the EA app; MassEffect1.exe and MassEffectLauncher.exe started
# directly exit at once with 100010, and steam://rungameid / -applaunch are logged by Steam but start nothing. The
# URL's cmdParams reach the launcher's command line, where LEBinkProxy's launcher module boots ME1 itself for
# `-game 1 -autoterminate` (no PLAY). The chain is started from a scheduled task, never from this shell: a process
# spawned here sits in the harness's job object, and the EA app it spawns then fails every launch with "Create
# Process: Access is denied" (its proxy needs to break away from the job). The task runs with the user's own token.
#
#   tools/launch-game.ps1                 start, resume the last save, wait (up to 4 minutes), say READY
#   tools/launch-game.ps1 -Pause          ... and freeze the world (tools/console.ps1 pause) once in gameplay
#   tools/launch-game.ps1 -Quit           close a running game through its console (the mod's console.request)
#   tools/launch-game.ps1 -Attach        don't start anything: wait for MassEffect1.exe started another way, then
#                                         the same Enter and READY wait
#   tools/launch-game.ps1 -Map BIOA_STA60 open that map straight from the command line (UE3's first argument; a
#                                         fresh Shepard at the map's player start, no save): a known place for
#                                         measurements. In gameplay the same is tools/console.ps1 'open BIOA_STA60'.
#                                         LE1 maps: BIOA_PRO10 Eden Prime, BIOA_STA00 Citadel docking bay, BIOA_STA20
#                                         Presidium, BIOA_STA30 Wards, BIOA_STA60 Citadel Tower, BIOA_NOR10 Normandy.
#
# The mouse does nothing in VR mode (Halcyon decouples it); the keyboard works, so Enter goes through SendKeys to the
# game's window. Exit code 1 when the game never reached gameplay.
param([switch]$Pause, [switch]$Quit, [switch]$Attach, [int]$TimeoutSeconds = 240, [string]$Map = '')
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
if ($Attach) {
    $p = $null
    for ($i = 0; $i -lt 60 -and -not $p; $i++) { $p = Get-Process MassEffect1 -ErrorAction SilentlyContinue; if (-not $p) { Start-Sleep 2 } }
    if (-not $p) { 'no MassEffect1.exe within 120 s'; exit 1 }
    "attached to MassEffect1.exe (pid $($p.Id), started $($p.StartTime.ToString('HH:mm:ss')))"
} else {
    if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { throw 'the game is already running' }
    if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { throw 'Steam is not running (the game needs it)' }
    $link2ea = 'C:\Program Files\Electronic Arts\EA Desktop\EA Desktop\Link2EA.exe'
    if (-not (Test-Path -LiteralPath $link2ea)) { throw "Link2EA.exe not found at $link2ea (the EA app)" }
    $params = '-game 1 -autoterminate'
    if ($Map -ne '') { $params = $Map + ' ' + $params }
    $url = 'link2ea://launchgame/1328670?platform=steam&theme=met&cmdParams=' + [uri]::EscapeDataString($params)
    $action = New-ScheduledTaskAction -Execute $link2ea -Argument $url -WorkingDirectory (Split-Path $link2ea)
    Register-ScheduledTask -TaskName 'MEVR-LaunchLE1' -Action $action -Force | Out-Null
    Start-ScheduledTask -TaskName 'MEVR-LaunchLE1'
    "asked the EA app for the game through Link2EA at $(Get-Date -Format HH:mm:ss) ($params)"
    # The launcher's PLAY: cmdParams only sometimes reach its command line (seen once of three), so once the launcher
    # window is up and no game appears within 12 s, Space (its splash) and Enter (PLAY) go to it by keyboard, every 10 s.
    Add-Type -AssemblyName Microsoft.VisualBasic
    Add-Type -AssemblyName System.Windows.Forms
    $p = $null; $launcherSeen = $null; $lastKeys = Get-Date
    for ($i = 0; $i -lt 60 -and -not $p; $i++) {
        Start-Sleep 2
        $p = Get-Process MassEffect1 -ErrorAction SilentlyContinue
        if ($p) { break }
        $l = Get-Process MassEffectLauncher -ErrorAction SilentlyContinue
        if ($l -and -not $launcherSeen) { $launcherSeen = Get-Date; "launcher up (pid $($l.Id))" }
        if ($l -and ((Get-Date) - $launcherSeen).TotalSeconds -gt 12 -and ((Get-Date) - $lastKeys).TotalSeconds -gt 10) {
            try {
                [Microsoft.VisualBasic.Interaction]::AppActivate($l.Id); Start-Sleep -Milliseconds 600
                [System.Windows.Forms.SendKeys]::SendWait(' '); Start-Sleep -Milliseconds 900
                [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
                $lastKeys = Get-Date; 'Space and Enter sent to the launcher (PLAY)'
            } catch { "keys to the launcher failed: $($_.Exception.Message)" }
        }
    }
    if (-not $p) { 'no MassEffect1.exe within 120 s: the EA app did not launch it (its log: %LOCALAPPDATA%\Electronic Arts\EA Desktop\Logs\EADesktopVerbose.log)'; exit 1 }
    "MassEffect1.exe running (pid $($p.Id)) at $(Get-Date -Format HH:mm:ss)"
}
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
