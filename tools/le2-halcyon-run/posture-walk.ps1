# SPDX-License-Identifier: MIT
# Lane M2W's checks on ME2 (research/38 "ME2's late start", research/29 "Backing up on the sequels", research/23 "A lean is not
# a crouch", "The crouch follows you", "L3 crouches on the sequels"), in an open stretch (prep.ps1, launch.ps1, load.ps1:
# Save 4, Freedom's Progress, on foot with the weapon away). Replayed controllers (tools/make-walk-recording.py plans) with
# their inputs; a camera trace per phase; the log's lines since the start copied to the session folder, summarized.
#
#   tools\le2-halcyon-run\posture-walk.ps1 -Holder <you> [-Phases start,tree,crouch,l3] [-Tag what] [-OwnerCalibration:$false]
#
# Phases:
#   start   out-and-back pushes (each a 180-degree explore turn), the game's start and stop nodes' root motion
#           (`mevr loco start game`), then ours (`on`): `movement: response:` against each other, the nodes named
#   tree    `mevr animtree` for the locomotion nodes (the explore set's backward run, if any; the combat branch's directional
#           node): what research/29's backing-up options need
#   crouch  at the owner's calibration (-OwnerCalibration, on by default: tools/owner-calibration.ps1; his Quest 3's
#           thresholds, the replays' floor samples at his standing eye height): a lean (0.17 m down, 0.35 m ahead), the
#           owner's reach (0.18 m down, 0.33 m ahead, the right hand out to a person's chest) and a 0.45 m squat, with
#           crouchLeanRatio 0 / 1 and crouchMinDropM 0 / 0.25 (both on: the defaults since 2026-11-18); then a crouch and a
#           crouched walk exploring (the game stands him to walk; crouchFollow puts him back once still). Pass with the
#           defaults: `posture: physical crouch` for the squat, none for the lean and the reach (`posture: a lean, not a
#           crouch` or the drop short of crouchMinDropM), `floor lock: ... final eye` down with the replayed head.
#           -OwnerCalibration:$false: [Hands] crouchBelow 0.92, standAbove 0.942 on the fake headset's 1.75 m instead.
#           The replays drive the floor sample since 2026-11-19 (research/le2-vrik.md "The replay STAGE-eye gap"; before,
#           the posture saw the live STAGE height and no replayed crouch)
#   l3      the left stick click tapped and held: the crouch toggles, no L3 reaches the game (`mevr calltrace` on the map's
#           functions)
# LE2MEVR.ini's [Hands] keys this script writes are the stretch's own (prep backed the file up; restore.ps1 puts it back).
param(
    [Parameter(Mandatory)][string]$Holder,
    [string[]]$Phases = @('start', 'tree', 'crouch', 'l3'),
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'm2w',
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
$out = Join-Path $open.session ('posture-walk-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$journal = Join-Path $out 'steps.txt'
$bin = Get-MELEBinDir (Find-MELEInstall).Root 'LE2'
$log = Join-Path $bin 'Logs\LE2MEVR.log'
$ini = Join-Path $bin 'ASI\LE2MEVR.ini'

Add-Type -Namespace Mevr -Name Profile -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
'@

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
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -ne '' }) } finally { $reader.Close() }
}
function Play([string]$plan, [string]$file, [string[]]$extra = @()) {
    $path = Join-Path $out $file
    $made = & python (Join-Path $W 'tools\make-walk-recording.py') $path --plan $plan --pitch -20 @extra @script:standEye
    Add-Content -LiteralPath (Join-Path $out 'recordings.txt') -Value $made -Encoding utf8
    $seconds = 0.0
    foreach ($part in $plan -split ',') { $seconds += [double]($part.Substring($part.LastIndexOf(':') + 1)) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Path $path -Inputs | Out-Null
    Start-Sleep -Milliseconds ([int](($seconds + 0.6) * 1000))
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Stop | Out-Null
}
function Hands([string]$key, [string]$value) {
    [void][Mevr.Profile]::WritePrivateProfileString('Hands', $key, $value, $ini)
    Note "[Hands] $key = $value"
    Start-Sleep -Milliseconds 1500  # the mod re-reads its settings once a second
}

# The owner's calibration: the key in use (a relaunch if not yet), the replays' floor samples at his standing eye height.
$script:standEye = @()
if ($OwnerCalibration) {
    . (Join-Path $W 'tools\owner-calibration.ps1')
    $cal = Use-OwnerCalibrationLe2 -Holder $Holder
    $script:standEye = @('--stand-eye', $cal.StandEyeM.ToString([Globalization.CultureInfo]::InvariantCulture), '--ease', '0.4')
}
$from = (ReadLog).Count
Note ("posture-walk {0}: phases {1}; {2}" -f $Tag, ($Phases -join ','), (Ask 'mevr state'))
if ($OwnerCalibration) {
    Note ("owner calibration [Calibration.{0}] ({1}): standing {2} m, crouched {3} m{4}; calibration.ini sha256 {5}" -f $cal.Key,
        $cal.Time, $cal.StandEyeM, $cal.CrouchEyeM, $(if ($cal.Relaunched) { '; ME2 relaunched for it' } else { '' }), $cal.Sha256)
}
$outAndBack = 'stand:1.5,forward:1,stand:1.2,back:1,stand:1.2,forward:1,stand:1.2,back:1,stand:1.5'
if ($Phases -contains 'start') {
    foreach ($which in @('game', 'on')) {
        Note (Ask "mevr loco start $which")
        Note (Ask "mevr camera trace 12 $Tag-start-$which")
        Play $outAndBack "start-$which.mevrrec"
        Start-Sleep -Seconds 3
    }
}
if ($Phases -contains 'tree') {
    foreach ($word in @('Locomotion', 'Directional', 'Combat', 'Storm')) { Note (Ask "mevr animtree $word" 15) }
    Note (Ask "mevr camera trace 10 $Tag-backing")
    Play 'stand:1,back:3,stand:1.5,stick@135:2,stand:1.5' 'backing.mevrrec' @('--pitch', '-60')
}
if ($Phases -contains 'crouch') {
    if (-not $OwnerCalibration) {
        Hands 'crouchBelow' '0.92'
        Hands 'standAbove' '0.942'
    }
    foreach ($case in @(@('0', '0'), @('1', '0'), @('0', '0.25'), @('1.0', '0.25'))) {
        Hands 'crouchLeanRatio' $case[0]
        Hands 'crouchMinDropM' $case[1]
        Note ("case: crouchLeanRatio {0}, crouchMinDropM {1}: lean 2-4.5 s, reach 7-9.5 s, squat 12-14.5 s" -f $case[0], $case[1])
        Note (Ask "mevr camera trace 17 $Tag-ratio$($case[0])-drop$($case[1])")
        Play 'stand:2,lean:2.5,stand:2.5,reach:2.5,stand:2.5,crouch:2.5,stand:2.5' ("lean-reach-ratio{0}-drop{1}.mevrrec" -f $case[0], $case[1]) @('--crouch-drop', '0.45', '--hands', 'view')
    }
    # The defaults since the owner's yes (2026-11-18, question 20).
    Hands 'crouchLeanRatio' '1.0'
    Hands 'crouchMinDropM' '0.25'
    # The follow rule: crouched, a crouched walk exploring (the game stands him to walk), crouched still again.
    Play 'stand:2,crouch:2,crouch+forward:1.5,crouch:3,stand:2.5' 'crouch-walk.mevrrec' @('--crouch-drop', '0.45')
}
if ($Phases -contains 'l3') {
    Note (Ask 'mevr calltrace 9 showareamap objectiveradar crouch')
    Play 'stand:1,l3:0.06,stand:1.5,l3:0.06,stand:1.5,l3:0.5,stand:1.5,l3:0.06,stand:1.5' 'l3.mevrrec'
}
Start-Sleep -Seconds 2
$lines = @(ReadLog)
$mine = @($lines | Select-Object -Skip $from)
Set-Content -LiteralPath (Join-Path $out 'log.txt') -Value $mine -Encoding utf8
$keys = 'movement: response:|animation nodes that asked|asked for its animation''s root motion|posture:|mevr loco|animtree|call trace:.*(ShowAreaMap|ObjectiveRadar)|halcyon bridge: first left stick|floor lock: raw camera|replay drives the floor|calibration: (position|\[Debug\])|arm IK: body anchor|camera trace'
$summary = @($mine | Where-Object { $_ -match $keys })
Set-Content -LiteralPath (Join-Path $out 'summary.txt') -Value $summary -Encoding utf8
Note ("{0} log lines, {1} in the summary: {2}" -f $mine.Count, $summary.Count, (Join-Path $out 'summary.txt'))
