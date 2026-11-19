# SPDX-License-Identifier: MIT
# Lane M2W's checks on ME2 (research/38 "ME2's late start", research/29 "Backing up on the sequels", research/23 "A lean is not
# a crouch", "The crouch follows you", "L3 crouches on the sequels"), in an open stretch (prep.ps1, launch.ps1, load.ps1:
# Save 4, Freedom's Progress, on foot with the weapon away). Replayed controllers (tools/make-walk-recording.py plans) with
# their inputs; a camera trace per phase; the log's lines since the start copied to the session folder, summarized.
#
#   tools\le2-halcyon-run\posture-walk.ps1 -Holder <you> [-Phases start,tree,crouch,l3] [-Tag what]
#
# Phases:
#   start   out-and-back pushes (each a 180-degree explore turn), the game's start and stop nodes' root motion
#           (`mevr loco start game`), then ours (`on`): `movement: response:` against each other, the nodes named
#   tree    `mevr animtree` for the locomotion nodes (the explore set's backward run, if any; the combat branch's directional
#           node): what research/29's backing-up options need
#   crouch  the owner's calibrated thresholds put in [Hands] (crouchBelow 0.92, standAbove 0.942) on the fake headset's
#           1.75 m: a lean (0.17 m down, 0.35 m ahead) and a squat, with crouchLeanRatio 0 / 1 and crouchMinDropM 0 / 0.25;
#           then a crouch and a crouched walk exploring (the game stands him to walk; crouchFollow puts him back once still).
#           Only conclusive if the replayed head reaches the posture's floor sample: research/23 section 7.5 found the live
#           STAGE height kept under a replay on 2026-11-16; no `posture: physical crouch` line here means it still is
#   l3      the left stick click tapped and held: the crouch toggles, no L3 reaches the game (`mevr calltrace` on the map's
#           functions)
# LE2MEVR.ini's [Hands] keys this script writes are the stretch's own (prep backed the file up; restore.ps1 puts it back).
param(
    [Parameter(Mandatory)][string]$Holder,
    [string[]]$Phases = @('start', 'tree', 'crouch', 'l3'),
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'm2w'
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
    $made = & python (Join-Path $W 'tools\make-walk-recording.py') $path --plan $plan --pitch -20 @extra
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

$from = (ReadLog).Count
Note ("posture-walk {0}: phases {1}; {2}" -f $Tag, ($Phases -join ','), (Ask 'mevr state'))
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
    Hands 'crouchBelow' '0.92'
    Hands 'standAbove' '0.942'
    foreach ($case in @(@('0', '0'), @('1', '0'), @('0', '0.25'))) {
        Hands 'crouchLeanRatio' $case[0]
        Hands 'crouchMinDropM' $case[1]
        Play 'stand:2,lean:2.5,stand:2.5,crouch:2.5,stand:2.5' ("lean-ratio{0}-drop{1}.mevrrec" -f $case[0], $case[1]) @('--crouch-drop', '0.45')
    }
    Hands 'crouchLeanRatio' '0'
    Hands 'crouchMinDropM' '0'
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
$keys = 'movement: response:|animation nodes that asked|asked for its animation''s root motion|posture:|mevr loco|animtree|call trace:.*(ShowAreaMap|ObjectiveRadar)|halcyon bridge: first left stick'
$summary = @($mine | Where-Object { $_ -match $keys })
Set-Content -LiteralPath (Join-Path $out 'summary.txt') -Value $summary -Encoding utf8
Note ("{0} log lines, {1} in the summary: {2}" -f $mine.Count, $summary.Count, (Join-Path $out 'summary.txt'))
