# SPDX-License-Identifier: MIT
# The hand-cast powers' check on ME2 and ME3 (research/55 "Mass Effect 2"; ME1's controls, nothing new): with a stretch open
# (prep.ps1, launch.ps1, load.ps1), replayed controllers (tools/make-power-recording.py) work the power wheel at the left hand
# the way a player does, and the mod's status and `power wheel:` lines say what happened:
#   1. the wheel's look: X held 4 s without a pick (a snapshot while it is open), let go: closed without a choice
#   2. every tile picked and equipped, by the left hand's travel (even tiles) or the left stick (odd), "No power" last
#   3. a tapped X keeps it open: the hand's travel picks, a pinch of the left trigger equips
#   4. the coach card: a snapshot a second after an equip (the card where the wheel was)
#   5. every self-cast power by its gesture: the open hand on the snapshot's gesture target (the chest: a 0.7 s move and a
#      dwell; the gun: a 0.13 s slap), with a gun drawn; a chest power again with the weapon away (`mevr omni holster`)
#   6. every power that aims: the lock and the push. The lock's scan (`mevr powerwheel lock`) names the awake pawns in range;
#      the recording is turned toward the nearest hostile it lists (or -PushYaw), the left arm raised and pushed. With no
#      hostile in range the push must be refused by name, not cast.
# Results go to the stretch's session folder, powers-<time>\ (recordings, console.txt, snapshots.txt, lines.txt).
#
#      -Drone (ME2's Save 4): the level's own drone factory makes a hostile assault drone, moved 10 m ahead (`mevr activate`
#      of Drone_Combat_A's SFXSeqAct_AIFactory, then `mevr setloc` of the new drone): a target for the lock and the push. The
#      squad shoots it down within seconds, so it is made right before each push.
#
#   tools\le2-halcyon-run\powers.ps1 -Holder <you> [-Game LE3] [-Drone] [-PushYaw <deg>] [-Only look,picks,tap,self,push]
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [double]$PushYaw = [double]::NaN,
    [switch]$Drone,
    [string[]]$Only = @('look', 'picks', 'tap', 'self', 'push')
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
$out = Join-Path $open.session ('powers-' + (Get-Date -Format 'HHmmss'))
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
    $line = (Ask 'mevr powerwheel status') -split "`n" | Where-Object { $_ -match '^\s*\{' } | Select-Object -First 1
    if (-not $line) { throw 'no power wheel status (is the mod up?)' }
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
    $line = if ($json) { "$tag $($json.FullName)" } else { "$tag (no snapshot)" }
    Add-Content -LiteralPath (Join-Path $out 'snapshots.txt') -Value $line
    return $json
}
# Writes a recording, replays it with its inputs, takes a snapshot `snapAt` s in (0: none) and the wheel's status after it
# ($script:midStatus), waits it out, stops.
$script:midStatus = $null
function Play([string]$tag, [string[]]$events, [double]$seconds, [double]$snapAt = 0, [double]$yaw = 0) {
    $recording = Join-Path $out ($tag + '.mevrrec')
    $arguments = @($recording, '--seconds', "$seconds", '--yaw', "$yaw") + $events
    & python (Join-Path $W 'tools\make-power-recording.py') @arguments | Out-Null
    if ($LASTEXITCODE) { throw "the $tag recording failed" }
    $started = Get-Date
    Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
    $shot = $null
    $script:midStatus = $null
    if ($snapAt -gt 0) {
        Start-Sleep -Milliseconds ([int](1000 * $snapAt))
        $script:midStatus = Status
        $shot = Shot $tag
    }
    $left = $seconds + 0.5 - ((Get-Date) - $started).TotalSeconds
    if ($left -gt 0) { Start-Sleep -Milliseconds ([int](1000 * $left)) }
    Tool 'replay.ps1' @('-Stop') | Out-Null
    Start-Sleep -Milliseconds 800
    return $shot
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
# Equips roster entry `index` of `count` by the hand (or the stick) and returns the status after.
function Equip([int]$index, [int]$count, [string]$how = 'hand') {
    $kind = if ($how -eq 'stick') { 'stick' } else { 'wheel' }
    $null = Play ("equip-{0}-{1}" -f $index, $how) @("${kind}:1.0:2.4:${index}:${count}") 4.0
    return Status
}

$from = (ReadLog).Count
Ask 'mevr state' | Write-Output
Ask 'mevr powerwheel roster' | Write-Output
$s = Status
# The look (always: the wheel's roster is read while it is open): X held 5 s without a pick, a snapshot 2.6 s in, let go.
$shot = Play 'look' @('x:1.0:8.0') 9.0 2.0
$after = Status
$roster = @($script:midStatus.roster)
$count = $roster.Count
"roster on the wheel ($count): $($roster -join ', ')"
Verdict 'the wheel opens on X and closes without a choice' ($script:midStatus.open -and $after.opened -gt $s.opened -and $after.cancels -gt $s.cancels) (
    "open while held: $($script:midStatus.open); opened $($s.opened) -> $($after.opened), closed unequipped $($s.cancels) -> $($after.cancels); look: $($shot.FullName)")
if ($count -eq 0) { throw 'the wheel has no powers (the roster is empty): nothing to check' }

if ($Only -contains 'picks') {
    for ($i = 0; $i -lt $count; ++$i) {
        $how = if ($i % 2 -eq 0) { 'hand' } else { 'stick' }
        $before = Status
        $after = Equip $i $count $how
        $want = $roster[$i]
        $took = $after.equips -gt $before.equips -and $after.last -eq "equipped $want"
        if ($want -eq 'No power') {
            Verdict "tile $i ($want) by the $how" ($took -and $after.mappedPower -in @('None', '')) "equips $($before.equips) -> $($after.equips), last '$($after.last)', mapped '$($after.mappedPower)'"
        } else {
            Verdict "tile $i ($want) by the $how" $took "equips $($before.equips) -> $($after.equips), equipped '$($after.equipped)' ($($after.gesture)), last '$($after.last)'"
        }
        if ($i -eq 0 -and $want -ne 'No power') {
            $null = Play 'coach' @("wheel:1.0:2.4:0:${count}") 8.0 2.6  # the card about a second after the equip (it stays 8 s, or until a cast)
        }
    }
}

if ($Only -contains 'tap') {
    $before = Status
    $tile = [Math]::Min(1, $count - 1)
    $null = Play 'tap' @('x:1.0:1.15', "lhand:1.5:1.9:$(-0.20 + 0.07 * [Math]::Cos([Math]::PI * 83.5 / 180)),$(-0.15 + 0.07 * [Math]::Sin([Math]::PI * 83.5 / 180)),-0.37",
        'ltrigger:2.6:2.9', 'lrest:3.2:3.6') 4.5
    $after = Status
    Verdict 'a tapped X keeps it open; the left trigger equips' ($after.equips -gt $before.equips) (
        "equips $($before.equips) -> $($after.equips), equipped '$($after.equipped)', last '$($after.last)' (tile $tile's direction; " +
        'with other than four entries the hand may pick its neighbour)')
}

if ($Only -contains 'self') {
    for ($i = 0; $i -lt $count; ++$i) {
        $power = $roster[$i]
        if ($power -eq 'No power') { continue }
        foreach ($away in @($false, $true)) {
            Ask ($(if ($away) { 'mevr omni holster' } else { 'mevr weapon assault' })) | Out-Null
            Start-Sleep -Seconds 2
            $gesture = (Equip $i $count 'hand').gesture
            if ($gesture -notmatch 'chest|gun|raised' -or ($away -and $gesture -notmatch 'chest')) { break }
            Start-Sleep -Seconds 1
            # The target shows only with tracked hands: a standing replay, its status and a snapshot (the target's look) meanwhile.
            $null = Play ("self-{0}-probe" -f $i) @() 10.0 3.0
            $target = if ($script:midStatus) { $script:midStatus.gestureTarget } else { $null }
            if (-not $target -or ($target[0] -eq 0 -and $target[1] -eq 0 -and $target[2] -eq 0)) {
                Verdict "$power's gesture$(if ($away) { ', weapon away' })" $false 'no gesture target in the snapshot (mod.powerWheel.gestureTarget)'
                continue
            }
            $point = '{0},{1},{2}' -f $target[0], $target[1], $target[2]
            $move = if ($gesture -match 'gun') { 'lhand:1.0:1.13:' + $point } else { 'lhand:1.0:1.7:' + $point }
            $before = Status
            $null = Play ("self-{0}{1}" -f $i, $(if ($away) { '-away' } else { '' })) @($move, 'lrest:3.5:4.0') 5.0 2.2
            $after = Status
            Verdict "$power by $gesture$(if ($away) { ', weapon away' })" ($after.lock.casts -gt $before.lock.casts) (
                "casts $($before.lock.casts) -> $($after.lock.casts), refused $($before.lock.refused) -> $($after.lock.refused); last cast '$($after.lock.lastCast)'")
        }
    }
    Ask 'mevr weapon pistol' | Out-Null
}

# A standing replay with the left arm raised; the lock's scan read while it runs (the scan only runs with a tracked hand).
function Scan([string]$tag, [double]$yaw = 0) {
    $recording = Join-Path $out ($tag + '.mevrrec')
    & python (Join-Path $W 'tools\make-power-recording.py') $recording --seconds 8 --yaw "$yaw" 'lhand:0.5:1.0:-0.20,-0.10,-0.45' | Out-Null
    Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
    Start-Sleep -Seconds 2
    $text = Ask 'mevr powerwheel lock'
    Start-Sleep -Milliseconds 200
    Tool 'replay.ps1' @('-Stop') | Out-Null
    Start-Sleep -Milliseconds 800
    return $text
}
# The nearest pawn the scan lists that isn't a squadmate: its name, distance and the --yaw that faces it (UE yaw is clockwise
# from above; the recording's --yaw turns to the left).
function Nearest([string]$scan) {
    $squad = @((Status2).squad | ForEach-Object { $_.name })
    $eyes = [regex]::Match($scan, 'the eyes at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\) looking along \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)')
    $best = $null
    foreach ($m in [regex]::Matches($scan, '\n\s+(\S+) at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\): ([\d.]+) m')) {
        $who = $m.Groups[1].Value
        if (@($squad | Where-Object { $_ -and $who -match [regex]::Escape($_) }).Count -gt 0) { continue }
        if ($best -eq $null -or [double]$m.Groups[5].Value -lt [double]$best.Groups[5].Value) { $best = $m }
    }
    if (-not $eyes.Success -or -not $best) { return $null }
    $look = [Math]::Atan2([double]$eyes.Groups[5].Value, [double]$eyes.Groups[4].Value)
    $to = [Math]::Atan2([double]$best.Groups[3].Value - [double]$eyes.Groups[2].Value, [double]$best.Groups[2].Value - [double]$eyes.Groups[1].Value)
    $d = ($to - $look) * 180 / [Math]::PI
    while ($d -gt 180) { $d -= 360 }
    while ($d -le -180) { $d += 360 }
    return [pscustomobject]@{ name = $best.Groups[1].Value; m = [double]$best.Groups[5].Value; yaw = -$d
        eyes = @([double]$eyes.Groups[1].Value, [double]$eyes.Groups[2].Value, [double]$eyes.Groups[3].Value)
        look = @([double]$eyes.Groups[4].Value, [double]$eyes.Groups[5].Value) }
}
function Status2 {
    $line = (Ask 'mevr place status') -split "`n" | Where-Object { $_ -match '^\s*\{' } | Select-Object -First 1
    if ($line) { return $line | ConvertFrom-Json } else { return [pscustomobject]@{ squad = @() } }
}

# The squad shoots a drone down within seconds: it is made at its spawn point, out of the squad's sight, and brought in front
# of Shepard only once the push's replay has started (MakeDrone returns the `mevr setloc` line that brings it).
function MakeDrone {
    # Save 4 has no hostile in reach: the level's own drone factories (Freedom's Progress, BioD_ProFre_200Vorcha's
    # Drone_Combat_A) make drones at their spawn points, 80 m away behind the door; one assault drone is moved 10 m ahead of
    # the eyes, 0.4 m above them. A factory runs out after a couple of uses: each is tried until a new drone stands far away.
    $scan = Scan 'drone-look'
    $eyes = [regex]::Match($scan, 'the eyes at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\) looking along \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)')
    if (-not $eyes.Success) { throw 'no view for the drone''s place (the lock scan saw no eyes)' }
    $ex, $ey, $ez = [double]$eyes.Groups[1].Value, [double]$eyes.Groups[2].Value, [double]$eyes.Groups[3].Value
    $fx, $fy = [double]$eyes.Groups[4].Value, [double]$eyes.Groups[5].Value
    $n = [Math]::Sqrt($fx * $fx + $fy * $fy)
    $factories = [regex]::Matches((Ask 'mevr instances SFXGame.SFXSeqAct_AIFactory Drone_Combat_A'), '\n\[(\d+)\] ') | ForEach-Object { $_.Groups[1].Value }
    if (@($factories).Count -eq 0) { throw 'no drone factory in this level' }
    foreach ($factory in $factories) {
        # `mevr instances` replaces the found list `activate` reads: list the factories again before each.
        $null = Ask 'mevr instances SFXGame.SFXSeqAct_AIFactory Drone_Combat_A'
        Write-Host (Ask "mevr activate $factory 0")
        Start-Sleep -Seconds 3
        # The new drone is the one far away (at its spawn point); moved alone (`mevr setloc`: the console's `set` would move
        # every drone of the class onto one spot).
        $drones = Ask 'mevr instances SFXGame.SFXPawn_AssaultDrone'
        $far = $null
        foreach ($m in [regex]::Matches($drones, '\n\[(\d+)\] \S+ \S+ at (-?\d+) (-?\d+) (-?\d+)')) {
            $d = [Math]::Sqrt([Math]::Pow([double]$m.Groups[2].Value - $ex, 2) + [Math]::Pow([double]$m.Groups[3].Value - $ey, 2))
            if ($d -gt 3000 -and ($far -eq $null -or $d -gt $far.d)) { $far = [pscustomobject]@{ n = $m.Groups[1].Value; d = $d } }
        }
        if ($far) {
            return ('mevr setloc {0} {1:F0} {2:F0} {3:F0}' -f $far.n, ($ex + 1000 * $fx / $n), ($ey + 1000 * $fy / $n), ($ez + 40))
        }
    }
    throw 'no drone factory made a drone (they ran out: load the save again)'
}

if ($Only -contains 'push') {
    for ($i = 0; $i -lt $count; ++$i) {
        $power = $roster[$i]
        if ($power -eq 'No power') { continue }
        if ((Equip $i $count 'hand').gesture -ne 'none') { continue }
        $bring = if ($Drone) { MakeDrone } else { $null }
        $target = $null
        $yaw = 0.0
        if ($bring) {
            "a drone brought 10 m ahead as the push's replay starts ($bring)"
        } else {
            $target = Nearest (Scan ("push-{0}-scan" -f $i))
            $yaw = if (-not [double]::IsNaN($PushYaw)) { $PushYaw } elseif ($target) { $target.yaw } else { 0.0 }
            if ($target) { "aiming at $($target.name), $($target.m) m: --yaw $([Math]::Round($yaw, 1))" }
        }
        $before = Status
        # The arm raised and held 3 s (the lock takes and is kept), then pushed along its ray; held out after.
        $recording = Join-Path $out ("push-{0}.mevrrec" -f $i)
        & python (Join-Path $W 'tools\make-power-recording.py') $recording --seconds 9 --yaw "$yaw" 'lhand:0.5:1.0:-0.20,-0.10,-0.45' 'push:4.0' 'lrest:7.0:7.5' | Out-Null
        $started = Get-Date
        Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
        if ($bring) { Ask $bring | Write-Output; $target = $true }
        Start-Sleep -Milliseconds 1500
        $lockText = Ask 'mevr powerwheel lock'
        $null = Shot ("push-{0}" -f $i)
        $left = 9.5 - ((Get-Date) - $started).TotalSeconds
        if ($left -gt 0) { Start-Sleep -Milliseconds ([int](1000 * $left)) }
        Tool 'replay.ps1' @('-Stop') | Out-Null
        Start-Sleep -Milliseconds 800
        $after = Status
        $hostile = $after.lock.locks -gt $before.lock.locks -or $after.lock.target -ne 'nothing'
        if ($hostile -or $target) {
            Verdict "$power by the lock and the push" ($after.lock.casts -gt $before.lock.casts -and $after.lock.lastCast -match '^\S.* at \S+_\d+ ') (
                "locks $($before.lock.locks) -> $($after.lock.locks), pushes $($before.lock.pushes) -> $($after.lock.pushes), casts $($before.lock.casts) -> $($after.lock.casts); last '$($after.lock.lastCast)'")
        } else {
            Verdict "$power's push with no hostile near (nothing cast)" ($after.lock.casts -eq $before.lock.casts) (
                "pushes $($before.lock.pushes) -> $($after.lock.pushes), casts $($before.lock.casts) -> $($after.lock.casts); lock '$($after.lock.why)'")
        }
    }
}

$lines = ReadLog
$new = if ($lines.Count -gt $from) { $lines[$from..($lines.Count - 1)] } else { @() }
$kept = @($new | Where-Object { $_ -match '\] \[\w+\] (power wheel:|omni-tool:)' })
$kept | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$verdicts | Set-Content -LiteralPath (Join-Path $out 'verdicts.txt') -Encoding utf8
"results: $out"
