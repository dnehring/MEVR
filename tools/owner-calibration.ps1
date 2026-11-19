# SPDX-License-Identifier: MIT
# The owner's own calibration for the posture runs on the fake headset (dot-source it). The null driver names no controller
# type, so without help a run uses no calibration at all and the crouch thresholds are the typical ones; the owner's are
# much tighter (2026-11-19: Quest 3, standing 1.869 m, crouched 1.558 m: Shepard crouches about 15 cm under standing, and a
# reach toward someone crouched him until [Hands] crouchMinDropM and crouchLeanRatio). Three pieces:
#   Get-OwnerCalibration      reads %LOCALAPPDATA%\MEVR\calibration.ini, READ ONLY (never written by a run): [Calibration]
#                             last's section (the controller type, standEyeM, crouchEyeM, crouchDropM, position).
#   [Debug] calibrationKey    in the game's settings stands in for the runtime's controller type (development builds only;
#                             the host reads it once, at its first frame: put it in before the launch).
#   --stand-eye <standEyeM>   for tools/make-walk-recording.py: the replays' floor samples at the owner's real height
#                             (research/le2-vrik.md "The replay STAGE-eye gap"), so his thresholds mean what they mean.
#
#   . tools\owner-calibration.ps1
#   $cal = Get-OwnerCalibration            # .Key .StandEyeM .CrouchEyeM .CrouchDropM .Position .Time .Label
#   Use-OwnerCalibrationLe2 -Holder <you>  # an open ME2 stretch: the key in its ASI\LE2MEVR.ini, ME2 relaunched and Save 4
#                                          # loaded again if the running game doesn't use it yet
Set-StrictMode -Off

function Read-MevrIni([string]$path) {
    # Sections and keys as Win32 reads them (names case-insensitive, the first wins); UTF-8 or UTF-16 by the BOM.
    $sections = @{}
    $current = $null
    foreach ($raw in [IO.File]::ReadAllLines($path)) {
        $line = $raw.Trim()
        if ($line -eq '' -or $line.StartsWith(';') -or $line.StartsWith('#')) { continue }
        if ($line -match '^\[(.+)\]$') {
            $name = $Matches[1].Trim().ToLowerInvariant()
            if (-not $sections.ContainsKey($name)) { $sections[$name] = @{} }
            $current = $sections[$name]
            continue
        }
        if ($null -ne $current -and $line -match '^([^=]+)=(.*)$') {
            $key = $Matches[1].Trim().ToLowerInvariant()
            if (-not $current.ContainsKey($key)) { $current[$key] = $Matches[2].Trim() }
        }
    }
    return $sections
}

function Get-OwnerCalibration([string]$Key = '') {
    $path = Join-Path $env:LOCALAPPDATA 'MEVR\calibration.ini'
    if (-not (Test-Path -LiteralPath $path)) { throw "no calibration file at ${path}: run without the owner's calibration (-OwnerCalibration:`$false)" }
    $ini = Read-MevrIni $path
    if (-not $Key) {
        $Key = if ($ini.ContainsKey('calibration')) { $ini['calibration']['last'] } else { '' }
        if (-not $Key) { throw "$path names no [Calibration] last" }
    }
    $section = $ini['calibration.' + $Key.ToLowerInvariant()]
    if (-not $section -or -not $section['standeyem']) { throw "$path has no measured [Calibration.$Key]" }
    $number = { param($v) if ($v) { [double]::Parse($v, [Globalization.CultureInfo]::InvariantCulture) } else { $null } }
    return [pscustomobject]@{
        Key         = $Key
        Path        = $path
        Label       = $section['label']
        Position    = if ($section['position']) { $section['position'] } else { 'standing' }
        StandEyeM   = & $number $section['standeyem']
        CrouchEyeM  = & $number $section['croucheyem']
        CrouchDropM = & $number $section['crouchdropm']
        Time        = $section['time']
        Sha256      = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
}

# The calibration in use by the running game, from its mod log: the host's stand-in line and the mod's calibration line.
function Test-OwnerCalibrationInLog([string[]]$lines, [string]$key) {
    $standsIn = @($lines | Where-Object { $_ -match [regex]::Escape("calibrationKey $key stands in") }).Count -gt 0
    $applied = @($lines | Where-Object { $_ -match [regex]::Escape("[Calibration.$key]") -and $_ -match 'eyes [0-9.]+ m standing' }).Count -gt 0
    return $standsIn -and $applied
}

# An open ME2 stretch (tools/le2-halcyon-run): makes the running game use the owner's calibration. The stretch's
# ASI\LE2MEVR.ini is its own ([Debug] shared = 0, prep backed it up, restore.ps1 puts it back); the key is read at the
# host's first frame, so a game that doesn't use it yet is relaunched (launch.ps1 -Kill) and Save 4 loaded again
# (load.ps1). Returns the calibration (Get-OwnerCalibration) with .Relaunched.
function Use-OwnerCalibrationLe2([string]$Holder, [string]$Save = '') {
    . "$PSScriptRoot\common.ps1"
    $cal = Get-OwnerCalibration
    if ($cal.Position -eq 'seated') { throw "the owner's [Calibration.$($cal.Key)] is seated: physical crouch is off seated" }
    $bin = Get-MELEBinDir (Find-MELEInstall).Root 'LE2'
    $log = Join-Path $bin 'Logs\LE2MEVR.log'
    $ini = Join-Path $bin 'ASI\LE2MEVR.ini'
    $read = {
        $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        $reader = New-Object IO.StreamReader($stream)
        try { @($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Close() }
    }
    $relaunched = $false
    if (-not (Test-OwnerCalibrationInLog (& $read) $cal.Key)) {
        if (-not (Select-String -LiteralPath $ini -Pattern '^\s*shared\s*=\s*0' -Quiet)) {
            throw "$ini is not a stretch's own file ([Debug] shared = 0): prep.ps1 first"
        }
        $text = [IO.File]::ReadAllText($ini)
        if ($text -match '(?m)^\s*calibrationKey\s*=.*$') {
            $text = $text -replace '(?m)^\s*calibrationKey\s*=.*$', "calibrationKey = $($cal.Key)"
        } else {
            $text = $text -replace '(?m)^\[Debug\]\s*$', "[Debug]`r`ncalibrationKey = $($cal.Key)"
        }
        [IO.File]::WriteAllText($ini, $text)
        Write-Host "owner calibration: [Debug] calibrationKey = $($cal.Key) in $ini; relaunching ME2 for it"
        & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\le2-halcyon-run\launch.ps1" -Holder $Holder -Kill | Select-Object -Last 3 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'launch.ps1 -Kill failed' }
        $loadArgs = @('-Holder', $Holder)
        if ($Save) { $loadArgs += @('-Save', $Save) }
        & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\le2-halcyon-run\load.ps1" @loadArgs | Select-Object -Last 3 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'load.ps1 failed' }
        $relaunched = $true
        for ($i = 0; $i -lt 20 -and -not (Test-OwnerCalibrationInLog (& $read) $cal.Key); $i++) { Start-Sleep -Seconds 1 }
        if (-not (Test-OwnerCalibrationInLog (& $read) $cal.Key)) {
            throw "the game still doesn't use [Calibration.$($cal.Key)] (no 'calibrationKey ... stands in' and calibration line in $log): a development build?"
        }
    }
    $cal | Add-Member -NotePropertyName Relaunched -NotePropertyValue $relaunched -Force
    return $cal
}

# Our main menu opens by itself the first time tracked controllers appear in a fresh settings file (the controller guide,
# `[Menu] controlsShown`): a stretch's first replay with hands opened it, and the menu pauses the game, so a whole posture run
# measured a frozen world (2026-11-19: the pawn rose 33 units with each crouch and stand, no physics to put him back). Closes it
# with the right B (a replayed tap, as a player's B), if this launch's log shows it open. True if a menu was closed.
function Close-FirstRunMenuLe2([string]$Session) {
    . "$PSScriptRoot\common.ps1"
    $log = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE2') 'Logs\LE2MEVR.log'
    $read = {
        $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        $reader = New-Object IO.StreamReader($stream)
        try { @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -match 'main menu: (opened|closed)' }) } finally { $reader.Close() }
    }
    $events = @(& $read)
    if ($events.Count -eq 0 -or $events[-1] -notmatch 'main menu: opened') { return $false }
    Write-Host "the main menu is open ($($events[-1].Trim())): closing it with B"
    $path = Join-Path $Session 'close-menu-b.mevrrec'
    & python (Join-Path $PSScriptRoot 'make-walk-recording.py') $path --plan 'stand:1,b:0.15,stand:1.5' --pitch -20 --hands view | Out-Null
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'replay.ps1') -Path $path -Inputs | Out-Null
    for ($i = 0; $i -lt 10; $i++) {
        Start-Sleep -Seconds 1
        $events = @(& $read)
        if ($events[-1] -match 'main menu: closed') { return $true }
    }
    throw "the main menu is still open after a replayed B: $log"
}
