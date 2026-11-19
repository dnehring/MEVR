# SPDX-License-Identifier: MIT
# Lane ANC's check on ME2 (research/29 "The crouch that went third person" and "A run never lowers the body", 2026-11-18): the
# owner's "almost magnetic" pull at a barrier was arm IK falling to its third-person anchor whenever ME2 crouched Shepard, and
# the torso's "crouching with you" lowered the body 14-17 cm mid-run. In an open stretch (prep.ps1, launch.ps1, load.ps1: Save 4,
# Freedom's Progress, on foot), with replayed controllers (tools/make-walk-recording.py plans, `--hands view` so the drawn hands
# are in every snapshot) and their inputs; a camera trace per phase (`mevr camera trace`: the pawn, the camera and 14 bones a
# tick, python tools\skeleton-audit.py reads it); snapshots of the body while crouched; the log's lines since the start copied
# to the session folder and summarized. At the owner's calibration by default (-OwnerCalibration: tools/owner-calibration.ps1,
# his Quest 3's thresholds and the replays' floor samples at his standing eye height; ME2 relaunched once if the running game
# doesn't use it yet).
#
#   tools\le2-halcyon-run\crouch-anchor.ps1 -Holder <you> [-Phases button,physical,run] [-Tag what] [-OwnerCalibration:$false]
#
# Phases (each the pass/fail lines to read in summary.txt):
#   button    L3 (the mod's crouch toggle on the sequels) crouches Shepard by the game while the replayed head stands: his
#             animated head 45-60 units under the view. Pass: no `arm IK: body anchor: third person` between the two L3s (an
#             anchor line, if any, reads `... (N across: the view's height is your own head's ...)`), the snapshot's
#             mod.armIk shows the hands on the controllers, `torso:` lines say first person.
#   physical  the owner's case: their calibrated thresholds (-OwnerCalibration; else [Hands] crouchBelow 0.92, standAbove 0.942),
#             crouchMinDropM 0 and crouchLeanRatio 0 so a 0.17 m drop crouches, a 0.17 m and a 0.40 m crouch, held, then
#             standing up. Pass: `posture: physical crouch` for each (the replays drive the floor sample since 2026-11-19,
#             research/le2-vrik.md "The replay STAGE-eye gap"), then as `button`, through the stand (the old rule fell to third
#             person while the head rose before the game stood him); the snapshots' mod.posture and mod.torso (summary.txt's
#             `snapshot:` lines) and `floor lock: ... final eye`: the body down with the head, the eye at the replayed head's.
#   run       out-and-back runs exploring (each back a 180-degree explore turn, the owner's backing up), the head standing.
#             Pass: no `torso: crouching with you: the body down N cm` with N above 3 (the margin and the idle's own); the
#             heartbeat's `arm IK torso` lines may say `held at the player's own head drop` (the animation stood higher).
# LE2MEVR.ini's [Hands] keys this script writes are put back to the defaults at the end (prep backed the file up too;
# restore.ps1 puts it back).
param(
    [Parameter(Mandatory)][string]$Holder,
    [string[]]$Phases = @('button', 'physical', 'run'),
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'anc',
    [switch]$OwnerCalibration = $true   # the owner's calibration (tools/owner-calibration.ps1): relaunches ME2 once if needed
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME2 stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$out = Join-Path $open.session ('crouch-anchor-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$journal = Join-Path $out 'steps.txt'
$bin = Get-MELEBinDir (Find-MELEInstall).Root 'LE2'
$log = Join-Path $bin 'Logs\LE2MEVR.log'
$ini = Join-Path $bin 'ASI\LE2MEVR.ini'

Add-Type -Namespace Mevr -Name AncProfile -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern uint GetPrivateProfileString(string section, string key, string fallback, System.Text.StringBuilder value, uint size, string file);
'@
# The [Hands] keys the physical phase writes, as the file had them (null: absent), put back at the end.
$handsKeys = @('crouchBelow', 'standAbove', 'crouchMinDropM', 'crouchLeanRatio')
$handsBefore = @{}
foreach ($key in $handsKeys) {
    $buffer = New-Object System.Text.StringBuilder 256
    [void][Mevr.AncProfile]::GetPrivateProfileString('Hands', $key, '<absent>', $buffer, 256, $ini)
    $handsBefore[$key] = if ($buffer.ToString() -eq '<absent>') { $null } else { $buffer.ToString() }
}

function Note([string]$text) {
    $line = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss.fff'), $text
    Add-Content -LiteralPath $journal -Value $line -Encoding utf8
    Write-Output $line
}
function Ask([string]$command, [int]$timeout = 10) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds $timeout 2>$null
    return (($text | Where-Object { $_ }) -join "`n")
}
function ReadLog {
    # Shared read: the game keeps the log open for writing.
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -ne '' }) } finally { $reader.Close() }
}
# Replays a plan (make-walk-recording.py's segments) with its inputs; snapshots at the given seconds into it (the body while it
# holds a posture), then waits the plan out.
function Play([string]$plan, [string]$file, [string[]]$extra = @(), [double[]]$snapshotsAt = @()) {
    $path = Join-Path $out $file
    $made = & python (Join-Path $W 'tools\make-walk-recording.py') $path --plan $plan --pitch -20 --hands view @extra @script:standEye
    Add-Content -LiteralPath (Join-Path $out 'recordings.txt') -Value $made -Encoding utf8
    $seconds = 0.0
    foreach ($part in $plan -split ',') { $seconds += [double]($part.Substring($part.LastIndexOf(':') + 1)) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Path $path -Inputs | Out-Null
    $started = Get-Date
    foreach ($at in $snapshotsAt) {
        $wait = $at - ((Get-Date) - $started).TotalSeconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
        $shotTag = "$Tag-" + [IO.Path]::GetFileNameWithoutExtension($file)
        $shot = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\snapshot.ps1') -Tag $shotTag 2>$null
        $shotText = ($shot | Where-Object { $_ }) -join ' '
        Note ("snapshot at {0:0.0} s of {1}: {2}" -f $at, $file, $shotText)
        Note (Describe-Snapshot $shotTag ([regex]::Match($shotText, '\S+\.json').Value))
    }
    $left = $seconds + 0.6 - ((Get-Date) - $started).TotalSeconds
    if ($left -gt 0) { Start-Sleep -Milliseconds ([int]($left * 1000)) }
    HoldStanding
}
# Between plays: a standing replay looped (the next play replaces it), so the eyes don't drop back to the null driver's 1.75 m
# between phases at the owner's height (the posture's standing reference and the floor lock stay at his height).
function HoldStanding {
    if (-not $script:standPath) {
        $script:standPath = Join-Path $out 'hold-standing.mevrrec'
        & python (Join-Path $W 'tools\make-walk-recording.py') $script:standPath --plan 'stand:10' --pitch -20 --hands view @script:standEye | Out-Null
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Path $script:standPath -Loop -Inputs | Out-Null
}
# What a snapshot's JSON (the path snapshot.ps1 printed) says about the body and the eyes, copied into the session folder with
# its eye images: the posture's eyes (the replayed head's height) and Shepard's crouch, the torso's crouch (the body down with
# the head: research/29 "The eyes inside the body"). The floor lock's eye is in the log (`floor lock: ... final eye`).
function Describe-Snapshot([string]$shotTag, [string]$jsonPath) {
    if (-not $jsonPath -or -not (Test-Path -LiteralPath $jsonPath)) { return "snapshot: ${shotTag}: no JSON" }
    $stem = [IO.Path]::GetFileNameWithoutExtension($jsonPath)
    Get-ChildItem (Split-Path $jsonPath) -Filter "$stem*" | Where-Object { $_.Name -match '\.json$|-(left|right)\.png$' } |
        Copy-Item -Destination $out
    $j = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    $t = $j.mod.torso
    $r = if ($t) { $t.result } else { $null }
    $p = $j.mod.posture
    return ("snapshot: {0} ({1}): posture eyes {2} m of standing {3} m, Shepard crouched {4} (the game's), physical {5}; torso " +
        "crouchBody {6}, body lifted {7} m, crouch gap {8} m, held by the legs {9}, Shepard's head {10} m from its goal") -f $shotTag,
        $stem, $p.eyeHeight, $p.standingEyeHeight, $p.gameCrouched, $p.physical, $t.crouchBody, $r.bodyLiftM, $r.crouchGapM,
        $r.liftLimited, $r.headFromPivotM
}
function Hands([string]$key, [string]$value) {
    [void][Mevr.AncProfile]::WritePrivateProfileString('Hands', $key, $value, $ini)
    Note "[Hands] $key = $value"
    Start-Sleep -Milliseconds 2500  # the host checks the game's own file every 2 s, the mod re-reads once a second
}

# The owner's calibration: the key in use (a relaunch if not yet), the replays' floor samples at his standing eye height.
$script:standEye = @()
$script:standPath = $null
. (Join-Path $W 'tools\owner-calibration.ps1')
if ($OwnerCalibration) {
    $cal = Use-OwnerCalibrationLe2 -Holder $Holder
    $script:standEye = @('--stand-eye', $cal.StandEyeM.ToString([Globalization.CultureInfo]::InvariantCulture), '--ease', '0.4')
}
# Tracked hands from here on; the first time in a stretch they open our main menu's controller guide, which pauses the game.
HoldStanding
Start-Sleep -Seconds 2
if (Close-FirstRunMenuLe2 $out) { Note 'the first-run controller guide was open (the game paused): closed with B'; HoldStanding }
Start-Sleep -Seconds 6  # the posture's standing reference rises toward a higher eye with a 2 s time constant
$from = (ReadLog).Count
Note ("crouch-anchor {0}: phases {1}; {2}" -f $Tag, ($Phases -join ','), (Ask 'mevr state'))
if ($OwnerCalibration) {
    Note ("owner calibration [Calibration.{0}] ({1}): standing {2} m, crouched {3} m{4}; calibration.ini sha256 {5}" -f $cal.Key,
        $cal.Time, $cal.StandEyeM, $cal.CrouchEyeM, $(if ($cal.Relaunched) { '; ME2 relaunched for it' } else { '' }), $cal.Sha256)
}
try {
    if ($Phases -contains 'button') {
        Note (Ask "mevr camera trace 14 $Tag-button")
        # Standing, L3 (the game's crouch), held 5 s with the head standing, L3 again (the game stands him), standing.
        Play 'stand:2,l3:0.06,stand:5,l3:0.06,stand:3' 'button.mevrrec' @() @(3.5)
        Start-Sleep -Seconds 1
    }
    if ($Phases -contains 'physical') {
        if (-not $OwnerCalibration) {
            Hands 'crouchBelow' '0.92'
            Hands 'standAbove' '0.942'
        }
        Hands 'crouchMinDropM' '0'
        Hands 'crouchLeanRatio' '0'
        foreach ($drop in @('0.17', '0.40')) {
            Note (Ask "mevr camera trace 12 $Tag-physical-$drop")
            # The crouch held 6 s: the replay and the snapshot request each wait for the game's once-a-second poll.
            Play 'stand:2,crouch:6,stand:3' ("physical-{0}.mevrrec" -f $drop) @('--crouch-drop', $drop) @(4.5)
            Start-Sleep -Seconds 1
        }
    }
    if ($Phases -contains 'run') {
        Note (Ask "mevr camera trace 14 $Tag-run")
        Play 'stand:1.5,forward:1.5,stand:1.5,back:1.5,stand:1.5,forward:1.5,stand:1.5,back:1.5,stand:2' 'run.mevrrec' @() @(3.2, 6.2)
    }
} finally {
    if ($Phases -contains 'physical') {
        # The keys as they were (an absent one removed again).
        foreach ($key in $handsKeys) {
            $value = if ($null -eq $handsBefore[$key]) { [NullString]::Value } else { $handsBefore[$key] }
            [void][Mevr.AncProfile]::WritePrivateProfileString('Hands', $key, $value, $ini)
            Note ("[Hands] {0} back to {1}" -f $key, $(if ($null -eq $handsBefore[$key]) { '(absent)' } else { $handsBefore[$key] }))
        }
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Stop | Out-Null
}
Start-Sleep -Seconds 2
$lines = @(ReadLog)
$mine = @($lines | Select-Object -Skip $from)
Set-Content -LiteralPath (Join-Path $out 'log.txt') -Value $mine -Encoding utf8
$keys = 'arm IK: body anchor|crouching with you|back to the game.s height|held at|posture: (physical|crouch request|stand request|L3|Shepard.s crouch|standing eye height|a lean)|arm IK torso|torso: (first|third) person|camera trace|snapshot|floor lock: raw camera|replay drives the floor|calibration: (position|\[Debug\])'
$summary = @($mine | Where-Object { $_ -match $keys })
Set-Content -LiteralPath (Join-Path $out 'summary.txt') -Value $summary -Encoding utf8
$third = @($mine | Where-Object { $_ -match 'arm IK: body anchor: third person' }).Count
$drops = @($mine | Where-Object { $_ -match 'crouching with you: the body down (\d+) cm' -and [int]$Matches[1] -gt 3 }).Count
$format = "{0} log lines, {1} in the summary; third-person anchors {2}, body drops past 3 cm {3} (the physical phase's are the " +
    "player's own crouch): {4}"
Note ($format -f $mine.Count, $summary.Count, $third, $drops, (Join-Path $out 'summary.txt'))
