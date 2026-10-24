# SPDX-License-Identifier: MIT
# The fake headset: SteamVR's null driver standing in for the real one, so the game runs its full VR path (two
# Stereo 2 renders a frame, 90 Hz pacing, every probe) with nobody in the headset (research/35, "Setup for measuring
# without the headset").
#
#   tools/fake-headset.ps1 -On        null driver on, the real driver off, standby disabled; SteamVR restarted
#   tools/fake-headset.ps1 -Off       the user's settings back (from the backups the first -On made); SteamVR stopped
#   tools/fake-headset.ps1 -Status    what the files say now
#
# Edits (JSON, backed up next to this script's data in %LOCALAPPDATA%\MEVR\fake-headset\ before the first change):
#   SteamVR\drivers\null\resources\settings\default.vrsettings: driver_null.enable, renderWidth/Height (the PS VR2's eye
#     size 2824x2880), displayFrequency 90, windowWidth/Height 1920x1080
#   Steam\config\steamvr.vrsettings: steamvr.forcedDriver = "null", steamvr.activateMultipleDrivers = false,
#     driver_playstation_vr2.enable = false, power.pauseCompositorOnStandby = false, power.turnOffScreensTimeout = 36000
#     (without the power keys the null headset goes to standby 5 s after each wake and the session never leaves READY).
# The game must not be running while the driver changes.
param([switch]$On, [switch]$Off, [switch]$Status, [int]$Width = 2824, [int]$Height = 2880)
$ErrorActionPreference = 'Stop'

$steam = 'C:\Program Files (x86)\Steam'
$nullSettings = Join-Path $steam 'steamapps\common\SteamVR\drivers\null\resources\settings\default.vrsettings'
$vrSettings = Join-Path $steam 'config\steamvr.vrsettings'
$vrStartup = Join-Path $steam 'steamapps\common\SteamVR\bin\win64\vrstartup.exe'
$backupDir = Join-Path $env:LOCALAPPDATA 'MEVR\fake-headset'

function Read-Json($path) { Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
function Write-Json($path, $obj) { $obj | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding utf8 }
function Set-Member($obj, $name, $value) {
    if ($null -eq $obj.PSObject.Properties[$name]) { $obj | Add-Member -NotePropertyName $name -NotePropertyValue $value }
    else { $obj.$name = $value }
}
function Section($root, $name) {
    if ($null -eq $root.PSObject.Properties[$name]) { $root | Add-Member -NotePropertyName $name -NotePropertyValue ([pscustomobject]@{}) }
    return $root.$name
}
function Stop-SteamVR {
    foreach ($n in 'vrmonitor', 'vrserver', 'vrcompositor', 'vrdashboard', 'vrwebhelper', 'vrstartup') {
        Get-Process $n -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
}

if ($Status -or -not ($On -or $Off)) {
    $n = Read-Json $nullSettings; $s = Read-Json $vrSettings
    $forced = if ($s.steamvr.PSObject.Properties['forcedDriver']) { $s.steamvr.forcedDriver } else { '(none)' }
    $psvr = if ($s.PSObject.Properties['driver_playstation_vr2'] -and $s.driver_playstation_vr2.PSObject.Properties['enable']) { $s.driver_playstation_vr2.enable } else { '(default)' }
    "null driver enable: $($n.driver_null.enable) ($($n.driver_null.renderWidth)x$($n.driver_null.renderHeight) @ $($n.driver_null.displayFrequency) Hz)"
    "steamvr.forcedDriver: $forced; driver_playstation_vr2.enable: $psvr"
    "SteamVR running: $([bool](Get-Process vrserver -ErrorAction SilentlyContinue)); game running: $([bool](Get-Process MassEffect1 -ErrorAction SilentlyContinue))"
    return
}
if (Get-Process MassEffect1 -ErrorAction SilentlyContinue) { throw 'the game is running: close it first' }

New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
$nullBackup = Join-Path $backupDir 'null-default.vrsettings'
$vrBackup = Join-Path $backupDir 'steamvr.vrsettings'

if ($On) {
    if (-not (Test-Path -LiteralPath $nullBackup)) { Copy-Item -LiteralPath $nullSettings $nullBackup }
    if (-not (Test-Path -LiteralPath $vrBackup)) { Copy-Item -LiteralPath $vrSettings $vrBackup }
    Stop-SteamVR
    $n = Read-Json $nullSettings
    Set-Member $n.driver_null 'enable' $true
    Set-Member $n.driver_null 'renderWidth' $Width
    Set-Member $n.driver_null 'renderHeight' $Height
    Set-Member $n.driver_null 'displayFrequency' 90.0
    Set-Member $n.driver_null 'windowWidth' 1920
    Set-Member $n.driver_null 'windowHeight' 1080
    Write-Json $nullSettings $n
    $s = Read-Json $vrSettings
    $sv = Section $s 'steamvr'
    Set-Member $sv 'forcedDriver' 'null'
    Set-Member $sv 'activateMultipleDrivers' $false
    Set-Member (Section $s 'driver_playstation_vr2') 'enable' $false
    $pw = Section $s 'power'
    Set-Member $pw 'pauseCompositorOnStandby' $false
    Set-Member $pw 'turnOffScreensTimeout' 36000
    Write-Json $vrSettings $s
    Start-Process -FilePath $vrStartup | Out-Null
    "fake headset ON: null driver ${Width}x${Height} @ 90 Hz, the PS VR2 driver off, standby off; SteamVR starting (backups in $backupDir)"
}
if ($Off) {
    Stop-SteamVR
    if (Test-Path -LiteralPath $nullBackup) { Copy-Item -LiteralPath $nullBackup $nullSettings -Force } else {
        $n = Read-Json $nullSettings; Set-Member $n.driver_null 'enable' $false; Write-Json $nullSettings $n
    }
    if (Test-Path -LiteralPath $vrBackup) { Copy-Item -LiteralPath $vrBackup $vrSettings -Force } else {
        $s = Read-Json $vrSettings
        $s.steamvr.PSObject.Properties.Remove('forcedDriver')
        if ($s.PSObject.Properties['driver_playstation_vr2']) { $s.driver_playstation_vr2.PSObject.Properties.Remove('enable') }
        if ($s.PSObject.Properties['power']) { $s.power.PSObject.Properties.Remove('pauseCompositorOnStandby'); $s.power.PSObject.Properties.Remove('turnOffScreensTimeout') }
        Write-Json $vrSettings $s
    }
    "fake headset OFF: the user's SteamVR settings restored; SteamVR stopped (start it with the headset on)"
}
