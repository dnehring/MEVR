# SPDX-License-Identifier: MIT
# Steps inside a sequel's galaxy map over the CIC's projector (docs/galaxy-projector-handoff.md), after galaxy.ps1 -NoExit
# opened it in an open stretch (prep.ps1, launch.ps1, load.ps1): the map's own controls replayed (tools/make-walk-recording.py
# plans: the stick, A, B, X, LT, RT), the ship steered to a target with the stick (closed loop over `mevr galaxy capture ship`),
# snapshots, console lines, waits for a game mode. Each step is logged with the game mode and the projector flow's state; the
# results go to the stretch's session folder, galaxy-steps-<tag>-<time>\.
#
#   tools\le2-halcyon-run\galaxy-steps.ps1 -Holder <you> -Steps 'state; play:b:0.25; wait:3; snap:system; steer:relay:600; ...'
#
# Steps (separated by ';'):
#   state                     `mevr state`, `mevr galaxy state`, `mevr galaxy capture state` into the log
#   snap:<tag>                a snapshot (tools/snapshot.ps1)
#   wait:<seconds>            sleep
#   play:<plan>               replay a make-walk-recording plan with its inputs (e.g. `b:0.25`, `stick@90:1.2`, `lt:0.3`), wait it out
#   playlive:<plan>           the same with the runtime's head (--live-head): the Back double tap's recenter
#   ask:<console line>        tools/console.ps1 (e.g. `ask:mevr galaxy capture flares`)
#   mode:<regex>:<seconds>    wait until the game mode matches (e.g. `mode:1[12]:20`); a miss is logged, not fatal
#   steer:<target>:<radius>   push the stick toward a target until the ship (the map's cross-hair object) is within radius map
#                             units: `relay`, `selected`, `centre` or a selectable actor's name or tag (part of it); at most 25
#                             pushes of 0.1-3 s (70% of the way at the last push's speed)
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE3',
    [Parameter(Mandatory)][string]$Steps,
    [double]$Yaw = 10,
    [double]$Pitch = -20,
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'steps'
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
$out = Join-Path $open.session ('galaxy-steps-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$journal = Join-Path $out 'steps.txt'
$bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
$log = Join-Path $bin ("Logs\{0}MEVR.log" -f $Game)

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
function Mode {
    $state = ''
    try { $state = Ask 'mevr state' 5 } catch { }
    if ($state -match 'game mode (\d+)') { return [int]$Matches[1] }
    return -1
}
function Play([string]$plan, [string]$file, [switch]$LiveHead) {
    $path = Join-Path $out $file
    $extra = @(); if ($LiveHead) { $extra = @('--live-head') }
    $made = & python (Join-Path $W 'tools\make-walk-recording.py') $path --plan $plan --yaw $Yaw --pitch $Pitch @extra
    Add-Content -LiteralPath (Join-Path $out 'recordings.txt') -Value $made -Encoding utf8
    $seconds = 0.0
    foreach ($part in $plan -split ',') { $seconds += [double]($part.Substring($part.LastIndexOf(':') + 1)) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Path $path -Inputs | Out-Null
    Start-Sleep -Milliseconds ([int](($seconds + 0.6) * 1000))
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Stop | Out-Null
}
function Ship {
    $text = Ask 'mevr galaxy capture ship'
    $i = $text.IndexOf('{')
    if ($i -lt 0) { return $null }
    try { return ($text.Substring($i) | ConvertFrom-Json) } catch { return $null }
}
function Where-Target($ship, [string]$target) {
    if ($target -eq 'relay') { return $ship.relay }
    if ($target -eq 'selected') { return $ship.selected }
    if ($target -eq 'centre') { return $ship.centre }
    foreach ($a in $ship.selectable) {
        if ("$($a.name) $($a.tag)" -like "*$target*") { return $a }
    }
    return $null
}

$n = 0
$from = (ReadLog).Count
foreach ($raw in ($Steps -split ';')) {
    $step = $raw.Trim()
    if (-not $step) { continue }
    $n++
    $kind, $arg = $step -split ':', 2
    Note ("step {0}: {1} (game mode {2})" -f $n, $step, (Mode))
    switch ($kind) {
        'state' {
            Note ((Ask 'mevr state') + "`n" + (Ask 'mevr galaxy state') + "`n" + (Ask 'mevr galaxy capture state'))
        }
        'snap' {
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\snapshot.ps1') -Tag ("galaxy-" + $Tag + "-" + $arg) |
                Add-Content -LiteralPath (Join-Path $out 'snapshots.txt') -Encoding utf8
        }
        'wait' { Start-Sleep -Milliseconds ([int]([double]$arg * 1000)) }
        'play' { Play $arg ("step{0}.mevrrec" -f $n) }
        'playlive' { Play $arg ("step{0}.mevrrec" -f $n) -LiveHead }
        'ask' { Note (Ask $arg 15) }
        'mode' {
            $pattern, $seconds = $arg -split ':', 2
            $deadline = (Get-Date).AddSeconds([double]$seconds)
            do { $m = Mode; if ("$m" -match "^($pattern)$") { break }; Start-Sleep -Milliseconds 500 } while ((Get-Date) -lt $deadline)
            Note ("game mode {0} ({1})" -f $m, $(if ("$m" -match "^($pattern)$") { 'as wanted' } else { "NOT $pattern within $seconds s" }))
        }
        'steer' {
            $target, $radius = $arg -split ':', 2
            $radius = if ($radius) { [double]$radius } else { 300.0 }
            $offset = 0.0  # the map's measured turn between the commanded and the ship's actual heading
            $speed = 40.0  # map units per second of a push, measured after each (the galaxy view's cursor is fast)
            for ($k = 1; $k -le 25; $k++) {
                $ship = Ship
                if (-not $ship -or -not $ship.available) { Note 'steer: no map context'; break }
                $goal = Where-Target $ship $target
                if (-not $goal -or $null -eq $goal.x) { Note ("steer: no target '{0}' (selectable: {1})" -f $target, (($ship.selectable | ForEach-Object { $_.name }) -join ', ')); break }
                $sx = [double]$ship.crosshair.x; $sy = [double]$ship.crosshair.y  # the ship (the map's cross-hair object)
                $dx = [double]$goal.x - $sx; $dy = [double]$goal.y - $sy
                $dist = [Math]::Sqrt($dx * $dx + $dy * $dy)
                if ($dist -le $radius) { Note ("steer: at {0} ({1:N0} units, state {2})" -f $target, $dist, $ship.state); break }
                $yaw = [double]$ship.cameraYaw * 2 * [Math]::PI / 65536.0
                $fx = [Math]::Cos($yaw); $fy = [Math]::Sin($yaw); $rx = -$fy; $ry = $fx
                $along = ($dx * $fx + $dy * $fy) / $dist; $across = ($dx * $rx + $dy * $ry) / $dist
                $angle = [Math]::Atan2($across, $along) * 180 / [Math]::PI - $offset
                $push = [Math]::Min(3.0, [Math]::Max(0.1, 0.7 * $dist / $speed))
                Play ('stick@{0:F1}:{1:F2},stand:0.2' -f $angle, $push) ("steer{0}-{1}.mevrrec" -f $n, $k)
                $after = Ship
                if ($after -and $after.available) {
                    $mx = [double]$after.crosshair.x - $sx; $my = [double]$after.crosshair.y - $sy
                    $moved = [Math]::Sqrt($mx * $mx + $my * $my)
                    if ($moved -gt 5) {
                        $wanted = [Math]::Atan2($dy, $dx) * 180 / [Math]::PI
                        $went = [Math]::Atan2($my, $mx) * 180 / [Math]::PI
                        $turn = (($went - $wanted + 540) % 360) - 180
                        $offset = $offset + 0.7 * $turn
                        $speed = [Math]::Max(5.0, $moved / $push)
                    }
                    Note ("steer {0}: {1:N0} units to {2}; stick {3:N0} deg {4:N2} s; moved {5:N0} units; offset {6:N0} deg; state {7}" -f
                        $k, $dist, $target, $angle, $push, $moved, $offset, $after.state)
                }
            }
        }
        default { Note "unknown step '$step'" }
    }
}
$lines = @((ReadLog) | Select-Object -Skip $from | Where-Object { $_ -match 'galaxy|scene capture|retention|overlays: game UI|swapchains|presentation' })
$lines | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
"results: $out"
