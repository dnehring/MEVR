# SPDX-License-Identifier: MIT
# Walking Shepard by script on the fake headset (lane SMOKE, 2026-11-19): short replays of the left stick, Shepard's place read
# back through the console after each, the stick's world direction learned from how far and which way each push moved him. For
# the smoke runs on a mission's real path (tools/le2-halcyon-run/smoke-owner-save.ps1) and any later one, on every game:
#
#   . tools\steer-common.ps1
#   Steer-Init -Out <folder> [-Hold 'grip']           # the parts every replay holds (here the right grip: a drawn gun stays)
#   Steer-Hold                                         # tracked hands standing still until the next replay
#   Walk-To -X -9250 -Y 8420 [-Within 60]              # short pushes until within that many units (flat), or stuck
#   Steer-Face -Yaw -90                                # the view turned to a world yaw (UE degrees: 0 +X, 90 +Y)
#   Steer-Play 'stand+a:0.15,stand:2'                  # any make-walk-recording.py plan, the held parts added to each segment
#   Steer-Where                                        # @{ x; y; z } from `mevr state` (ME2, ME3) or `mevr loc` (ME1)
#
# Each replay is a one-shot whose last segment stands for 10 minutes: the next request replaces it at once (the test loop's
# pending replay), so the controllers never drop out between steps (the holsters let a gun go when they do). The replays carry
# their inputs (`inputs=1`). How the stick maps onto the world is measured, never assumed: with the head replayed straight
# ahead the stick moves along the game's view yaw (ControlRotation), so a push's direction minus the stick's angle is the view
# yaw; every push that moved Shepard updates it. Steer-Face sets the controller's rotation with the console's `set` (the
# player controller's class, found once) and checks it by a short push. Everything goes to <Out>\steer.txt.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$script:SteerRoot = (Resolve-Path "$PSScriptRoot\..").Path
$script:SteerOut = $null
$script:SteerHold = ''
$script:SteerPitch = 0.0
$script:SteerHands = 'view'      # make-walk-recording.py's --hands: view (held up in view) or chest (out of the eyes' snapshots)
$script:SteerYaw = 0.0            # the head turned this far right in the room for every replayed plan (Steer-Init -Yaw)
$script:SteerHeading = $null     # the view yaw the last pushes showed (degrees, UE: 0 along +X, 90 along +Y)
$script:SteerCount = 0
$script:SteerSpeed = 380.0       # units per second at the replay's stick (0.9: a run), corrected by every push
$script:SteerController = $null  # the player controller's class name for `set`
$script:SteerLocked = $false     # the view yaw is known (Steer-Face set and checked it): pushes deflected by walls don't relearn it

function Steer-Init([Parameter(Mandatory)][string]$Out, [string]$Hold = '', [double]$Pitch = 0.0, [double]$Yaw = 0.0,
    [ValidateSet('view', 'chest')][string]$Hands = 'view') {
    New-Item -ItemType Directory -Force $Out | Out-Null
    $script:SteerHands = $Hands
    $script:SteerOut = $Out
    $script:SteerHold = (($Hold -split '\+') | Where-Object { $_ }) -join '+'
    $script:SteerPitch = $Pitch
    $script:SteerYaw = $Yaw
}

function Steer-Note([string]$text) {
    $line = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss.fff'), $text
    if ($script:SteerOut) { Add-Content -LiteralPath (Join-Path $script:SteerOut 'steer.txt') -Value $line -Encoding utf8 }
    Write-Host $line  # never the pipeline: the functions return places
}

# The console's request and result files in this process (tools/console.ps1's protocol without a new PowerShell per line).
function Steer-Ask([string]$command, [int]$timeout = 10) {
    $result = Join-Path $MevrDataDir 'console.result'
    if (Test-Path -LiteralPath $result) { Remove-Item -LiteralPath $result -Force }
    $request = Write-MevrRequest 'console.request' $command
    $deadline = (Get-Date).AddSeconds($timeout)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $result)) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path -LiteralPath $result)) {
        if (Test-Path -LiteralPath $request) { Remove-Item -LiteralPath $request -Force }
        return ''
    }
    Start-Sleep -Milliseconds 30
    $text = [IO.File]::ReadAllText($result)
    if ($script:SteerOut) { Add-Content -LiteralPath (Join-Path $script:SteerOut 'console.txt') -Value "> $command`n$text" -Encoding utf8 }
    # The console answers on a loading screen too (the 2026-11-20 ME3 hang): say so once no world has been drawn for 30 s
    # (tools/game-watch.ps1, common.ps1 Get-MevrWorldVerdict).
    $verdict = Get-MevrWorldVerdict
    if ($verdict.State -in 'no-world', 'blocked', 'no-frames' -and $verdict.Seconds -ge 30) {
        $warning = "game-watch: $($verdict.Detail) (tools/game-watch.ps1 -UntilWorld waits for it, failing at 120 s)"
        Write-Warning $warning
        if ($script:SteerOut) { Add-Content -LiteralPath (Join-Path $script:SteerOut 'console.txt') -Value "! $warning" -Encoding utf8 }
    }
    return $text
}

function Steer-Where {
    $text = Steer-Ask 'mevr state'
    if ($text -match 'at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)') {
        return @{ x = [double]$Matches[1]; y = [double]$Matches[2]; z = [double]$Matches[3]; text = $text.Trim() }
    }
    $text = Steer-Ask 'mevr loc'
    if ($text -match 'loc (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)') {
        return @{ x = [double]$Matches[1]; y = [double]$Matches[2]; z = [double]$Matches[3]; text = $text.Trim() }
    }
    return $null
}

function Steer-Wrap([double]$deg) {
    $d = $deg % 360.0
    if ($d -gt 180.0) { $d -= 360.0 }
    if ($d -le -180.0) { $d += 360.0 }
    return $d
}

# A plan of make-walk-recording.py, the held parts added to each segment, replayed with its inputs; returns once the game took
# the request (not when the plan ends). The plan's last segment should stand long (it does unless -NoTail).
function Steer-Play([Parameter(Mandatory)][string]$Plan, [switch]$NoTail) {
    $parts = @(foreach ($segment in ($Plan -split ',')) {
        $name, $seconds = $segment.Substring(0, $segment.LastIndexOf(':')), $segment.Substring($segment.LastIndexOf(':') + 1)
        if ($script:SteerHold -and $name -notmatch '(^|\+)nohold($|\+)') { $name = "$name+$($script:SteerHold)" }
        $name = ($name -split '\+' | Where-Object { $_ -ne 'nohold' }) -join '+'
        if (-not $name) { $name = 'stand' }
        "${name}:$seconds"
    })
    if (-not $NoTail) { $parts += $(if ($script:SteerHold) { "stand+$($script:SteerHold):600" } else { 'stand:600' }) }
    $script:SteerCount++
    $file = Join-Path $script:SteerOut ('steer-{0:D3}.mevrrec' -f $script:SteerCount)
    $made = & python (Join-Path $script:SteerRoot 'tools\make-walk-recording.py') $file --plan ($parts -join ',') --hands $script:SteerHands --pitch $script:SteerPitch --yaw $script:SteerYaw
    if ($LASTEXITCODE -ne 0) { throw "make-walk-recording.py failed: $made" }
    $request = Write-MevrRequest 'replay.request' ((Resolve-Path -LiteralPath $file).Path + ' inputs=1')
    $deadline = (Get-Date).AddSeconds(5)
    while ((Test-Path -LiteralPath $request) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
    if (Test-Path -LiteralPath $request) { throw 'the game did not take the replay request' }
}

function Steer-Hold { Steer-Play 'stand:0.1' }

function Steer-Stop {
    $request = Write-MevrRequest 'replay.request' 'stop'
    $deadline = (Get-Date).AddSeconds(5)
    while ((Test-Path -LiteralPath $request) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
}

# One push of the left stick at an angle (degrees clockwise from the view's forward) for that long; Shepard's place after it.
# A push that moved him teaches the view yaw and the speed.
function Steer-Push([double]$Angle, [double]$Seconds, [string]$Extra = '') {
    $before = Steer-Where
    $name = ('stick@{0:0.#}' -f $Angle) + $Extra
    Steer-Play ("{0}:{1:0.###}" -f $name, $Seconds)
    Start-Sleep -Milliseconds ([int](($Seconds + 0.25) * 1000))
    $after = Steer-Where
    if ($null -eq $before -or $null -eq $after) { return $after }
    $dx = $after.x - $before.x; $dy = $after.y - $before.y
    $moved = [Math]::Sqrt($dx * $dx + $dy * $dy)
    $speed = $moved / [Math]::Max(0.1, $Seconds)
    if ($moved -gt 40.0 -and $speed -lt 650.0) {
        # A push faster than a run was something else (a fall, a ragdoll, a scripted move): it teaches nothing.
        $heading = Steer-Wrap ([Math]::Atan2($dy, $dx) * 180.0 / [Math]::PI - $Angle)
        if (-not $script:SteerLocked -and ($null -eq $script:SteerHeading -or $moved -gt 80.0)) { $script:SteerHeading = $heading }
        $script:SteerSpeed = [Math]::Min(600.0, [Math]::Max(150.0, 0.5 * $script:SteerSpeed + 0.5 * $speed))
    }
    Steer-Note ("push {0:0} deg for {1:0.00} s: ({2:0}, {3:0}, {4:0}) -> ({5:0}, {6:0}, {7:0}), moved {8:0} units; view yaw {9}" -f
        $Angle, $Seconds, $before.x, $before.y, $before.z, $after.x, $after.y, $after.z, $moved,
        $(if ($null -eq $script:SteerHeading) { 'unknown' } else { '{0:0}' -f $script:SteerHeading }))
    return $after
}

# The player controller's class for `set` (the live one's name, e.g. SFXPlayerController_2 -> SFXPlayerController).
function Steer-ControllerClass {
    if ($script:SteerController) { return $script:SteerController }
    $text = Steer-Ask 'mevr instances Engine.PlayerController'
    if ($text -match '\[0\] (\w+) ') { $script:SteerController = $Matches[1] }
    return $script:SteerController
}

# The view turned to a world yaw: the controller's Rotation set (ControlRotation), then a short push forward checks it. Falls
# back to the right stick (the game's own turn) if `set` has no effect.
function Steer-Face([double]$Yaw, [switch]$NoCheck) {
    $class = Steer-ControllerClass
    $units = [int][Math]::Round((Steer-Wrap $Yaw) * 65536.0 / 360.0)
    if ($class) {
        Steer-Ask ("set {0} Rotation (Pitch=0,Yaw={1},Roll=0)" -f $class, $units) | Out-Null
        $script:SteerHeading = Steer-Wrap $Yaw
        Steer-Note ("face {0:0} deg: set {1} Rotation yaw {2}" -f $Yaw, $class, $units)
    }
    $script:SteerLocked = $false
    if ($NoCheck) { $script:SteerLocked = [bool]$class; return }
    $was = $script:SteerHeading
    $script:SteerHeading = $null
    $null = Steer-Push 0 0.25
    if ($null -eq $script:SteerHeading) {
        $script:SteerHeading = $was
        Steer-Note 'face: the check push did not move Shepard (blocked?); the yaw is assumed'
        return
    }
    $off = Steer-Wrap ($Yaw - $script:SteerHeading)
    if ([Math]::Abs($off) -le 15.0) { $script:SteerHeading = Steer-Wrap $Yaw; $script:SteerLocked = $true }
    if ([Math]::Abs($off) -gt 10.0) {
        # `set` did not turn the view (or the game turned it back): the right stick, the game's own turn, until it faces.
        for ($try = 0; $try -lt 6 -and [Math]::Abs($off) -gt 10.0; $try++) {
            $seconds = [Math]::Min(1.5, [Math]::Abs($off) / 120.0)
            Steer-Play ("stand+turn@{0}:{1:0.00}" -f $(if ($off -gt 0) { '1' } else { '-1' }), $seconds)
            Start-Sleep -Milliseconds ([int](($seconds + 0.3) * 1000))
            $script:SteerHeading = $null
            $null = Steer-Push 0 0.25
            if ($null -eq $script:SteerHeading) { break }
            $off = Steer-Wrap ($Yaw - $script:SteerHeading)
            Steer-Note ("face: by the right stick, {0:0} deg left to turn" -f $off)
        }
    }
}

# Short pushes toward a point until Shepard is within -Within units of it (flat), or stuck: a push that moves him under 30
# units counts as blocked; the next push waits a moment and tries again (a squadmate in the way moves on), then 60 and 90
# degrees to either side of the goal, never further round (backing off through a door led the walk astray), then gives up.
function Walk-To([double]$X, [double]$Y, [double]$Within = 60.0, [int]$MaxSteps = 40, [double]$StepSeconds = 0.6, [string]$Label = '') {
    $at = Steer-Where
    if ($null -eq $at) { throw 'no position from the console (no pawn?)' }
    Steer-Note ("walk to ({0:0}, {1:0}){2} from ({3:0}, {4:0}, {5:0})" -f $X, $Y, $(if ($Label) { " ($Label)" } else { '' }), $at.x, $at.y, $at.z)
    if ($null -eq $script:SteerHeading) { $at = Steer-Push 0 0.3 }
    $blocked = 0
    for ($step = 0; $step -lt $MaxSteps; $step++) {
        $dx = $X - $at.x; $dy = $Y - $at.y
        $distance = [Math]::Sqrt($dx * $dx + $dy * $dy)
        if ($distance -le $Within) {
            Steer-Note ("arrived {0:0} units from ({1:0}, {2:0}) at ({3:0}, {4:0}, {5:0})" -f $distance, $X, $Y, $at.x, $at.y, $at.z)
            return @{ arrived = $true; at = $at; distance = $distance }
        }
        $bearing = [Math]::Atan2($dy, $dx) * 180.0 / [Math]::PI
        $heading = if ($null -eq $script:SteerHeading) { 0.0 } else { $script:SteerHeading }
        $angle = Steer-Wrap ($bearing - $heading)
        if ($blocked -eq 1) { Start-Sleep -Milliseconds 800 }
        $aside = @{ 2 = 60.0; 3 = -60.0; 4 = 90.0; 5 = -90.0 }
        if ($aside.ContainsKey($blocked)) { $angle = Steer-Wrap ($angle + $aside[$blocked]) }
        $seconds = [Math]::Max(0.05, [Math]::Min($StepSeconds, ($distance - 0.3 * $Within) / $script:SteerSpeed))  # short near the goal: 0.15 s overshot a 15-unit goal both ways
        $before = $at
        $at = Steer-Push $angle $seconds
        if ($null -eq $at) { throw 'no position from the console (no pawn?)' }
        $moved = [Math]::Sqrt(($at.x - $before.x) * ($at.x - $before.x) + ($at.y - $before.y) * ($at.y - $before.y))
        if ($moved -lt [Math]::Min(30.0, 0.4 * $seconds * $script:SteerSpeed)) { $blocked++ } else { $blocked = 0 }
        if ($blocked -ge 6) {
            Steer-Note ("stuck at ({0:0}, {1:0}, {2:0}), {3:0} units from the goal" -f $at.x, $at.y, $at.z, $distance)
            return @{ arrived = $false; at = $at; distance = $distance }
        }
    }
    Steer-Note ("gave up after {0} steps at ({1:0}, {2:0}, {3:0})" -f $MaxSteps, $at.x, $at.y, $at.z)
    return @{ arrived = $false; at = $at; distance = [Math]::Sqrt(($X - $at.x) * ($X - $at.x) + ($Y - $at.y) * ($Y - $at.y)) }
}

# A snapshot of both eyes, tagged (the JSON's "tag"): -Wait runs tools/snapshot.ps1 and notes the files; without it only the
# request is written (the game keeps running while the eyes are written, about 5 s; Steer-Snapshots lists them afterwards).
function Steer-Snapshot([string]$Tag, [switch]$Wait) {
    if ($Wait) {
        $shot = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $script:SteerRoot 'tools\snapshot.ps1') -Tag $Tag 2>$null
        Steer-Note ("snapshot {0}: {1}" -f $Tag, (($shot | Where-Object { $_ }) -join ' '))
        return
    }
    $request = Join-Path $MevrDataDir 'snapshot.request'
    $deadline = (Get-Date).AddSeconds(8)
    while ((Test-Path -LiteralPath $request) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }  # the one before taken
    $null = Write-MevrRequest 'snapshot.request' "count=1 interval=1 delay=0 tag=$Tag"
    Steer-Note "snapshot $Tag requested"
}

# The snapshots written since a time: stamp, tag and what the mod said about the body (arm IK's status, each drawn wrist from
# its controller, first person), one line each.
function Steer-Snapshots([datetime]$Since) {
    $dir = Join-Path $MevrDataDir 'snapshots'
    if (-not (Test-Path -LiteralPath $dir)) { return @() }
    foreach ($file in @(Get-ChildItem -LiteralPath $dir -Filter '*.json' | Where-Object { $_.LastWriteTime -ge $Since } | Sort-Object Name)) {
        try { $j = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json } catch { continue }
        $arm = $j.mod.armIk
        $hands = if ($arm -and $arm.hands) {
            'hands first person {0}, left {1} m from its grip, right {2} m' -f $arm.hands.firstPerson, $arm.hands.left.fromGripM, $arm.hands.right.fromGripM
        } else { 'no hands measured' }
        '{0} [{1}] game mode {2}; arm IK: {3}; {4}' -f $j.stamp, $j.tag, $j.mod.gameMode, $(if ($arm) { $arm.status } else { 'none' }), $hands
    }
}

# The pawns VR cover would judge (`mevr cover pawns`): the eyes and each pawn's place, distance, hostility and verdict.
function Steer-Pawns {
    $text = Steer-Ask 'mevr cover pawns'
    $eyes = $null
    if ($text -match 'the eyes at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)') { $eyes = @{ x = [double]$Matches[1]; y = [double]$Matches[2]; z = [double]$Matches[3] } }
    $pawns = @(foreach ($line in ($text -split "`r?`n")) {
        if ($line -match '^\s*\[(\d+)\] (hostile )?(\S+) at ([\d.]+) m: ([^|]+)\|.*\(at (-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)\s*$') {
            [pscustomobject]@{ index = [int]$Matches[1]; hostile = [bool]$Matches[2]; name = $Matches[3]; meters = [double]$Matches[4];
                verdict = $Matches[5].Trim(); x = [double]$Matches[6]; y = [double]$Matches[7]; z = [double]$Matches[8] }
        }
    })
    return @{ eyes = $eyes; pawns = $pawns; text = $text }
}
