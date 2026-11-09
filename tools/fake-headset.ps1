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
#   Steam\config\chaperone_info.vrchap: a universe "2" (the null driver's) at the zero pose with a 1 x 1 m play area.
#     Without it the chaperone's calibration state is 201 (universe uninitialised) and vrmonitor launches room setup the
#     first time the universe appears after a SteamVR start; room setup is a scene app, so vrserver sends the running
#     scene app (the game) a Quit event and KILLS it 5 s later ("Kill process MassEffect1 because it didn't quit in
#     time"; the game died at the title with no crash record, 2026-10-27 19:45, 22:35 and 22:36). A second launch
#     survived because the game then evicted room setup (which exits at once with no real headset).
# The game must not be running while the driver changes.
param([switch]$On, [switch]$Off, [switch]$Status, [int]$Width = 2824, [int]$Height = 2880,
      [string]$SteamRoot = 'C:\Program Files (x86)\Steam', [string]$SteamVRRoot = '',
      [string]$BackupDirectory = '', [int]$AppResolutionScale = 0)
$ErrorActionPreference = 'Stop'

$steam = $SteamRoot
if (-not $SteamVRRoot) { $SteamVRRoot = Join-Path $steam 'steamapps\common\SteamVR' }
$nullSettings = Join-Path $SteamVRRoot 'drivers\null\resources\settings\default.vrsettings'
$vrSettings = Join-Path $steam 'config\steamvr.vrsettings'
$chaperone = Join-Path $steam 'config\chaperone_info.vrchap'
$vrStartup = Join-Path $SteamVRRoot 'bin\win64\vrstartup.exe'
$backupDir = if ($BackupDirectory) { $BackupDirectory } else { Join-Path $env:LOCALAPPDATA 'MEVR\fake-headset' }
$nullUniverse = '2'   # the null driver's chaperone universe ("Update ZeroPose for the chaperone universe 2" in vrserver.txt)

function Read-Json($path) { Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
function Write-Json($path, $obj) {
    # UTF-8 without a BOM: SteamVR's own reader writes none
    [IO.File]::WriteAllText($path, ($obj | ConvertTo-Json -Depth 10), (New-Object Text.UTF8Encoding $false))
}
function Has-NullUniverse($chap) { [bool]($chap.universes | Where-Object { "$($_.universeID)" -eq $nullUniverse }) }
function New-NullUniverse {
    # a 1 x 1 m square around the zero pose, walls 2.43 m high (the user's own entry's shape), standing and seated at
    # the identity, the zero pose SteamVR used for this universe before it was registered
    $w = 0.5; $h = 2.43
    $corners = @(@(-$w, -$w), @(-$w, $w), @($w, $w), @($w, -$w))
    $walls = @()
    for ($i = 0; $i -lt 4; $i++) {
        $a = $corners[$i]; $b = $corners[($i + 1) % 4]
        $walls += , @(@($a[0], 0, $a[1]), @($a[0], $h, $a[1]), @($b[0], $h, $b[1]), @($b[0], 0, $b[1]))
    }
    [pscustomobject]@{
        collision_bounds = $walls
        play_area        = @(1, 1)
        seated           = [pscustomobject]@{ translation = @(0, 0, 0); yaw = 0 }
        standing         = [pscustomobject]@{ translation = @(0, 0, 0); yaw = 0 }
        time             = (Get-Date).ToString('ddd MMM dd HH:mm:ss yyyy', [Globalization.CultureInfo]::InvariantCulture)
        universeID       = $nullUniverse
    }
}
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
    "chaperone universe $nullUniverse (the null driver's) registered: $(if (Test-Path -LiteralPath $chaperone) { Has-NullUniverse (Read-Json $chaperone) } else { 'no chaperone file' })"
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
    if ($AppResolutionScale -gt 0) { Set-Member (Section $s 'steam.app.1328670') 'resolutionScale' $AppResolutionScale }
    Set-Member (Section $s 'driver_playstation_vr2') 'enable' $false
    $pw = Section $s 'power'
    Set-Member $pw 'pauseCompositorOnStandby' $false
    Set-Member $pw 'turnOffScreensTimeout' 36000
    Write-Json $vrSettings $s
    # the null driver's universe, so vrmonitor never launches room setup over the game (see the header)
    $chapBackup = Join-Path $backupDir 'chaperone_info.vrchap'
    if (Test-Path -LiteralPath $chaperone) {
        if (-not (Test-Path -LiteralPath $chapBackup)) { Copy-Item -LiteralPath $chaperone $chapBackup }
        $c = Read-Json $chaperone
    } else {
        $c = [pscustomobject]@{ jsonid = 'chaperone_info'; universes = @(); version = 5 }
    }
    if (-not (Has-NullUniverse $c)) {
        $c.universes = @($c.universes) + @(New-NullUniverse)
        Write-Json $chaperone $c
    }
    Start-Process -FilePath $vrStartup -WindowStyle Hidden | Out-Null
    "fake headset ON: null driver ${Width}x${Height} @ 90 Hz, the PS VR2 driver off, standby off, chaperone universe $nullUniverse registered; SteamVR starting (backups in $backupDir)"
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
    $chapBackup = Join-Path $backupDir 'chaperone_info.vrchap'
    if (Test-Path -LiteralPath $chapBackup) { Copy-Item -LiteralPath $chapBackup $chaperone -Force }
    elseif (Test-Path -LiteralPath $chaperone) {
        $c = Read-Json $chaperone
        $c.universes = @($c.universes | Where-Object { "$($_.universeID)" -ne $nullUniverse })
        Write-Json $chaperone $c
    }
    "fake headset OFF: the user's SteamVR settings and chaperone restored; SteamVR stopped (start it with the headset on)"
}
