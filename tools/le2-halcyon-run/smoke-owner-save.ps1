# SPDX-License-Identifier: MIT
# The smoke run on the owner's own path (lane SMOKE, 2026-11-19): the owner found ME2 bugs in the headset that our checks at test
# fixtures (Save 4's spaceport) never met, and nothing judged the logs. This plays the stretch of Freedom's Progress their
# 2026-11-18 autosave stands before ("open the door, go down the stairs, walk partly into the open, an engagement") on the fake
# headset with tracked replayed hands, by script only, takes a snapshot at each step, and judges the log (tools/session-judge.py).
# With a stretch open (prep.ps1, launch.ps1); it loads the save itself (load.ps1, which reads the save and never writes it).
#
#   tools\le2-halcyon-run\smoke-owner-save.ps1 -Holder <you> [-Save <career>\<file>.pcsav] [-NoLoad] [-Fresh] [-God]
#       [-Phases door,stairs,fight,letgo,crouch,down,cover,vault,barrier] [-Tag what]
#
# Phases (Shepard walked by tools/steer-common.ps1: short left-stick replays, his place read back through the console; the view
# turned with the console's `set` on the player controller's Rotation):
#   door     the view south, Shepard 2.7 m from SFXDoor_11 (BioD_ProFre_300Drones), A pressed (the game's use; the mod's interact
#            line names the door). Pass: `interact: A would use SFXDoor ... SFXDoor_11`, then Shepard walks through it.
#   stairs   the first flight down to the landing (z -12509 to -12810), where the drones arrive: the fight is from there.
#   fight    the gun put away and drawn by the holsters' own draw (`mevr weapon away`, `mevr weapon pistol`), then held by the
#            replayed right grip; each hostile `mevr cover pawns` lists faced and shot at (the right controller aimed at it, the
#            trigger in bursts). Pass: `hand aim: shot N` lines, no `holsters: ... stays out`.
#   letgo    mid-fight the right grip let go (no grip in the replay for 4 s). Pass: `holsters: ... goes back in its holster (in
#            combat, where SFXGameModeDefault.TryHolster refuses: its own last step ...)`, not `stays out`.
#   down     the second flight down to the courtyard (z -13109).
#   cover    a surface toward the nearest hostile found (`mevr cover probe`), Shepard walked behind it, held there while shot at.
#            Pass: `vr cover: a hit ... in cover` lines; the judge's VR cover ratio.
#   crouch   L3 (the mod's crouch toggle on the sequels) during the fight, held 4 s, L3 again. Pass: no `arm IK: body anchor:
#            third person`. The PHYSICAL crouch is not covered: the replay's head drop never reaches the host's floor sample yet
#            (lane POS).
#   vault    the barrier at the stairs' foot (CoverLink_41 slot 0 to CoverLink_15 slot 3): Shepard 0.25 m from the slot, facing
#            it, the double A. Pass: `traversal:` commits and Shepard on the far side.
#   barrier  the barrier the owner could not cross (CoverLink_5 slot 1 to CoverLink_3 slot 1, z -13640): walked to if the path
#            allows, else placed there (`mevr traversal place`, said in the log), the double A. Pass as vault.
# -Fresh restarts the game first (launch.ps1 -Kill): the third load of this map in one process crashed the game on 2026-11-19.
# -God turns on the console's god mode first (the drones killed an unarmed walk down the stairs in 7 s); the hits are judged
# either way. Results: the stretch's session folder, smoke-<tag>-<time>\ (steer.txt: every push and step; console.txt; log.txt:
# the mod's lines since the start; halcyon.txt; snapshots.txt; judge.txt and judge.json: tools/session-judge.py's verdict).
param(
    [Parameter(Mandatory)][string]$Holder,
    [string]$Save = 'VR_31_Soldier_141126_336072f\AutoSave.pcsav',
    [switch]$NoLoad,
    [switch]$Fresh,
    [switch]$God,
    [string[]]$Phases = @('door', 'stairs', 'fight', 'letgo', 'crouch', 'down', 'cover', 'vault', 'barrier'),
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'owner'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
. "$PSScriptRoot\..\steer-common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1 and launch.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME2 stretch is held by '$($open.holder)', not '$Holder'" }
if ($Fresh) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'launch.ps1') -Holder $Holder -Kill | Select-Object -Last 2
}
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$out = Join-Path $open.session ('smoke-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
Steer-Init -Out $out
$bin = Get-MELEBinDir (Find-MELEInstall).Root 'LE2'
$log = Join-Path $bin 'Logs\LE2MEVR.log'
$halcyonLog = Join-Path $env:LOCALAPPDATA 'MELE2VR\MELE2VR_Log.txt'
$started = Get-Date

function ReadLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    $stream = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')  # the game keeps it open for writing
    $reader = New-Object IO.StreamReader($stream)
    try { return @($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Close() }
}
function Mode {
    $text = Steer-Ask 'mevr state'
    if ($text -match 'game mode (\w+)') { return $Matches[1] }
    return 'none'
}
function Alive([string]$phase) {
    $mode = Mode
    if ($mode -ne '0') {
        Steer-Note "$phase stopped: game mode $mode (not on foot: a death's Critical Mission Failure is mode 7)"
        return $false
    }
    return $true
}
function Bearing($from, $to) { return [Math]::Atan2($to.y - $from.y, $to.x - $from.x) * 180.0 / [Math]::PI }
# Each hostile in turn: the view turned to it, the right controller aimed up or down at it, the trigger in bursts.
function Shoot([int]$volleys, [string]$label) {
    for ($v = 0; $v -lt $volleys; $v++) {
        $seen = Steer-Pawns
        $targets = @($seen.pawns | Where-Object { $_.hostile } | Sort-Object meters)
        if (-not $seen.eyes -or $targets.Count -eq 0) { Steer-Note "${label}: no hostile to shoot at"; return $false }
        $t = $targets[0]
        $flat = [Math]::Sqrt(($t.x - $seen.eyes.x) * ($t.x - $seen.eyes.x) + ($t.y - $seen.eyes.y) * ($t.y - $seen.eyes.y))
        $pitch = [Math]::Atan2($t.z - $seen.eyes.z, [Math]::Max(1.0, $flat)) * 180.0 / [Math]::PI
        Steer-Face -Yaw (Bearing $seen.eyes $t) -NoCheck
        Steer-Note ("{0}: volley {1} at {2} ({3:0.0} m, {4}, pitch {5:0})" -f $label, ($v + 1), $t.name, $t.meters, $t.verdict, $pitch)
        $aim = 'aim@0@{0:0}' -f $pitch
        Steer-Play ("stand+${aim}:0.3,stand+${aim}+rt:0.25,stand+${aim}:0.25,stand+${aim}+rt:0.25,stand+${aim}:0.25,stand+${aim}+rt:0.25,stand+${aim}:0.3")
        Start-Sleep -Milliseconds 1900
        if ($v -eq 0) { Steer-Snapshot "$Tag-$label" }
    }
    return $true
}
function DoubleA { Steer-Play 'stand:0.4,stand+a:0.1,stand:0.15,stand+a:0.1,stand:0.2'; Start-Sleep -Seconds 3 }

$from = (ReadLines $log).Count
$halcyonFrom = (ReadLines $halcyonLog).Count
Steer-Note ("smoke {0}: phases {1}; save {2}" -f $Tag, ($Phases -join ','), $(if ($NoLoad) { '(none: as the game stands)' } else { $Save }))
if (-not $NoLoad) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'load.ps1') -Holder $Holder -Save $Save 2>&1 |
        ForEach-Object { Steer-Note "load: $_" }
    $from = [Math]::Min($from, (ReadLines $log).Count)
}
Steer-Note ('state: ' + (Steer-Ask 'mevr state').Trim())
if ($God) { Steer-Ask 'god' | Out-Null; Steer-Note 'god mode asked (the console prints nothing for it: judged by surviving)' }
Steer-Hold
Start-Sleep -Seconds 1

try {
    if ($Phases -contains 'door') {
        # The game selects the door from where the head looks between about 2.7 and 4 m away (interact's line names it); A only
        # where it is selected. Up to three places, each a little further back.
        Steer-Face -Yaw -90
        $opened = $false
        foreach ($y in @(8560, 8620, 8680)) {
            $null = Walk-To -X -9252 -Y $y -Within 30 -Label 'SFXDoor_11, 3 m'
            Steer-Face -Yaw -90 -NoCheck
            Start-Sleep -Milliseconds 1200
            $selected = @(ReadLines $log | Select-Object -Skip $from | Select-String 'interact: (A would use|nothing to use)' | Select-Object -Last 1 | ForEach-Object { $_.Line })
            Steer-Note ("door: from y {0}: {1}" -f $y, ($selected -join ''))
            if (($selected -join '') -match 'SFXDoor_11') {
                Steer-Snapshot "$Tag-door"
                Steer-Play 'stand+a:0.15,stand:0.5'
                Start-Sleep -Seconds 2
                $opened = $true
                break
            }
        }
        Steer-Note ("door: {0}: {1}" -f $(if ($opened) { 'A pressed with SFXDoor_11 selected' } else { 'SFXDoor_11 never selected' }),
            ((ReadLines $log | Select-Object -Skip $from | Select-String 'traversal: A pressed|interact: (A would use|nothing)' | Select-Object -Last 2 | ForEach-Object { $_.Line }) -join ' / '))
    }
    if ($Phases -contains 'stairs' -and (Alive 'stairs')) {
        # The doorway: straight through, Shepard stopped at y 8331 (the door's frame or a squadmate); its west side passes.
        # The top landing's north side is where a squadmate stands (the walk east was stopped at y 8217): its south side.
        foreach ($p in @(@(-9320, 8300, 'the doorway'), @(-9290, 8140, 'the top landing'), @(-8950, 8140, 'the first flight'),
                         @(-8545, 8170, 'its foot'), @(-8560, 7880, 'the turn'), @(-8640, 7720, 'the landing'))) {
            $r = Walk-To -X $p[0] -Y $p[1] -Within 50 -MaxSteps 10 -Label $p[2]
        }
        Steer-Snapshot "$Tag-landing"   # the drones come as Shepard reaches the landing: the fight is from here
    }
    if ($Phases -contains 'fight' -and (Alive 'fight')) {
        Steer-Note ('fight: ' + (Steer-Ask 'mevr weapon away').Trim())
        Start-Sleep -Seconds 1
        Steer-Note ('fight: ' + (Steer-Ask 'mevr weapon pistol').Trim())
        Steer-Init -Out $out -Hold 'grip'   # the replayed right grip holds the drawn gun from here on
        Steer-Hold
        Start-Sleep -Seconds 1
        $null = Shoot 4 'fight'
    }
    if ($Phases -contains 'letgo' -and (Alive 'letgo')) {
        Steer-Init -Out $out -Hold ''
        Steer-Note 'letgo: the right grip let go mid-fight'
        Steer-Hold
        Start-Sleep -Seconds 4
        Steer-Snapshot "$Tag-letgo"
        Steer-Note ('letgo: ' + ((ReadLines $log | Select-Object -Skip $from | Select-String 'holsters: (let go|the .* (goes back|stays out|waits)|.*stays out)' | Select-Object -Last 3 | ForEach-Object { $_.Line }) -join ' / '))
        Steer-Note ('letgo: drawn again: ' + (Steer-Ask 'mevr weapon pistol').Trim())
        Steer-Init -Out $out -Hold 'grip'
        Steer-Hold
        Start-Sleep -Seconds 2
    }
    if ($Phases -contains 'crouch' -and (Alive 'crouch')) {
        Steer-Note 'crouch: L3 (the mod''s crouch toggle), held 4 s, L3 again'
        Steer-Play 'stand+l3:0.06,stand:4'
        Start-Sleep -Seconds 2
        Steer-Snapshot "$Tag-crouched"
        Start-Sleep -Seconds 2.5
        Steer-Play 'stand+l3:0.06,stand:1'
        Start-Sleep -Seconds 2
    }
    if ($Phases -contains 'down' -and (Alive 'down')) {
        foreach ($p in @(@(-8800, 7800, 'the second flight'), @(-9050, 7810, 'its middle'), @(-9290, 7800, 'its foot'))) {
            $r = Walk-To -X $p[0] -Y $p[1] -Within 50 -MaxSteps 10 -Label $p[2]
        }
        Steer-Face -Yaw 180 -NoCheck
        Steer-Snapshot "$Tag-courtyard"
    }
    if ($Phases -contains 'cover' -and (Alive 'cover')) {
        $seen = Steer-Pawns
        $t = @($seen.pawns | Where-Object { $_.hostile } | Sort-Object meters | Select-Object -First 1)
        if ($t.Count -gt 0 -and $seen.eyes) {
            $toward = Bearing $seen.eyes $t[0]
            $best = $null
            foreach ($off in @(0, -20, 20, -40, 40, -60, 60)) {
                $yaw = Steer-Wrap ($toward + $off)
                $probe = Steer-Ask ('mevr cover probe {0:0} 8' -f $yaw)
                if ($probe -match 'at ([\d.]+) m along yaw (-?\d+): \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)') {
                    $d = [double]$Matches[1]
                    if ($d -ge 0.8 -and ($null -eq $best -or $d -lt $best.d)) { $best = @{ d = $d; yaw = $yaw; x = [double]$Matches[3]; y = [double]$Matches[4] } }
                }
            }
            if ($best) {
                $r = [Math]::PI * $best.yaw / 180.0
                $standAt = @{ x = $best.x - [Math]::Cos($r) * 55.0; y = $best.y - [Math]::Sin($r) * 55.0 }
                Steer-Note ("cover: a surface {0:0.0} m along yaw {1:0} toward {2}: Shepard 0.55 m behind it" -f $best.d, $best.yaw, $t[0].name)
                $null = Walk-To -X $standAt.x -Y $standAt.y -Within 25 -MaxSteps 12 -Label 'behind the surface'
                Steer-Face -Yaw $best.yaw -NoCheck
                Steer-Snapshot "$Tag-cover"
                Start-Sleep -Seconds 6
                Steer-Note ('cover: ' + (((Steer-Pawns).pawns | Where-Object { $_.hostile } | ForEach-Object { '{0} {1:0.0} m: {2}' -f $_.name, $_.meters, $_.verdict }) -join ' / '))
                $null = Shoot 2 'cover'
            } else {
                Steer-Note 'cover: no surface within 8 m toward the nearest hostile'
            }
        } else {
            Steer-Note 'cover: no hostile left to take cover from'
        }
    }
    if ($Phases -contains 'vault' -and (Alive 'vault')) {
        # CoverLink_41 slot 0 at (-9485, 7757), yaw 90: over the barrier to CoverLink_15 slot 3 at y 7933.
        # Turned first: an instant 180-degree turn of the view put arm IK on its third-person anchor for a tick (2026-11-19).
        Steer-Face -Yaw 90 -NoCheck
        $r = Walk-To -X -9485 -Y 7735 -Within 12 -MaxSteps 16 -Label 'CoverLink_41 slot 0, 0.25 m back'
        Steer-Face -Yaw 90 -NoCheck
        Steer-Note ('vault: ' + (Steer-Ask 'mevr traversal near').Trim())
        Steer-Snapshot "$Tag-vault-before"
        DoubleA
        $at = Steer-Where
        Steer-Note ("vault: after the double A Shepard is at ({0:0}, {1:0}, {2:0}): {3}" -f $at.x, $at.y, $at.z, $(if ($at.y -gt 7850) { 'over the barrier' } else { 'still on the near side' }))
    }
    if ($Phases -contains 'barrier' -and (Alive 'barrier')) {
        # CoverLink_5 slot 1 at (-10352, 4978, -13640), yaw -90: over to CoverLink_3 slot 1 at y 4852 (the owner's refusals).
        $r = Walk-To -X -10352 -Y 5003 -Within 15 -MaxSteps 30 -Label 'CoverLink_5 slot 1, 0.25 m back'
        if (-not $r.arrived) {
            Steer-Note ('barrier: not reached on foot; placed instead (a test placement, not the path): ' + (Steer-Ask 'mevr traversal place CoverLink_5 1 0.25').Trim())
            Start-Sleep -Seconds 2
        }
        Steer-Face -Yaw -90 -NoCheck
        Steer-Note ('barrier: ' + (Steer-Ask 'mevr traversal near').Trim())
        Steer-Snapshot "$Tag-barrier-before"
        DoubleA
        $at = Steer-Where
        Steer-Note ("barrier: after the double A Shepard is at ({0:0}, {1:0}, {2:0}): {3}" -f $at.x, $at.y, $at.z, $(if ($at.y -lt 4900) { 'over the barrier' } else { 'still on the near side' }))
        Steer-Snapshot "$Tag-barrier-after"
    }
} finally {
    Steer-Stop
    Start-Sleep -Seconds 6  # the last snapshot written
    $mine = @(ReadLines $log | Select-Object -Skip $from)
    Set-Content -LiteralPath (Join-Path $out 'log.txt') -Value $mine -Encoding utf8
    $halcyonMine = @(ReadLines $halcyonLog | Select-Object -Skip $halcyonFrom)
    Set-Content -LiteralPath (Join-Path $out 'halcyon.txt') -Value $halcyonMine -Encoding utf8
    Steer-Snapshots $started | Set-Content -LiteralPath (Join-Path $out 'snapshots.txt') -Encoding utf8
    & python (Join-Path $W 'tools\session-judge.py') --mod (Join-Path $out 'log.txt') --halcyon (Join-Path $out 'halcyon.txt') `
        --crash-dir $MevrDataDir --since-file-time $started.ToString('o') --json (Join-Path $out 'judge.json') |
        Tee-Object -FilePath (Join-Path $out 'judge.txt')
    "results: $out"
}
