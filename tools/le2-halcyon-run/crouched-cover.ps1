# SPDX-License-Identifier: MIT
# Lane CCV's check on ME2 (research/23 section 7.8, the owner 2026-11-19: crouching behind a low wall counts as cover even when
# the real eyes stay above it, "for seated players this should of course be necessary"): while the game has Shepard crouched,
# VR cover judges from Shepard's crouched eyes over his floor (core/vr/VrCover JudgingEyes). In an open stretch (prep.ps1,
# launch.ps1, load.ps1: Save 4, Freedom's Progress, on foot), standing hands replayed at the owner's height (-OwnerCalibration,
# tools/owner-calibration.ps1), never `god` (it hides VR cover).
#
#   tools\le2-halcyon-run\crouched-cover.ps1 -Holder <you> [-At x,y,z] [-Shooter n] [-Phases measure,find,button,off,physical]
#       [-Tag what] [-OwnerCalibration:$false]
#
# Phases (each answer in steps.txt; the mod's lines since the start in log.txt, the vr cover lines in summary.txt):
#   measure   `mevr cover eyes` standing and crouched by L3 (the mod's crouch toggle on the sequels): Shepard's heights over his
#             floor (the real eyes, Location + EyeHeight, Location + BaseEyeHeight, CollisionHeight, his animated head bone) and
#             which the rule takes. The honest crouched height is read from these (research/23 section 7.8).
#   find      a low wall: crouched by L3, `mevr cover probe` at every 30 degrees from the crouched eyes, and again with the
#             crouched eyes off (`mevr cover crouch off`: the real eyes); the nearest surface within 1.0 m that only the crouched
#             eyes meet. A squadmate (-Shooter, pawn n of `mevr cover pawns`; the first friend by default) is put 7 m beyond it
#             (`mevr cover put`, at Shepard's height; nearer if refused).
#   button    behind it, `mevr cover hit 30 <shooter>` twice standing (pass: judged from the real eyes, not covered, the shields
#             drop) and twice crouched by L3 (pass: `judged from Shepard's crouched eyes`, covered, the shields unchanged).
#   off       crouched by L3 with `mevr cover crouch off` (the rule before 2026-11-19): two hits, judged from the real eyes, not
#             covered (the A/B); `crouch on` again.
#   physical  the owner's natural crouch (1.52 m, a 0.35 m drop from his 1.869 m: the replays drive the floor sample since
#             2026-11-19): the physical crouch crouches Shepard by the game, two hits judged from his crouched eyes, covered.
# The squadmate stays where it was put (the AI may walk it back); the stretch's restore puts the saves and files back.
param(
    [Parameter(Mandatory)][string]$Holder,
    [double[]]$At = @(),
    [int]$Shooter = -1,
    [string[]]$Phases = @('measure', 'find', 'button', 'off', 'physical'),
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'ccv',
    [switch]$OwnerCalibration = $true
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME2 stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$out = Join-Path $open.session ('crouched-cover-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$journal = Join-Path $out 'steps.txt'
$bin = Get-MELEBinDir (Find-MELEInstall).Root 'LE2'
$log = Join-Path $bin 'Logs\LE2MEVR.log'
$inv = [Globalization.CultureInfo]::InvariantCulture

function Note([string]$text) {
    $line = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss.fff'), $text
    Add-Content -LiteralPath $journal -Value $line -Encoding utf8
    Write-Output $line
}
function Ask([string]$command, [int]$timeout = 10) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds $timeout 2>$null
    $joined = (($text | Where-Object { $_ }) -join "`n")
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n" -Encoding utf8
    return $joined
}
function ReadLog {
    # Shared read: the game keeps the log open for writing.
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -ne '' }) } finally { $reader.Close() }
}
# A replay of a make-walk-recording.py plan with its inputs, the last segment standing long: the next replay takes over (the
# test loop's pending replay), so the controllers never drop out. Returns once the game took it.
$script:count = 0
function Play([string]$plan, [string[]]$extra = @(), [string]$tail = 'stand') {
    $script:count++
    $path = Join-Path $out ('play-{0:D2}.mevrrec' -f $script:count)
    $made = & python (Join-Path $W 'tools\make-walk-recording.py') $path --plan "$plan,${tail}:600" --pitch -20 --hands view @extra @script:standEye
    if ($LASTEXITCODE -ne 0) { throw "make-walk-recording.py failed: $made" }
    Add-Content -LiteralPath (Join-Path $out 'recordings.txt') -Value "$plan $extra : $made" -Encoding utf8
    $request = Write-MevrRequest 'replay.request' ((Resolve-Path -LiteralPath $path).Path + ' inputs=1')
    $deadline = (Get-Date).AddSeconds(5)
    while ((Test-Path -LiteralPath $request) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
}
function Crouched {
    $text = Ask 'mevr cover eyes'
    return $text -match 'Shepard crouched \(bIsCrouched\)'
}
# L3 (the mod's crouch toggle) until the game has Shepard in the wanted posture (at most three presses).
function Posture([bool]$crouch) {
    for ($i = 0; $i -lt 3; $i++) {
        if ((Crouched) -eq $crouch) { return $true }
        Play 'stand:0.5,l3:0.06,stand:1.5'
        Start-Sleep -Seconds 3
    }
    return (Crouched) -eq $crouch
}
function Hits([string]$label, [int]$count = 2) {
    for ($i = 1; $i -le $count; $i++) {
        Note ("{0} hit {1}: {2}" -f $label, $i, (Ask "mevr cover hit 30 $($script:shooter)"))
        Start-Sleep -Milliseconds 700
    }
}
function Eyes([string]$label) { Note ("{0}: {1}" -f $label, (Ask 'mevr cover eyes')) }

# The owner's calibration and height; tracked standing hands; the first-run controller guide closed (it pauses the game).
$script:standEye = @()
. (Join-Path $W 'tools\owner-calibration.ps1')
if ($OwnerCalibration) {
    $cal = Use-OwnerCalibrationLe2 -Holder $Holder
    $script:standEye = @('--stand-eye', $cal.StandEyeM.ToString($inv), '--ease', '0.4')
    Note ("owner calibration [Calibration.{0}]: standing {1} m, crouched {2} m" -f $cal.Key, $cal.StandEyeM, $cal.CrouchEyeM)
}
Play 'stand:1'
Start-Sleep -Seconds 2
if (Close-FirstRunMenuLe2 $out) { Note 'the first-run controller guide was open (the game paused): closed with B'; Play 'stand:1' }
Start-Sleep -Seconds 6  # the posture's standing reference rises to the replayed height
$from = (ReadLog).Count
Note ("crouched-cover {0}: phases {1}; {2}" -f $Tag, ($Phases -join ','), (Ask 'mevr state'))
if ($At.Count -eq 3) {
    Note ('placed: ' + (Ask ('mevr cover put me {0} {1} {2}' -f $At[0].ToString($inv), $At[1].ToString($inv), $At[2].ToString($inv))))
    Start-Sleep -Seconds 2
}
$script:shooter = $Shooter
try {
    $null = Posture $false
    if ($Phases -contains 'measure') {
        Eyes 'measure, standing'
        if (Posture $true) { Start-Sleep -Seconds 2; Eyes 'measure, crouched by L3'; Start-Sleep -Seconds 2; Eyes 'measure, crouched by L3, 2 s on' }
        else { Note 'measure: L3 never crouched Shepard' }
        $null = Posture $false
    }
    if ($Phases -contains 'find') {
        $null = Posture $true
        $here = Ask 'mevr cover eyes'
        $best = $null
        for ($yaw = -180; $yaw -lt 180; $yaw += 30) {
            $low = Ask "mevr cover probe $yaw 1.0"
            $null = Ask 'mevr cover crouch off'
            $high = Ask "mevr cover probe $yaw 1.0"
            $null = Ask 'mevr cover crouch on'
            Note ("find: yaw {0}: crouched eyes: {1} | real eyes: {2}" -f $yaw, $low, $high)
            if ($low -match ' at ([\d.]+) m along yaw (-?\d+): \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)' -and $high -match 'nothing within') {
                $d = [double]$Matches[1]
                if ($null -eq $best -or $d -lt $best.d) {
                    $best = @{ d = $d; yaw = [double]$Matches[2]; x = [double]$Matches[3]; y = [double]$Matches[4]; z = [double]$Matches[5] }
                }
            }
        }
        if (-not $best) { throw 'find: no surface within 1.0 m that only the crouched eyes meet (no low wall here: pass -At)' }
        Note ("find: a low wall {0:0.00} m along yaw {1:0} at ({2:0}, {3:0}, {4:0})" -f $best.d, $best.yaw, $best.x, $best.y, $best.z)
        $pawns = Ask 'mevr cover pawns'
        if ($script:shooter -lt 0) {
            $friend = ($pawns -split "`r?`n" | Where-Object { $_ -match '^\s*\[(\d+)\] (?!hostile )' } | Select-Object -First 1)
            if (-not $friend -or $friend -notmatch '^\s*\[(\d+)\]') { throw "find: no squadmate to shoot: $pawns" }
            $script:shooter = [int]$Matches[1]
        }
        $where = Ask 'mevr state'
        if ($where -notmatch 'at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)') { throw "find: Shepard's place unread: $where" }
        $me = @{ x = [double]$Matches[1]; y = [double]$Matches[2]; z = [double]$Matches[3] }
        $r = [Math]::PI * $best.yaw / 180.0
        $placed = $false
        foreach ($m in @(7.0, 5.0, 4.0, 3.5)) {
            $sx = $me.x + [Math]::Cos($r) * $m * 100.0
            $sy = $me.y + [Math]::Sin($r) * $m * 100.0
            $put = Ask ('mevr cover put {0} {1} {2} {3}' -f $script:shooter, $sx.ToString('0', $inv), $sy.ToString('0', $inv), $me.z.ToString('0', $inv))
            Note "find: the shooter $m m out: $put"
            if ($put -match ' now at ') { $placed = $true; break }
        }
        if (-not $placed) { throw 'find: the shooter could not be put beyond the wall' }
        Start-Sleep -Seconds 1
        Note ('find: ' + (Ask 'mevr cover pawns'))
        $null = Posture $false
    }
    if ($script:shooter -lt 0) { throw 'no shooter: run the find phase or pass -Shooter' }
    if ($Phases -contains 'button') {
        $null = Posture $false
        Eyes 'button, standing'
        Hits 'button, standing'
        if (Posture $true) {
            Start-Sleep -Seconds 1
            Eyes 'button, crouched by L3'
            Hits 'button, crouched by L3'
        } else { Note 'button: L3 never crouched Shepard' }
    }
    if ($Phases -contains 'off') {
        if (Posture $true) {
            Note ('off: ' + (Ask 'mevr cover crouch off'))
            Eyes 'off, crouched by L3'
            Hits 'off, crouched by L3'
            Note ('off: ' + (Ask 'mevr cover crouch on'))
        }
        $null = Posture $false
    }
    if ($Phases -contains 'physical') {
        $null = Posture $false
        # The owner's natural crouch, 1.52 m: a 0.35 m drop from his standing 1.869 m (the replay's floor sample follows it).
        $drop = if ($OwnerCalibration) { ($cal.StandEyeM - 1.52).ToString('0.00', $inv) } else { '0.35' }
        Play 'stand:1' @('--crouch-drop', $drop) 'crouch'   # the crouch held until the next replay
        Start-Sleep -Seconds 4
        Eyes "physical, the head $drop m down"
        Hits "physical, the head $drop m down"
        Play 'stand:1'
        Start-Sleep -Seconds 3
        Eyes 'physical, stood up'
    }
} finally {
    $null = Ask 'mevr cover crouch on'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Stop | Out-Null
}
Start-Sleep -Seconds 2
$mine = @(ReadLog | Select-Object -Skip $from)
Set-Content -LiteralPath (Join-Path $out 'log.txt') -Value $mine -Encoding utf8
$summary = @($mine | Where-Object { $_ -match 'vr cover:|posture: (physical|crouch request|stand request|L3|Shepard.s crouch|standing eye height)' })
Set-Content -LiteralPath (Join-Path $out 'summary.txt') -Value $summary -Encoding utf8
# The hits' own answers (`mevr cover hit`: health and shields before and after, the verdict, the eyes that judged).
$answers = @(Get-Content -LiteralPath $journal | Where-Object { $_ -match ' hit \d: vr cover hit: ' })
$crouchedCovered = @($answers | Where-Object { $_ -match "covered 1, .*judged from Shepard's crouched eyes" }).Count
$realOpen = @($answers | Where-Object { $_ -match 'covered 0, .*judged from the real eyes' }).Count
Note ("{0} hits; covered and judged from Shepard's crouched eyes {1}, open and judged from the real eyes {2}; {3} log lines: {4}" -f
    $answers.Count, $crouchedCovered, $realOpen, $mine.Count, (Join-Path $out 'summary.txt'))
