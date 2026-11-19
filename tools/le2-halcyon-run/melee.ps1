# SPDX-License-Identifier: MIT
# ME1's melee by gesture on ME2 (research/20 "The trilogy's one path"; game/Melee), and ME2's own reload on an empty
# magazine: with a stretch open (prep.ps1, launch.ps1, load.ps1: Save 4, Freedom's Progress), on the fake headset:
#   1. Save 4 has no hostile near, and a hostile starts combat, where ME2 draws the weapon by itself: the weapon-away strikes
#      run first, with no enemy ([Throwables] meleeNeedsEnemy = 0 meanwhile: judged on their way to the game);
#   2. the weapon away (Save 4 loads with the pistol out: holstered first, mevr omni holster): `mevr melee strike` (an open hand:
#      refused, only the fist punches) and `mevr melee punch` (the right fist: refused on ME2, which holds no weapon while
#      holstered and whose melee is the held weapon's own; seen 2026-11-17);
#   3. a replayed right fist (grip and trigger held) thrust forward at 4 m/s: the gesture's own path (ik::melee::Gestures,
#      physical rules) to the same dispatcher and the same refusal;
#   4. the pistol out (mevr weapon pistol): the level's own drone factory makes an assault drone (powers.ps1 -Drone's route,
#      research/55), brought 1.1 m ahead of Shepard (-Toward, a world direction; Save 4's facing is 0,-1) in the same console
#      request as `mevr melee strike` (TryMelee, the B button's exec): the squad shoots drones down within a second; then a
#      replayed open left hand thrust forward while the right grip holds the gun (either hand strikes with the weapon out);
#   5. with -Reload: the pistol fired dry by replayed trigger pulls (the right grip held), the ammo before and after
#      (`mevr reload status`) and ME2's own reload in the call trace (no reload button on the sequels: X is the power wheel).
# A call trace of every call whose path holds "melee" or "reload" runs throughout (the game's own TryMelee, the melee custom
# action and its Impact); the reload's own trace has only the player's weapon's reload calls. [Throwables] meleeNeedsEnemy is put
# back after each use. Results in the stretch's session folder, melee-<time>\.
#
#   tools\le2-halcyon-run\melee.ps1 -Holder <you> [-Reload] [-Pulls 16] [-Toward 0,-1] [-Only away,fist,drawn,left]
param(
    [Parameter(Mandatory)][string]$Holder,
    [switch]$Reload,
    [int]$Pulls = 16,
    [string]$Toward = '0,-1',
    [string[]]$Only = @('away', 'fist', 'drawn', 'left')
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$Game = 'LE2'
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME2 stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$out = Join-Path $open.session ('melee-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
$log = Join-Path $bin 'Logs\LE2MEVR.log'
$ini = Join-Path $bin 'ASI\LE2MEVR.ini'

function AskAll([string[]]$commands) {
    # One request: the lines run one after another on the game thread.
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') @commands -TimeoutSeconds 10 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value ("> " + ($commands -join ' ; ') + "`n$joined`n")
    return $joined
}
function Ask([string]$command) { return AskAll @($command) }
function Tool([string]$tool, [string[]]$arguments) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W "tools\$tool") @arguments
}
function ReadLog {
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    # Whole lines only: the file ends with a newline, and an empty last element counted as a line made a `Skip` of the old
    # count skip the first new line (the canary's `call trace: on for` was missed, 2026-11-17).
    try { return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -ne '' }) } finally { $reader.Close() }
}
# A replay; `$bring` (a console line: a drone brought in front) is sent `$bringAt` seconds after it starts.
function Play([string]$tag, [string[]]$events, [double]$seconds, [double]$bringAt = 0, [string]$bring = '') {
    $recording = Join-Path $out ($tag + '.mevrrec')
    & python (Join-Path $W 'tools\make-power-recording.py') $recording --seconds "$seconds" @events | Out-Null
    if ($LASTEXITCODE) { throw "the $tag recording failed" }
    $started = Get-Date
    Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
    if ($bring) {
        $wait = $bringAt - ((Get-Date) - $started).TotalSeconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int](1000 * $wait)) }
        "  brought: " + (Ask $bring)
    }
    $left = $seconds + 0.5 - ((Get-Date) - $started).TotalSeconds
    if ($left -gt 0) { Start-Sleep -Milliseconds ([int](1000 * $left)) }
    Tool 'replay.ps1' @('-Stop') | Out-Null
}
$verdicts = New-Object System.Collections.Generic.List[string]
function Verdict([string]$what, [bool]$ok, [string]$detail) {
    $line = '{0} {1}: {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' }), $what, $detail)
    $verdicts.Add($line)
    Write-Output $line
}
function Note([string]$what, [string]$detail) {
    $line = "NOTE $what`: $detail"
    $verdicts.Add($line)
    Write-Output $line
}
function SetIni([string]$section, [string]$key, [string]$value) {
    # Win32's profile API, as the mod writes it (first section and key win); the mod rereads the file within a second.
    Add-Type -Namespace MevrMelee -Name Ini -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern bool WritePrivateProfileString(string s, string k, string v, string f);' -ErrorAction SilentlyContinue
    [void][MevrMelee.Ini]::WritePrivateProfileString($section, $key, $value, $ini)
}
function LinesSince([int]$from, [string]$pattern) {
    return @((ReadLog) | Select-Object -Skip $from | Where-Object { $_ -match $pattern })
}
# Where the drones go: from Shepard (mevr state), away from where the squad stands (mevr place status), so the squad isn't
# between; returned as a point 80 units over Shepard's centre and the flat direction (MakeDrone puts a drone 110 units that
# way, 60 below the point: at the chest).
function Eyes {
    $state = Ask 'mevr state'
    $me = [regex]::Match($state, ' at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)')
    if (-not $me.Success) { throw "no player in mevr state: $state" }
    $sx, $sy, $sz = [double]$me.Groups[1].Value, [double]$me.Groups[2].Value, [double]$me.Groups[3].Value
    $line = (Ask 'mevr place status') -split "`n" | Where-Object { $_ -match '^\s*\{' } | Select-Object -First 1
    $squad = if ($line) { @(($line | ConvertFrom-Json).squad) } else { @() }
    $fx, $fy = 1.0, 0.0
    if ($Toward) {
        $t = $Toward -split ','
        $n = [Math]::Sqrt([double]$t[0] * [double]$t[0] + [double]$t[1] * [double]$t[1])
        return @($sx, $sy, ($sz + 80), ([double]$t[0] / $n), ([double]$t[1] / $n))
    }
    if ($squad.Count -gt 0) {
        $mx = ($squad | ForEach-Object { [double]$_.at[0] } | Measure-Object -Average).Average
        $my = ($squad | ForEach-Object { [double]$_.at[1] } | Measure-Object -Average).Average
        $dx, $dy = ($sx - $mx), ($sy - $my)
        $n = [Math]::Sqrt($dx * $dx + $dy * $dy)
        if ($n -gt 1) { $fx, $fy = ($dx / $n), ($dy / $n) }
    }
    return @($sx, $sy, ($sz + 80), $fx, $fy)
}
# The level's drone factories (Freedom's Progress, BioD_ProFre_200Vorcha's Drone_Combat_A) make one drone at its spawn point,
# 80 m away behind the door (a factory runs out after a couple of uses: each is tried); the line that brings it 1.1 m ahead of
# the eyes, at the chest, is returned, to be sent with the strike.
function MakeDrone([double[]]$at) {
    $ex, $ey, $ez, $fx, $fy = $at
    $n = [Math]::Sqrt($fx * $fx + $fy * $fy)
    $factories = [regex]::Matches((Ask 'mevr instances SFXGame.SFXSeqAct_AIFactory Drone_Combat_A'), '\n\[(\d+)\] ') | ForEach-Object { $_.Groups[1].Value }
    if (@($factories).Count -eq 0) { throw 'no drone factory in this level' }
    foreach ($factory in $factories) {
        $null = Ask 'mevr instances SFXGame.SFXSeqAct_AIFactory Drone_Combat_A'
        $null = Ask "mevr activate $factory 0"
        Start-Sleep -Seconds 3
        $drones = Ask 'mevr instances SFXGame.SFXPawn_AssaultDrone'
        $far = $null
        foreach ($m in [regex]::Matches($drones, '\n\[(\d+)\] \S+ \S+ at (-?\d+) (-?\d+) (-?\d+)')) {
            $d = [Math]::Sqrt([Math]::Pow([double]$m.Groups[2].Value - $ex, 2) + [Math]::Pow([double]$m.Groups[3].Value - $ey, 2))
            if ($d -gt 3000 -and ($far -eq $null -or $d -gt $far.d)) { $far = [pscustomobject]@{ n = $m.Groups[1].Value; d = $d } }
        }
        if ($far) {
            return ('mevr setloc {0} {1:F0} {2:F0} {3:F0}' -f $far.n, ($ex + 110 * $fx / $n), ($ey + 110 * $fy / $n), ($ez - 60))
        }
    }
    throw 'no drone factory made a drone (they ran out: load the save again)'
}

$from = (ReadLog).Count
Ask 'mevr state' | Write-Output
# Every call whose path holds melee or reload, but the input's per-tick IsReloading poll (it filled the trace's 600 lines in
# seconds) and the HUD's melee hint.
"call trace: " + (Ask 'mevr calltrace 120 melee -isreloading -meleehint -meleeattacker')
$eyes = Eyes
"drones go: $($eyes -join ', ')"
$text = [IO.File]::ReadAllText($ini)
$needs = [regex]::Match($text, '(?im)^[ \t]*meleeNeedsEnemy[ \t]*=[ \t]*([^\s;]+)')
$needsWas = if ($needs.Success) { $needs.Groups[1].Value } else { '1' }

# 2-3. The weapon away (Save 4 loads with the pistol out: ME2's holster first). A hostile starts combat and ME2 then draws the
# weapon by itself, so with the weapon away no drone is made: the strikes are judged on their way to the game
# ([Throwables] meleeNeedsEnemy = 0 meanwhile).
if ($Only -contains 'away' -or $Only -contains 'fist') {
    "holster: " + (Ask 'mevr omni holster')
    Start-Sleep -Seconds 3
    SetIni 'Throwables' 'meleeNeedsEnemy' '0'
    Start-Sleep -Seconds 2
}
if ($Only -contains 'away') {
    $strike = Ask 'mevr melee strike'
    "strike, weapon away: $strike"
    Verdict 'the weapon away, an open hand: refused (only the right fist punches)' ($strike -match 'make a fist') $strike
    $punch = Ask 'mevr melee punch'
    "punch, weapon away: $punch"
    Verdict 'the weapon away, a fist: refused by ME2''s rule (no weapon in hand, its melee is the held weapon''s)' ($punch -match 'no punch with the weapon away') $punch
    Start-Sleep -Seconds 3
}
if ($Only -contains 'fist') {
    $g0 = (ReadLog).Count
    Play 'fist' @('rgrip:0.5:2.6', 'rtrigger:0.5:2.6', 'rpunch:1.2', 'rrest:1.7:2.3') 3.0
    $fist = LinesSince $g0 'melee: the right hand'
    $fist | ForEach-Object { "  $_" }
    Verdict 'a replayed right fist stroke, weapon away: the gesture reaches the dispatcher as a fist (and ME2''s refusal)' (@($fist | Where-Object { $_ -match 'a fist.*no punch with the weapon away' }).Count -gt 0) ($fist -join ' | ')
    Start-Sleep -Seconds 3
}
if ($Only -contains 'away' -or $Only -contains 'fist') {
    $cast = LinesSince $from 'call trace: .*(BioPlayerMeleeScript|SFXCustomAction_Melee)'
    Note 'the punches in the game (the melee power''s script and custom action)' ("$($cast.Count) call(s)" + $(if ($cast.Count) { ': ' + (($cast | Select-Object -First 3) -join ' | ') } else { '' }))
    SetIni 'Throwables' 'meleeNeedsEnemy' $needsWas
    Start-Sleep -Seconds 2
}

# 4. The weapon out: the B button's exec from a stroke of either hand; a drone in front of Shepard for the console's.
if ($Only -contains 'drawn' -or $Only -contains 'left') {
    "draw: " + (Ask 'mevr weapon pistol')
    Start-Sleep -Seconds 3
}
if ($Only -contains 'drawn') {
    $strike = AskAll @((MakeDrone $eyes), 'mevr melee strike')
    "strike, pistol out: $strike"
    Verdict 'the pistol out, a stroke: SFXGameModeDefault.TryMelee' ($strike -match 'TryMelee') $strike
    Start-Sleep -Seconds 3
}
if ($Only -contains 'left') {
    SetIni 'Throwables' 'meleeNeedsEnemy' '0'
    Start-Sleep -Seconds 2
    $g0 = (ReadLog).Count
    Play 'left' @('rgrip:0.2:2.8', 'lpunch:1.2', 'lrest:1.7:2.3') 3.0
    $left = LinesSince $g0 'melee: the left hand'
    $left | ForEach-Object { "  $_" }
    Verdict 'a replayed open left hand stroke, the right grip holding the pistol: TryMelee' (@($left | Where-Object { $_ -match 'TryMelee' }).Count -gt 0) ($left -join ' | ')
    SetIni 'Throwables' 'meleeNeedsEnemy' $needsWas
    Start-Sleep -Seconds 3
}
$hits = LinesSince $from 'melee: (hit|no damage seen)'
$hits | ForEach-Object { "  $_" }
Note 'what the strikes did to the drones' ($hits -join ' | ')

# 5. ME2's own reload on an empty magazine (nothing about reload is changed: only looked at).
if ($Reload) {
    "draw: " + (Ask 'mevr weapon pistol')
    Start-Sleep -Seconds 3
    # The player's weapon only: the AI's reload questions (ShouldReload, CanReload) would fill the trace.
    "call trace: " + (Ask 'mevr calltrace 60 sfxweapon.reload sfxweapon.startreload sfxweapon.tryreload sfxweapon.shouldautoreload sfxweapon.beginfire -ai_ -bioai')
    $before = Ask 'mevr reload status'
    "reload before: $before"
    $r0 = (ReadLog).Count
    $events = @('rgrip:0.2:' + (0.6 + 0.4 * $Pulls + 3.0))
    for ($i = 0; $i -lt $Pulls; ++$i) { $t = 0.6 + 0.4 * $i; $events += ('rtrigger:{0:F2}:{1:F2}' -f $t, ($t + 0.15)) }
    Play 'dry' $events (0.6 + 0.4 * $Pulls + 3.0)
    $after = Ask 'mevr reload status'
    "reload after: $after"
    $reloads = LinesSince $r0 'call trace: (script|event)\s+\S*(Reload|BeginFire)'
    $reloads | Select-Object -First 30 | ForEach-Object { "  $_" }
    Note "ME2 fired dry ($Pulls pulls): its own reload" ("before $before; after $after; $($reloads.Count) call(s) in the trace" +
        $(if ($reloads.Count) { ': ' + (($reloads | Where-Object { $_ -match 'Reload' } | Select-Object -First 6) -join ' | ') } else { '' }))
}

$lines = (ReadLog) | Select-Object -Skip $from
@($lines | Where-Object { $_ -match '\] \[\w+\] (melee:|call trace:|holsters:|reload:)' }) | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$verdicts | Set-Content -LiteralPath (Join-Path $out 'verdicts.txt') -Encoding utf8
"results: $out"
