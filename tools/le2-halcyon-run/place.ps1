# SPDX-License-Identifier: MIT
# The squad's placement by an arc on ME2 and ME3 (research/54 "Placement by an arc"; ME1's controls): with a stretch open
# (prep.ps1, launch.ps1, load.ps1) and the squad present, replayed controllers (tools/make-power-recording.py) hold the right B:
#   1. the squad wheel at the right hand (two squadmates: the first left, the squad up, the second right): the hand moved up
#      picks the squad; the right hand then aims the arc (a snapshot shows the arc and its ring); B let go sends both to the ring
#   2. the hand moved left: the first squadmate alone, a little further to the left
#   3. B double-tapped: the squad back to Shepard
#   4. the flat order by gesture: a left grip squeeze pointing at the floor ahead, let go: the squad moves there
# After each, `mevr place status` says where each squadmate stands and how far from the ring (`fromPointM`; ME1 measured
# 0.6 m) and from Shepard. Results go to the stretch's session folder, place-<time>\ (recordings, console.txt, snapshots.txt,
# verdicts.txt, lines.txt).
#
#   tools\le2-halcyon-run\place.ps1 -Holder <you> [-Game LE3] [-SettleS 8] [-Only wheel,one,double,gesture]
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [double]$SettleS = 8,
    [double]$RingM = 1.5,       # one squadmate sent: this near the ring (ME1 measured 0.6 m)
    [double]$SquadRingM = 2.5,  # the squad sent to one point: two people cannot stand on it (ME2: 0.27 and 0.38 m, or 1.9 m beside the first)
    [string[]]$Only = @('wheel', 'one', 'double', 'gesture')
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$name = if ($Game -eq 'LE3') { 'ME3' } else { 'ME2' }
$current = Join-Path $env:LOCALAPPDATA ("MEVR\sessions\{0}-current.json" -f $Game.ToLowerInvariant())
if (-not (Test-Path -LiteralPath $current)) { throw "no $name stretch is open: prep.ps1, launch.ps1, load.ps1 first" }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open $name stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process ("MassEffect" + $Game.Substring(2)) -ErrorAction SilentlyContinue)) { throw "$name is not running" }
$out = Join-Path $open.session ('place-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$log = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root $Game) ("Logs\{0}MEVR.log" -f $Game)
$shots = Join-Path $env:LOCALAPPDATA 'MEVR\snapshots'

function Ask([string]$command) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds 10 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
function Status {
    $line = (Ask 'mevr place status') -split "`n" | Where-Object { $_ -match '^\s*\{' } | Select-Object -First 1
    if (-not $line) { throw 'no squad place status (is the mod up?)' }
    return $line | ConvertFrom-Json
}
function Tool([string]$tool, [string[]]$arguments) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W "tools\$tool") @arguments
}
function Shot([string]$tag) {
    $started = Get-Date
    Tool 'snapshot.ps1' @('-Count', '1', '-Tag', $tag) | Out-Null
    $json = Get-ChildItem -LiteralPath $shots -Filter '*.json' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-1) } | Sort-Object LastWriteTime | Select-Object -Last 1
    Add-Content -LiteralPath (Join-Path $out 'snapshots.txt') -Value $(if ($json) { "$tag $($json.FullName)" } else { "$tag (no snapshot)" })
    return $json
}
function Play([string]$tag, [string[]]$events, [double]$seconds, [double]$snapAt = 0) {
    $recording = Join-Path $out ($tag + '.mevrrec')
    & python (Join-Path $W 'tools\make-power-recording.py') $recording --seconds "$seconds" @events | Out-Null
    if ($LASTEXITCODE) { throw "the $tag recording failed" }
    $started = Get-Date
    Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
    if ($snapAt -gt 0) {
        Start-Sleep -Milliseconds ([int](1000 * $snapAt))
        $null = Shot $tag
    }
    $left = $seconds + 0.5 - ((Get-Date) - $started).TotalSeconds
    if ($left -gt 0) { Start-Sleep -Milliseconds ([int](1000 * $left)) }
    Tool 'replay.ps1' @('-Stop') | Out-Null
}
function ReadLog {
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return ($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Close() }
}
$verdicts = New-Object System.Collections.Generic.List[string]
function Verdict([string]$what, [bool]$ok, [string]$detail) {
    $line = '{0} {1}: {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' }), $what, $detail)
    $verdicts.Add($line)
    Write-Output $line
}
function Placed([object]$s) {
    return (@($s.squad) | ForEach-Object { '{0} {1:N2} m from the ring, {2:N2} m from Shepard{3} [AI: {4}]' -f $_.name, $_.fromPointM, $_.fromShepardM, $(if ($_.wasSent) { '' } else { ' (not sent)' }), $_.ai }) -join '; '
}

$from = (ReadLog).Count
Ask 'mevr state' | Write-Output
$s = Status
$members = @($s.squad).Count
"squad ($members): $(Placed $s)"
if ($members -eq 0) { throw 'no squadmates: placement needs a squad' }
# The right hand as the recording holds it: rest (0.15, -0.34, -0.31); the squad wheel's tiles 7 cm away (left, up, right).
$up = 'rhand:1.3:1.6:0.15,-0.21,-0.31'  # 13 cm up: the wheel faces the eyes from below, so its plane sees about 0.7 of it
$leftTile = 'rhand:1.3:1.6:0.04,-0.34,-0.31'
$aim = @('rhand:1.9:2.3:0.15,-0.30,-0.35', 'rpitch:1.9:2.3:-4')  # level-ish: the arc lands some metres ahead

if ($Only -contains 'wheel') {
    $before = $s
    $events = @('rb:1.0:5.0') + $(if ($members -ge 2) { @($up) } else { @() }) + $aim + @('rrest:5.3:5.8')
    Play 'squad' $events 6.0 3.6
    Start-Sleep -Seconds $SettleS
    $after = Status
    $sent = @($after.squad | Where-Object { $_.wasSent })
    $near = @($sent | Where-Object { $_.fromPointM -ge 0 -and $_.fromPointM -le $SquadRingM })
    Verdict 'B held, the squad picked up, the arc let go: both go to the ring' ($after.given -gt $before.given -and $sent.Count -eq $members -and $near.Count -eq $members) (
        "given $($before.given) -> $($after.given), last '$($after.last)'; $(Placed $after)")
}
if ($Only -contains 'one' -and $members -ge 2) {
    $before = Status
    $events = @('rb:1.0:5.0', $leftTile, 'rhand:1.9:2.3:0.10,-0.30,-0.35', 'rpitch:1.9:2.3:-4') + @('rrest:5.3:5.8')
    Play 'one' $events 6.0 3.6
    Start-Sleep -Seconds $SettleS
    $after = Status
    $first = @($after.squad)[0]
    Verdict 'the hand moved left: the first squadmate alone' ($after.given -gt $before.given -and $first.wasSent -and $first.fromPointM -ge 0 -and $first.fromPointM -le $RingM -and -not @($after.squad)[1].wasSent) (
        "given $($before.given) -> $($after.given), last '$($after.last)'; $(Placed $after)")
}
if ($Only -contains 'double') {
    $before = Status
    Play 'double' @('rb:1.0:1.12', 'rb:1.3:1.42') 2.5
    Start-Sleep -Seconds $SettleS
    $after = Status
    Verdict 'B double-tapped: the squad back to Shepard' ($after.regroups -gt $before.regroups) (
        "regroups $($before.regroups) -> $($after.regroups), last '$($after.last)'; $(Placed $after)")
}
if ($Only -contains 'gesture') {
    # ME1's flat order: a left grip squeeze nobody else claims, the left hand's ray on the floor past followWithinM, let go.
    $context = Ask 'mevr context'
    $before = Status
    Play 'gesture' @('lgrip:1.0:3.0', 'lhand:0.5:0.9:-0.25,-0.20,-0.45', 'lpitch:0.5:0.9:-30') 4.0 2.0
    Start-Sleep -Seconds $SettleS
    $after = Status
    $orders = Ask 'mevr say status'
    Verdict 'the left grip pointed at the floor and let go: a move order (squad orders)' $true (
        "see lines.txt's 'squad orders:' lines; $(Placed $after)")
}

$lines = ReadLog
$new = if ($lines.Count -gt $from) { $lines[$from..($lines.Count - 1)] } else { @() }
$kept = @($new | Where-Object { $_ -match '\] \[\w+\] (squad place:|squad orders:|squad ack:|squad context:)' })
$kept | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$verdicts | Set-Content -LiteralPath (Join-Path $out 'verdicts.txt') -Encoding utf8
"results: $out"
